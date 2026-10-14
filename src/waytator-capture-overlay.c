#include "waytator-capture-overlay.h"
#include "waytator-window.h"

#include <gtk4-layer-shell.h>
#include <math.h>

//in logical pixels, anything smaller is a stray click rather than a selection
#define WAYTATOR_CAPTURE_MIN_SELECTION 3.0

typedef struct _WaytatorCaptureOverlay WaytatorCaptureOverlay;

#define WAYTATOR_TYPE_CAPTURE_VIEW (waytator_capture_view_get_type())

G_DECLARE_FINAL_TYPE(WaytatorCaptureView, waytator_capture_view, WAYTATOR, CAPTURE_VIEW, GtkWidget)

//one per monitor, shows that monitor's frozen frame with the selection drawn on top
struct _WaytatorCaptureView {
  GtkWidget parent_instance;

  WaytatorCaptureOverlay *overlay;
  WaytatorCaptureOutput *output;
  GdkTexture *texture;
  GtkWidget *pill;
  GtkToggleButton *area_button;
  GtkToggleButton *screen_button;
  GtkLabel *hint_label;
  gboolean hovered;
  gboolean selecting;
  double start_x;
  double start_y;
  double end_x;
  double end_y;
};

struct _WaytatorCaptureOverlay {
  GPtrArray *outputs;
  GPtrArray *windows;
  GPtrArray *views;
  GtkCssProvider *pill_style;
  WaytatorCaptureMode mode;
  gboolean updating;
  gboolean closed;
  gboolean hide_waytator;
  WaytatorCaptureOverlayRetakeCallback retake;
  WaytatorCaptureOverlayCallback callback;
  gpointer user_data;
};

G_DEFINE_FINAL_TYPE(WaytatorCaptureView, waytator_capture_view, GTK_TYPE_WIDGET)

//cairo leaves the spare byte of RGB24 undefined, and a screenshot is opaque either way
static GdkTexture *
waytator_capture_texture_new(cairo_surface_t *surface,
                             int              x,
                             int              y,
                             int              width,
                             int              height)
{
  const guchar *src;
  gsize src_stride;
  const gsize stride = (gsize) width * 4;
  guint32 *pixels;
  g_autoptr(GBytes) bytes = NULL;

  cairo_surface_flush(surface);
  src = cairo_image_surface_get_data(surface);
  src_stride = cairo_image_surface_get_stride(surface);
  pixels = g_malloc(stride * height);

  for (int row = 0; row < height; row++) {
    const guint32 *src_row = (const guint32 *) (src + (gsize) (y + row) * src_stride) + x;
    guint32 *dst_row = pixels + (gsize) row * width;

    for (int column = 0; column < width; column++)
      dst_row[column] = src_row[column] | 0xff000000u;
  }

  bytes = g_bytes_new_take(pixels, stride * height);
  return gdk_memory_texture_new(width, height, GDK_MEMORY_DEFAULT, bytes, stride);
}

GdkTexture *
waytator_capture_output_create_texture(WaytatorCaptureOutput *output)
{
  return waytator_capture_texture_new(output->surface,
                                      0,
                                      0,
                                      cairo_image_surface_get_width(output->surface),
                                      cairo_image_surface_get_height(output->surface));
}

//the screen a capture is about when there is nothing to ask, NULL when the overlay has to
WaytatorCaptureOutput *
waytator_capture_pick_output(GPtrArray          *outputs,
                             WaytatorCaptureMode mode,
                             const char         *output_name,
                             GError            **error)
{
  if (mode != WAYTATOR_CAPTURE_SCREEN)
    return NULL;

  if (output_name == NULL)
    return outputs->len == 1 ? g_ptr_array_index(outputs, 0) : NULL;

  for (guint i = 0; i < outputs->len; i++) {
    WaytatorCaptureOutput *output = g_ptr_array_index(outputs, i);

    if (g_strcmp0(output->name, output_name) == 0)
      return output;
  }

  g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "There is no screen named %s", output_name);
  return NULL;
}

//the pill is one of the floating controls, read from the settings since there may be no window to ask
static GtkCssProvider *
waytator_capture_overlay_new_pill_style(void)
{
  g_autofree char *path = g_build_filename(g_get_user_config_dir(), WAYTATOR_SETTINGS_FILE, NULL);
  g_autoptr(GKeyFile) key_file = g_key_file_new();
  g_autofree char *css = NULL;
  GtkCssProvider *provider = gtk_css_provider_new();
  gboolean blur = TRUE;
  double opacity = 0.7;
  char opacity_text[G_ASCII_DTOSTR_BUF_SIZE];

  if (g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, NULL)) {
    if (g_key_file_has_key(key_file, WAYTATOR_SETTINGS_GROUP, "floating_controls_blur", NULL))
      blur = g_key_file_get_boolean(key_file, WAYTATOR_SETTINGS_GROUP, "floating_controls_blur", NULL);
    if (g_key_file_has_key(key_file, WAYTATOR_SETTINGS_GROUP, "floating_controls_opacity", NULL)) {
      const double value = g_key_file_get_double(key_file,
                                                 WAYTATOR_SETTINGS_GROUP,
                                                 "floating_controls_opacity",
                                                 NULL);

      if (value >= 0.0 && value <= 1.0)
        opacity = value;
    }
  }

  //printf would use the locale's decimal separator, which is not valid css
  g_ascii_formatd(opacity_text, sizeof opacity_text, "%.2f", opacity);
  css = g_strdup_printf(".capture-pill { background: alpha(black, %s); background-color: alpha(black, %s);"
                        " backdrop-filter: %s; }",
                        opacity_text,
                        opacity_text,
                        blur ? "blur(18px)" : "none");
  gtk_css_provider_load_from_string(provider, css);
  gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                             GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
  return provider;
}

static gboolean
waytator_capture_overlay_free(gpointer user_data)
{
  WaytatorCaptureOverlay *overlay = user_data;

  g_ptr_array_unref(overlay->outputs);
  g_ptr_array_unref(overlay->windows);
  g_ptr_array_unref(overlay->views);
  gtk_style_context_remove_provider_for_display(gdk_display_get_default(),
                                                GTK_STYLE_PROVIDER(overlay->pill_style));
  g_object_unref(overlay->pill_style);
  g_free(overlay);
  return G_SOURCE_REMOVE;
}

//FALSE when it already closed, events can still arrive while the windows go away
static gboolean
waytator_capture_overlay_close(WaytatorCaptureOverlay *overlay)
{
  if (overlay->closed)
    return FALSE;

  overlay->closed = TRUE;
  for (guint i = 0; i < overlay->windows->len; i++)
    gtk_window_destroy(g_ptr_array_index(overlay->windows, i));

  //this usually runs inside a handler of one of the views, which still points at the overlay
  g_idle_add(waytator_capture_overlay_free, overlay);
  return TRUE;
}

static void
waytator_capture_overlay_finish(WaytatorCaptureOverlay *overlay,
                                GdkTexture             *texture)
{
  //a screen pick hands over the view's own texture, which goes with the windows
  g_autoptr(GdkTexture) kept = texture != NULL ? g_object_ref(texture) : NULL;

  if (waytator_capture_overlay_close(overlay))
    overlay->callback(kept, overlay->mode, overlay->user_data);
}

static void
waytator_capture_overlay_hide_toggled(GtkCheckButton         *button,
                                      WaytatorCaptureOverlay *overlay)
{
  if (waytator_capture_overlay_close(overlay))
    overlay->retake(gtk_check_button_get_active(button), overlay->mode, overlay->user_data);
}

static void
waytator_capture_overlay_set_pills_visible(WaytatorCaptureOverlay *overlay,
                                           gboolean                visible)
{
  for (guint i = 0; i < overlay->views->len; i++) {
    WaytatorCaptureView *view = g_ptr_array_index(overlay->views, i);

    gtk_widget_set_visible(view->pill, visible);
  }
}

static void
waytator_capture_overlay_set_mode(WaytatorCaptureOverlay *overlay,
                                  WaytatorCaptureMode     mode)
{
  const gboolean area = mode == WAYTATOR_CAPTURE_AREA;

  overlay->mode = mode;
  overlay->updating = TRUE;

  for (guint i = 0; i < overlay->views->len; i++) {
    WaytatorCaptureView *view = g_ptr_array_index(overlay->views, i);

    gtk_toggle_button_set_active(area ? view->area_button : view->screen_button, TRUE);
    gtk_label_set_text(view->hint_label,
                       area ? "Drag to select · Esc to cancel" : "Click a screen · Esc to cancel");
    gtk_widget_set_cursor_from_name(GTK_WIDGET(view), area ? "crosshair" : "default");
    view->selecting = FALSE;
    gtk_widget_queue_draw(GTK_WIDGET(view));
  }

  overlay->updating = FALSE;
}

static gboolean
waytator_capture_view_get_selection(WaytatorCaptureView *self,
                                    graphene_rect_t     *selection)
{
  if (!self->selecting)
    return FALSE;

  graphene_rect_init(selection,
                     MIN(self->start_x, self->end_x),
                     MIN(self->start_y, self->end_y),
                     fabs(self->end_x - self->start_x),
                     fabs(self->end_y - self->start_y));
  return TRUE;
}

//the widget is laid out in logical pixels, the frame is in the monitor's real ones
static void
waytator_capture_view_selection_to_pixels(WaytatorCaptureView   *self,
                                          const graphene_rect_t *selection,
                                          int                   *x,
                                          int                   *y,
                                          int                   *width,
                                          int                   *height)
{
  const int surface_width = cairo_image_surface_get_width(self->output->surface);
  const int surface_height = cairo_image_surface_get_height(self->output->surface);
  const double scale_x = (double) surface_width / MAX(gtk_widget_get_width(GTK_WIDGET(self)), 1);
  const double scale_y = (double) surface_height / MAX(gtk_widget_get_height(GTK_WIDGET(self)), 1);
  const int left = CLAMP((int) round(selection->origin.x * scale_x), 0, surface_width);
  const int top = CLAMP((int) round(selection->origin.y * scale_y), 0, surface_height);
  const int right = CLAMP((int) round((selection->origin.x + selection->size.width) * scale_x), 0, surface_width);
  const int bottom = CLAMP((int) round((selection->origin.y + selection->size.height) * scale_y), 0, surface_height);

  *x = left;
  *y = top;
  *width = right - left;
  *height = bottom - top;
}

static void
waytator_capture_view_snapshot_size_label(WaytatorCaptureView   *self,
                                          GtkSnapshot           *snapshot,
                                          const graphene_rect_t *selection,
                                          float                  view_width,
                                          float                  view_height)
{
  const GdkRGBA background = { 0.0, 0.0, 0.0, 0.75 };
  const GdkRGBA foreground = { 1.0, 1.0, 1.0, 1.0 };
  g_autoptr(PangoLayout) layout = NULL;
  g_autofree char *text = NULL;
  GskRoundedRect box;
  int x;
  int y;
  int width;
  int height;
  int text_width;
  int text_height;
  float box_x;
  float box_y;

  waytator_capture_view_selection_to_pixels(self, selection, &x, &y, &width, &height);
  text = g_strdup_printf("%d × %d", width, height);
  layout = gtk_widget_create_pango_layout(GTK_WIDGET(self), text);
  pango_layout_get_pixel_size(layout, &text_width, &text_height);

  box_x = selection->origin.x;
  box_y = selection->origin.y + selection->size.height + 8.0f;
  if (box_y + text_height + 6.0f > view_height)
    box_y = selection->origin.y + selection->size.height - text_height - 14.0f;
  box_x = MAX(MIN(box_x, view_width - text_width - 12.0f), 0.0f);

  gsk_rounded_rect_init_from_rect(&box,
                                  &GRAPHENE_RECT_INIT(box_x, box_y, text_width + 12.0f, text_height + 6.0f),
                                  6.0f);
  gtk_snapshot_push_rounded_clip(snapshot, &box);
  gtk_snapshot_append_color(snapshot, &background, &box.bounds);
  gtk_snapshot_pop(snapshot);

  gtk_snapshot_save(snapshot);
  gtk_snapshot_translate(snapshot, &GRAPHENE_POINT_INIT(box_x + 6.0f, box_y + 3.0f));
  gtk_snapshot_append_layout(snapshot, layout, &foreground);
  gtk_snapshot_restore(snapshot);
}

static void
waytator_capture_view_snapshot_outline(GtkSnapshot           *snapshot,
                                       const graphene_rect_t *rect,
                                       float                  thickness)
{
  const GdkRGBA white = { 1.0, 1.0, 1.0, 1.0 };
  GskRoundedRect outline;

  gsk_rounded_rect_init_from_rect(&outline, rect, 0.0f);
  gtk_snapshot_append_border(snapshot,
                             &outline,
                             (float[4]) { thickness, thickness, thickness, thickness },
                             (GdkRGBA[4]) { white, white, white, white });
}

static void
waytator_capture_view_snapshot(GtkWidget   *widget,
                               GtkSnapshot *snapshot)
{
  WaytatorCaptureView *self = WAYTATOR_CAPTURE_VIEW(widget);
  const GdkRGBA dim = { 0.0, 0.0, 0.0, 0.45 };
  const float width = gtk_widget_get_width(widget);
  const float height = gtk_widget_get_height(widget);
  const graphene_rect_t bounds = GRAPHENE_RECT_INIT(0.0f, 0.0f, width, height);
  graphene_rect_t selection;

  gtk_snapshot_append_texture(snapshot, self->texture, &bounds);

  if (self->overlay->mode == WAYTATOR_CAPTURE_SCREEN) {
    if (self->hovered)
      waytator_capture_view_snapshot_outline(snapshot, &bounds, 3.0f);
    else
      gtk_snapshot_append_color(snapshot, &dim, &bounds);
    return;
  }

  if (!waytator_capture_view_get_selection(self, &selection)) {
    gtk_snapshot_append_color(snapshot, &dim, &bounds);
    return;
  }

  //everything but the selection
  gtk_snapshot_append_color(snapshot,
                            &dim,
                            &GRAPHENE_RECT_INIT(0.0f, 0.0f, width, selection.origin.y));
  gtk_snapshot_append_color(snapshot,
                            &dim,
                            &GRAPHENE_RECT_INIT(0.0f,
                                                selection.origin.y + selection.size.height,
                                                width,
                                                height - selection.origin.y - selection.size.height));
  gtk_snapshot_append_color(snapshot,
                            &dim,
                            &GRAPHENE_RECT_INIT(0.0f, selection.origin.y, selection.origin.x, selection.size.height));
  gtk_snapshot_append_color(snapshot,
                            &dim,
                            &GRAPHENE_RECT_INIT(selection.origin.x + selection.size.width,
                                                selection.origin.y,
                                                width - selection.origin.x - selection.size.width,
                                                selection.size.height));

  //drawn just outside, so the line never covers what is being captured
  waytator_capture_view_snapshot_outline(snapshot,
                                         &GRAPHENE_RECT_INIT(selection.origin.x - 1.0f,
                                                             selection.origin.y - 1.0f,
                                                             selection.size.width + 2.0f,
                                                             selection.size.height + 2.0f),
                                         1.0f);
  waytator_capture_view_snapshot_size_label(self, snapshot, &selection, width, height);
}

static void
waytator_capture_view_set_selection_end(WaytatorCaptureView *self,
                                        double               x,
                                        double               y)
{
  self->end_x = CLAMP(x, 0.0, gtk_widget_get_width(GTK_WIDGET(self)));
  self->end_y = CLAMP(y, 0.0, gtk_widget_get_height(GTK_WIDGET(self)));
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

static void
waytator_capture_view_drag_begin(GtkGestureDrag      *gesture,
                                 double               x,
                                 double               y,
                                 WaytatorCaptureView *self)
{
  (void) gesture;

  if (self->overlay->mode != WAYTATOR_CAPTURE_AREA)
    return;

  self->selecting = TRUE;
  self->start_x = CLAMP(x, 0.0, gtk_widget_get_width(GTK_WIDGET(self)));
  self->start_y = CLAMP(y, 0.0, gtk_widget_get_height(GTK_WIDGET(self)));
  waytator_capture_view_set_selection_end(self, x, y);
  //the pill would otherwise sit on top of what is being selected
  waytator_capture_overlay_set_pills_visible(self->overlay, FALSE);
}

static void
waytator_capture_view_drag_update(GtkGestureDrag      *gesture,
                                  double               offset_x,
                                  double               offset_y,
                                  WaytatorCaptureView *self)
{
  double start_x;
  double start_y;

  if (!self->selecting || !gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y))
    return;

  waytator_capture_view_set_selection_end(self, start_x + offset_x, start_y + offset_y);
}

static void
waytator_capture_view_drag_end(GtkGestureDrag      *gesture,
                               double               offset_x,
                               double               offset_y,
                               WaytatorCaptureView *self)
{
  g_autoptr(GdkTexture) texture = NULL;
  graphene_rect_t selection;
  int x;
  int y;
  int width;
  int height;

  if (self->overlay->mode == WAYTATOR_CAPTURE_SCREEN) {
    waytator_capture_overlay_finish(self->overlay, self->texture);
    return;
  }

  if (!self->selecting)
    return;

  waytator_capture_view_drag_update(gesture, offset_x, offset_y, self);
  waytator_capture_view_get_selection(self, &selection);
  waytator_capture_view_selection_to_pixels(self, &selection, &x, &y, &width, &height);

  if (selection.size.width < WAYTATOR_CAPTURE_MIN_SELECTION
      || selection.size.height < WAYTATOR_CAPTURE_MIN_SELECTION
      || width <= 0
      || height <= 0) {
    self->selecting = FALSE;
    waytator_capture_overlay_set_pills_visible(self->overlay, TRUE);
    gtk_widget_queue_draw(GTK_WIDGET(self));
    return;
  }

  texture = waytator_capture_texture_new(self->output->surface, x, y, width, height);
  waytator_capture_overlay_finish(self->overlay, texture);
}

static void
waytator_capture_view_drag_cancelled(GtkGesture          *gesture,
                                     GdkEventSequence    *sequence,
                                     WaytatorCaptureView *self)
{
  (void) gesture;
  (void) sequence;

  if (!self->selecting)
    return;

  self->selecting = FALSE;
  waytator_capture_overlay_set_pills_visible(self->overlay, TRUE);
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

static void
waytator_capture_view_secondary_pressed(GtkGestureClick     *gesture,
                                        int                  n_press,
                                        double               x,
                                        double               y,
                                        WaytatorCaptureView *self)
{
  (void) gesture;
  (void) n_press;
  (void) x;
  (void) y;

  waytator_capture_overlay_finish(self->overlay, NULL);
}

static void
waytator_capture_view_pointer_entered(GtkEventControllerMotion *controller,
                                      double                    x,
                                      double                    y,
                                      WaytatorCaptureView      *self)
{
  (void) controller;
  (void) x;
  (void) y;

  self->hovered = TRUE;
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

static void
waytator_capture_view_pointer_left(GtkEventControllerMotion *controller,
                                   WaytatorCaptureView      *self)
{
  (void) controller;

  self->hovered = FALSE;
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

static void
waytator_capture_view_dispose(GObject *object)
{
  WaytatorCaptureView *self = WAYTATOR_CAPTURE_VIEW(object);

  g_clear_object(&self->texture);

  G_OBJECT_CLASS(waytator_capture_view_parent_class)->dispose(object);
}

static void
waytator_capture_view_class_init(WaytatorCaptureViewClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = waytator_capture_view_dispose;
  widget_class->snapshot = waytator_capture_view_snapshot;
}

static void
waytator_capture_view_init(WaytatorCaptureView *self)
{
  GtkGesture *drag = gtk_gesture_drag_new();
  GtkGesture *secondary = gtk_gesture_click_new();
  GtkEventController *motion = gtk_event_controller_motion_new();

  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
  g_signal_connect(drag, "drag-begin", G_CALLBACK(waytator_capture_view_drag_begin), self);
  g_signal_connect(drag, "drag-update", G_CALLBACK(waytator_capture_view_drag_update), self);
  g_signal_connect(drag, "drag-end", G_CALLBACK(waytator_capture_view_drag_end), self);
  g_signal_connect(drag, "cancel", G_CALLBACK(waytator_capture_view_drag_cancelled), self);
  gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(drag));

  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(secondary), GDK_BUTTON_SECONDARY);
  g_signal_connect(secondary, "pressed", G_CALLBACK(waytator_capture_view_secondary_pressed), self);
  gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(secondary));

  g_signal_connect(motion, "enter", G_CALLBACK(waytator_capture_view_pointer_entered), self);
  g_signal_connect(motion, "leave", G_CALLBACK(waytator_capture_view_pointer_left), self);
  gtk_widget_add_controller(GTK_WIDGET(self), motion);
}

static void
waytator_capture_overlay_mode_toggled(GtkToggleButton     *button,
                                      WaytatorCaptureView *view)
{
  if (view->overlay->updating || !gtk_toggle_button_get_active(button))
    return;

  waytator_capture_overlay_set_mode(view->overlay,
                                    button == view->area_button
                                      ? WAYTATOR_CAPTURE_AREA
                                      : WAYTATOR_CAPTURE_SCREEN);
}

static GtkToggleButton *
waytator_capture_overlay_new_mode_button(WaytatorCaptureView *view,
                                         const char          *label)
{
  GtkWidget *button = gtk_toggle_button_new_with_label(label);

  //keys belong to the overlay, the only one it listens for is escape
  gtk_widget_set_focusable(button, FALSE);
  gtk_widget_add_css_class(button, "flat");
  g_signal_connect(button, "toggled", G_CALLBACK(waytator_capture_overlay_mode_toggled), view);
  gtk_box_append(GTK_BOX(view->pill), button);
  return GTK_TOGGLE_BUTTON(button);
}

static void
waytator_capture_overlay_append_separator(WaytatorCaptureView *view)
{
  GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);

  gtk_widget_set_margin_start(separator, 4);
  gtk_widget_set_margin_end(separator, 4);
  gtk_box_append(GTK_BOX(view->pill), separator);
}

static void
waytator_capture_overlay_build_pill(WaytatorCaptureView *view)
{
  view->pill = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_add_css_class(view->pill, "capture-pill");
  gtk_widget_set_halign(view->pill, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(view->pill, GTK_ALIGN_END);
  gtk_widget_set_margin_bottom(view->pill, 32);

  view->area_button = waytator_capture_overlay_new_mode_button(view, "Area");
  view->screen_button = waytator_capture_overlay_new_mode_button(view, "Screen");
  gtk_toggle_button_set_group(view->screen_button, view->area_button);
  waytator_capture_overlay_append_separator(view);

  if (view->overlay->retake != NULL) {
    GtkWidget *hide_button = gtk_check_button_new_with_label("Hide waytator");

    gtk_check_button_set_active(GTK_CHECK_BUTTON(hide_button), view->overlay->hide_waytator);
    gtk_widget_set_focusable(hide_button, FALSE);
    g_signal_connect(hide_button, "toggled", G_CALLBACK(waytator_capture_overlay_hide_toggled), view->overlay);
    gtk_box_append(GTK_BOX(view->pill), hide_button);
    waytator_capture_overlay_append_separator(view);
  }

  view->hint_label = GTK_LABEL(gtk_label_new(NULL));
  gtk_widget_add_css_class(GTK_WIDGET(view->hint_label), "capture-hint");
  gtk_box_append(GTK_BOX(view->pill), GTK_WIDGET(view->hint_label));
}

static gboolean
waytator_capture_overlay_key_pressed(GtkEventControllerKey  *controller,
                                     guint                   keyval,
                                     guint                   keycode,
                                     GdkModifierType         state,
                                     WaytatorCaptureOverlay *overlay)
{
  (void) controller;
  (void) keycode;
  (void) state;

  if (keyval != GDK_KEY_Escape)
    return GDK_EVENT_PROPAGATE;

  waytator_capture_overlay_finish(overlay, NULL);
  return GDK_EVENT_STOP;
}

static gboolean
waytator_capture_overlay_close_requested(GtkWindow              *window,
                                         WaytatorCaptureOverlay *overlay)
{
  (void) window;

  waytator_capture_overlay_finish(overlay, NULL);
  return GDK_EVENT_STOP;
}

static GdkMonitor *
waytator_capture_overlay_find_monitor(WaytatorCaptureOutput *output)
{
  GListModel *monitors = gdk_display_get_monitors(gdk_display_get_default());
  GdkMonitor *same_geometry = NULL;

  for (guint i = 0; i < g_list_model_get_n_items(monitors); i++) {
    g_autoptr(GdkMonitor) monitor = g_list_model_get_item(monitors, i);
    GdkRectangle geometry;

    if (output->name != NULL && g_strcmp0(gdk_monitor_get_connector(monitor), output->name) == 0)
      return monitor;

    gdk_monitor_get_geometry(monitor, &geometry);
    if (geometry.x == output->x
        && geometry.y == output->y
        && geometry.width == output->width
        && geometry.height == output->height)
      same_geometry = monitor;
  }

  return same_geometry;
}

static void
waytator_capture_overlay_add_window(WaytatorCaptureOverlay *overlay,
                                    WaytatorCaptureOutput  *output,
                                    GdkMonitor             *monitor)
{
  static const GtkLayerShellEdge edges[] = {
    GTK_LAYER_SHELL_EDGE_LEFT,
    GTK_LAYER_SHELL_EDGE_RIGHT,
    GTK_LAYER_SHELL_EDGE_TOP,
    GTK_LAYER_SHELL_EDGE_BOTTOM,
  };
  GtkWindow *window = GTK_WINDOW(gtk_window_new());
  GtkWidget *stack = gtk_overlay_new();
  GtkEventController *keys = gtk_event_controller_key_new();
  WaytatorCaptureView *view = g_object_new(WAYTATOR_TYPE_CAPTURE_VIEW, NULL);

  view->overlay = overlay;
  view->output = output;
  view->texture = waytator_capture_output_create_texture(output);
  waytator_capture_overlay_build_pill(view);

  gtk_overlay_set_child(GTK_OVERLAY(stack), GTK_WIDGET(view));
  gtk_overlay_add_overlay(GTK_OVERLAY(stack), view->pill);
  gtk_window_set_child(window, stack);
  gtk_window_set_decorated(window, FALSE);
  gtk_widget_add_css_class(GTK_WIDGET(window), "capture-overlay");

  gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
  g_signal_connect(keys, "key-pressed", G_CALLBACK(waytator_capture_overlay_key_pressed), overlay);
  gtk_widget_add_controller(GTK_WIDGET(window), keys);
  g_signal_connect(window, "close-request", G_CALLBACK(waytator_capture_overlay_close_requested), overlay);

  //gnome has no layer shell, a fullscreen window is the closest thing there
  if (gtk_layer_is_supported()) {
    gtk_layer_init_for_window(window);
    gtk_layer_set_namespace(window, "waytator-capture");
    gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_monitor(window, monitor);
    for (guint i = 0; i < G_N_ELEMENTS(edges); i++)
      gtk_layer_set_anchor(window, edges[i], TRUE);
    gtk_layer_set_exclusive_zone(window, -1);
    gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
  } else {
    gtk_window_fullscreen_on_monitor(window, monitor);
  }

  g_ptr_array_add(overlay->windows, window);
  g_ptr_array_add(overlay->views, view);
}

//takes the outputs, and returns FALSE without calling back when none of them is on a known monitor
gboolean
waytator_capture_overlay_show(GPtrArray                           *outputs,
                              WaytatorCaptureMode                  mode,
                              gboolean                             hide_waytator,
                              WaytatorCaptureOverlayRetakeCallback retake,
                              WaytatorCaptureOverlayCallback       callback,
                              gpointer                             user_data)
{
  WaytatorCaptureOverlay *overlay = g_new0(WaytatorCaptureOverlay, 1);

  waytator_window_ensure_css_loaded();

  overlay->outputs = outputs;
  overlay->windows = g_ptr_array_new();
  overlay->views = g_ptr_array_new();
  overlay->pill_style = waytator_capture_overlay_new_pill_style();
  overlay->hide_waytator = hide_waytator;
  overlay->retake = retake;
  overlay->callback = callback;
  overlay->user_data = user_data;

  for (guint i = 0; i < outputs->len; i++) {
    WaytatorCaptureOutput *output = g_ptr_array_index(outputs, i);
    GdkMonitor *monitor = waytator_capture_overlay_find_monitor(output);

    if (monitor != NULL)
      waytator_capture_overlay_add_window(overlay, output, monitor);
  }

  if (overlay->windows->len == 0) {
    waytator_capture_overlay_free(overlay);
    return FALSE;
  }

  waytator_capture_overlay_set_mode(overlay, mode);
  for (guint i = 0; i < overlay->windows->len; i++)
    gtk_window_present(g_ptr_array_index(overlay->windows, i));

  return TRUE;
}
