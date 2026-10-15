#include "waytator-window-private.h"

#include <math.h>

#define WAYTATOR_MIN_ZOOM 0.10
#define WAYTATOR_MAX_ZOOM 8.00

static void
waytator_window_get_viewport_size(WaytatorWindow *self,
                                  double         *width,
                                  double         *height)
{
  GtkAdjustment *hadjustment = gtk_scrolled_window_get_hadjustment(self->canvas_scroller);
  GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment(self->canvas_scroller);

  *width = gtk_adjustment_get_page_size(hadjustment);
  *height = gtk_adjustment_get_page_size(vadjustment);

  if (*width <= 0)
    *width = gtk_widget_get_width(GTK_WIDGET(self->canvas_scroller));

  if (*height <= 0)
    *height = gtk_widget_get_height(GTK_WIDGET(self->canvas_scroller));
}

static double
waytator_window_get_fit_zoom(WaytatorWindow *self)
{
  double viewport_width;
  double viewport_height;
  const int texture_width = self->texture != NULL
                          ? gdk_paintable_get_intrinsic_width(GDK_PAINTABLE(self->texture))
                          : 0;
  const int texture_height = self->texture != NULL
                           ? gdk_paintable_get_intrinsic_height(GDK_PAINTABLE(self->texture))
                           : 0;

  if (texture_width <= 0 || texture_height <= 0)
    return 1.0;

  //the floating controls are see-through, the image goes right to the window's edges under them
  waytator_window_get_viewport_size(self, &viewport_width, &viewport_height);
  if (viewport_width <= 0 || viewport_height <= 0)
    return 1.0;

  //fit fills the window, small images grow to it as well
  return CLAMP(MIN(viewport_width / texture_width, viewport_height / texture_height),
               WAYTATOR_MIN_ZOOM,
               WAYTATOR_MAX_ZOOM);
}

void
waytator_window_update_zoom_label(WaytatorWindow *self)
{
  const gboolean fitted = self->texture != NULL && self->fit_mode;
  g_autofree char *label = self->texture != NULL
                         ? g_strdup_printf("%.0f%%", waytator_window_get_effective_zoom(self) * 100.0)
                         : g_strdup("--");

  //both Fit buttons, the one in the pill and the one in its drawer, show whether fit is on
  for (guint i = 0; i < 2; i++) {
    GtkWidget *fit = i == 0 ? GTK_WIDGET(self->fit_zoom_button) : self->zoom_drawer_fit_button;

    if (fitted)
      gtk_widget_add_css_class(fit, "selected-tool");
    else
      gtk_widget_remove_css_class(fit, "selected-tool");
  }

  //the pill, the row in the main menu and the readout over the image all show it
  gtk_label_set_text(self->zoom_label, label);
  if (self->zoom_menu_label != NULL)
    gtk_label_set_text(self->zoom_menu_label, label);
  gtk_label_set_text(self->zoom_osd, label);
}

static gboolean
waytator_window_fit_zoom_idle(gpointer user_data)
{
  WaytatorWindow *self = WAYTATOR_WINDOW(user_data);
  double viewport_width;
  double viewport_height;

  if (self->texture == NULL)
    goto done;

  waytator_window_get_viewport_size(self, &viewport_width, &viewport_height);
  if (viewport_width <= 0 || viewport_height <= 0)
    return G_SOURCE_CONTINUE;

  self->fit_mode = TRUE;
  self->zoom = waytator_window_get_fit_zoom(self);
  waytator_window_apply_zoom_mode(self);
  waytator_window_update_zoom_label(self);

done:
  g_object_unref(self);
  return G_SOURCE_REMOVE;
}

//an image opens at its own size, or smaller when the window cannot hold it. that zoom settles once the window
//first has a size, and is then left alone like any other until Fit is turned on
static gboolean
waytator_window_opening_zoom_idle(gpointer user_data)
{
  WaytatorWindow *self = WAYTATOR_WINDOW(user_data);
  double viewport_width;
  double viewport_height;

  if (self->texture == NULL || !self->opening_zoom)
    goto done;

  waytator_window_get_viewport_size(self, &viewport_width, &viewport_height);
  if (viewport_width <= 0 || viewport_height <= 0)
    return G_SOURCE_CONTINUE;

  waytator_window_settle_opening_zoom(self);

done:
  g_object_unref(self);
  return G_SOURCE_REMOVE;
}

void
waytator_window_queue_opening_zoom(WaytatorWindow *self)
{
  self->opening_zoom = TRUE;
  g_idle_add(waytator_window_opening_zoom_idle, g_object_ref(self));
}

//Fit off keeps the zoom it was at, the window can then be resized around it
void
waytator_window_toggle_fit(WaytatorWindow *self)
{
  if (self->texture == NULL)
    return;

  if (!self->fit_mode) {
    self->opening_zoom = FALSE;
    waytator_window_queue_fit_zoom(self);
    return;
  }

  self->zoom = waytator_window_get_fit_zoom(self);
  self->fit_mode = FALSE;
  waytator_window_apply_zoom_mode(self);
  waytator_window_update_zoom_label(self);
}

static void
waytator_window_canvas_size_changed(GObject    *object,
                                    GParamSpec *pspec,
                                    gpointer    user_data)
{
  WaytatorWindow *self = WAYTATOR_WINDOW(user_data);

  (void) object;
  (void) pspec;

  if (self->texture == NULL || (!self->fit_mode && !self->opening_zoom))
    return;

  self->zoom = waytator_window_get_fit_zoom(self);
  waytator_window_apply_zoom_mode(self);
  waytator_window_update_zoom_label(self);
  waytator_window_update_ocr_overlay(self);
}

static void
waytator_window_viewport_changed(GObject    *object,
                                 GParamSpec *pspec,
                                 gpointer    user_data)
{
  waytator_window_canvas_size_changed(object, pspec, user_data);
}

static void
waytator_window_drawing_area_resized(GtkDrawingArea *drawing_area,
                                     int             width,
                                     int             height,
                                     gpointer        user_data)
{
  (void) drawing_area;
  (void) width;
  (void) height;

  waytator_window_update_ocr_overlay(WAYTATOR_WINDOW(user_data));
}

gboolean
waytator_window_get_display_rect(WaytatorWindow *self,
                                 double          widget_width,
                                 double          widget_height,
                                 double         *display_x,
                                 double         *display_y,
                                 double         *display_width,
                                 double         *display_height)
{
  const int image_width = self->texture != NULL
                        ? gdk_paintable_get_intrinsic_width(GDK_PAINTABLE(self->texture))
                        : 0;
  const int image_height = self->texture != NULL
                         ? gdk_paintable_get_intrinsic_height(GDK_PAINTABLE(self->texture))
                         : 0;
  const double zoom = waytator_window_get_effective_zoom(self);

  if (widget_width <= 0 || widget_height <= 0 || image_width <= 0 || image_height <= 0)
    return FALSE;

  *display_width = MIN(widget_width, image_width * zoom);
  *display_height = MIN(widget_height, image_height * zoom);
  *display_x = MAX(0.0, (widget_width - *display_width) / 2.0);
  *display_y = MAX(0.0, (widget_height - *display_height) / 2.0);

  return TRUE;
}

gboolean
waytator_window_get_image_point(WaytatorWindow *self,
                                double          widget_x,
                                double          widget_y,
                                gboolean        clamp_to_image,
                                double         *image_x,
                                double         *image_y)
{
  const double widget_width = gtk_widget_get_width(GTK_WIDGET(self->drawing_area));
  const double widget_height = gtk_widget_get_height(GTK_WIDGET(self->drawing_area));
  const int image_width = self->texture != NULL
                        ? gdk_paintable_get_intrinsic_width(GDK_PAINTABLE(self->texture))
                        : 0;
  const int image_height = self->texture != NULL
                         ? gdk_paintable_get_intrinsic_height(GDK_PAINTABLE(self->texture))
                         : 0;
  double display_x;
  double display_y;
  double display_width;
  double display_height;

  if (!waytator_window_get_display_rect(self,
                                        widget_width,
                                        widget_height,
                                        &display_x,
                                        &display_y,
                                        &display_width,
                                        &display_height)
      || image_width <= 0
      || image_height <= 0)
    return FALSE;

  if (!clamp_to_image
      && (widget_x < display_x
          || widget_x > display_x + display_width
          || widget_y < display_y
          || widget_y > display_y + display_height))
    return FALSE;

  widget_x = CLAMP(widget_x, display_x, display_x + display_width);
  widget_y = CLAMP(widget_y, display_y, display_y + display_height);

  *image_x = (widget_x - display_x) * image_width / display_width;
  *image_y = (widget_y - display_y) * image_height / display_height;
  return TRUE;
}

void
waytator_window_set_adjustment_clamped(GtkAdjustment *adjustment,
                                       double         value)
{
  const double lower = gtk_adjustment_get_lower(adjustment);
  const double upper = MAX(lower, gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_page_size(adjustment));

  gtk_adjustment_set_value(adjustment, CLAMP(value, lower, upper));
}

gboolean
waytator_window_get_pointer_viewport_position(WaytatorWindow *self,
                                              double         *x,
                                              double         *y)
{
  const graphene_point_t point = GRAPHENE_POINT_INIT(self->pointer_widget_x, self->pointer_widget_y);
  graphene_point_t viewport_point;

  if (!self->pointer_in
      || !gtk_widget_compute_point(GTK_WIDGET(self->drawing_area),
                                   GTK_WIDGET(self->canvas_scroller),
                                   &point,
                                   &viewport_point))
    return FALSE;

  *x = viewport_point.x;
  *y = viewport_point.y;
  return TRUE;
}

void
waytator_window_get_viewport_center(WaytatorWindow *self,
                                    double         *x,
                                    double         *y)
{
  double width;
  double height;

  waytator_window_get_viewport_size(self, &width, &height);
  *x = width / 2.0;
  *y = height / 2.0;
}

double
waytator_window_get_effective_zoom(WaytatorWindow *self)
{
  if (self->texture == NULL)
    return 1.0;

  if (self->fit_mode)
    return waytator_window_get_fit_zoom(self);
  if (self->opening_zoom)
    return MIN(1.0, waytator_window_get_fit_zoom(self));
  return self->zoom;
}

//the zoom an image opened at becomes a zoom like any other
void
waytator_window_settle_opening_zoom(WaytatorWindow *self)
{
  if (!self->opening_zoom)
    return;

  self->zoom = waytator_window_get_effective_zoom(self);
  self->opening_zoom = FALSE;
}

void
waytator_window_apply_zoom_mode(WaytatorWindow *self)
{
  if (self->texture == NULL) {
    gtk_picture_set_can_shrink(self->picture, TRUE);
    gtk_widget_set_size_request(self->canvas_surface, -1, -1);
    waytator_window_update_ocr_overlay(self);
    return;
  }

  if (self->fit_mode) {
    GtkAdjustment *hadjustment = gtk_scrolled_window_get_hadjustment(self->canvas_scroller);
    GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment(self->canvas_scroller);

    waytator_window_set_adjustment_clamped(hadjustment, 0.0);
    waytator_window_set_adjustment_clamped(vadjustment, 0.0);
  }

  gtk_picture_set_can_shrink(self->picture, TRUE);
  waytator_window_update_picture_size(self);
  waytator_window_update_ocr_overlay(self);
}

void
waytator_window_update_picture_size(WaytatorWindow *self)
{
  if (self->texture == NULL) {
    gtk_widget_set_size_request(self->canvas_surface, -1, -1);
    waytator_window_update_ocr_overlay(self);
    return;
  }

  const int width = gdk_paintable_get_intrinsic_width(GDK_PAINTABLE(self->texture));
  const int height = gdk_paintable_get_intrinsic_height(GDK_PAINTABLE(self->texture));
  const double zoom = waytator_window_get_effective_zoom(self);
  const int req_width = MAX(1, (int) lround(width * zoom));
  const int req_height = MAX(1, (int) lround(height * zoom));

  gtk_widget_set_size_request(self->canvas_surface, req_width, req_height);
  waytator_window_update_ocr_overlay(self);
}

//scrolls so that the point at rel_x, rel_y across the image, at the zoom just set, lands under viewport_x,
//viewport_y. the viewport only learns the image's new size at the next layout, so the scroll range is worked out
//ahead of it the way it will lay the image out, centred while it is smaller than the viewport
void
waytator_window_scroll_image_point_to(WaytatorWindow *self,
                                      double          rel_x,
                                      double          rel_y,
                                      double          viewport_x,
                                      double          viewport_y)
{
  GtkAdjustment *hadjustment = gtk_scrolled_window_get_hadjustment(self->canvas_scroller);
  GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment(self->canvas_scroller);
  const double zoom = waytator_window_get_effective_zoom(self);
  const double display_width = MAX(1, lround(gdk_paintable_get_intrinsic_width(GDK_PAINTABLE(self->texture)) * zoom));
  const double display_height = MAX(1, lround(gdk_paintable_get_intrinsic_height(GDK_PAINTABLE(self->texture)) * zoom));
  double viewport_width;
  double viewport_height;
  double content_width;
  double content_height;

  waytator_window_get_viewport_size(self, &viewport_width, &viewport_height);
  content_width = MAX(viewport_width, display_width);
  content_height = MAX(viewport_height, display_height);

  gtk_adjustment_set_upper(hadjustment, content_width);
  gtk_adjustment_set_upper(vadjustment, content_height);
  waytator_window_set_adjustment_clamped(hadjustment,
                                         (content_width - display_width) / 2.0 + rel_x * display_width - viewport_x);
  waytator_window_set_adjustment_clamped(vadjustment,
                                         (content_height - display_height) / 2.0 + rel_y * display_height
                                         - viewport_y);
}

void
waytator_window_set_zoom_at(WaytatorWindow *self,
                            double          zoom,
                            double          viewport_x,
                            double          viewport_y)
{
  const double fit_zoom = waytator_window_get_fit_zoom(self);
  const graphene_point_t viewport_point = GRAPHENE_POINT_INIT(viewport_x, viewport_y);
  graphene_point_t surface_point;
  double widget_width;
  double widget_height;
  double display_x;
  double display_y;
  double display_width;
  double display_height;
  double rel_x = 0.5;
  double rel_y = 0.5;

  if (self->texture == NULL)
    return;

  zoom = CLAMP(zoom, WAYTATOR_MIN_ZOOM, WAYTATOR_MAX_ZOOM);
  waytator_window_settle_opening_zoom(self);

  if (self->fit_mode && fabs(zoom - fit_zoom) < 0.0001)
    return;

  if (!self->fit_mode && fabs(zoom - self->zoom) < 0.0001)
    return;

  //the point of the image under the anchor, wherever the image sits in the viewport
  widget_width = gtk_widget_get_width(self->canvas_surface);
  widget_height = gtk_widget_get_height(self->canvas_surface);
  if (gtk_widget_compute_point(GTK_WIDGET(self->canvas_scroller), self->canvas_surface, &viewport_point, &surface_point)
      && waytator_window_get_display_rect(self, widget_width, widget_height,
                                          &display_x, &display_y,
                                          &display_width, &display_height)) {
    rel_x = (surface_point.x - display_x) / display_width;
    rel_y = (surface_point.y - display_y) / display_height;
  }

  self->fit_mode = FALSE;
  self->zoom = zoom;
  waytator_window_apply_zoom_mode(self);
  waytator_window_update_zoom_label(self);
  waytator_window_scroll_image_point_to(self, rel_x, rel_y, viewport_x, viewport_y);
  waytator_window_flash_zoom_readout(self);
}

void
waytator_window_queue_fit_zoom(WaytatorWindow *self)
{
  g_idle_add(waytator_window_fit_zoom_idle, g_object_ref(self));
}

void
waytator_window_sync_state(WaytatorWindow *self)
{
  const gboolean has_image = self->texture != NULL;
  const gboolean has_current_file = self->current_file != NULL;
  g_autoptr(GFile) parent = has_current_file ? g_file_get_parent(self->current_file) : NULL;

  gtk_stack_set_visible_child(self->canvas_stack,
                              has_image ? GTK_WIDGET(self->canvas_scroller) : self->empty_page);
  gtk_widget_set_visible(self->tool_group, has_image);
  gtk_widget_set_visible(self->history_actions, has_image);
  gtk_widget_set_visible(self->document_actions, has_image);
  gtk_widget_set_visible(self->settings_group, has_image);
  gtk_widget_set_sensitive(self->tool_group, has_image);
  gtk_widget_set_sensitive(self->settings_group, has_image);
  gtk_widget_set_sensitive(GTK_WIDGET(self->file_button), has_current_file);
  gtk_widget_set_sensitive(GTK_WIDGET(self->copy_button), has_image);
  gtk_widget_set_sensitive(GTK_WIDGET(self->copy_keep_open_button), has_image);
  gtk_widget_set_sensitive(self->zoom_group, has_image);
  gtk_widget_action_set_enabled(GTK_WIDGET(self), "win.open-current-file", has_current_file);
  gtk_widget_action_set_enabled(GTK_WIDGET(self), "win.open-containing-folder", parent != NULL);
  waytator_window_reset_save_button(self);
  waytator_window_update_history_buttons(self);
  waytator_window_update_ocr_panel(self);

  if (!has_image) {
    self->zoom = 1.0;
    self->fit_mode = TRUE;
    self->drawing = FALSE;
    gtk_label_set_text(self->file_label, "no image loaded");
    waytator_window_update_zoom_label(self);
    waytator_window_update_tool_ui(self);
    waytator_window_update_chrome_layout(self);
    return;
  }

  waytator_window_update_zoom_label(self);
  waytator_window_update_tool_ui(self);
  waytator_window_update_chrome_layout(self);
}

void
waytator_window_setup_signals(WaytatorWindow *self)
{
  GtkAdjustment *hadjustment = gtk_scrolled_window_get_hadjustment(self->canvas_scroller);
  GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment(self->canvas_scroller);

  g_signal_connect(self->canvas_scroller, "notify::width", G_CALLBACK(waytator_window_canvas_size_changed), self);
  g_signal_connect(self->canvas_scroller, "notify::height", G_CALLBACK(waytator_window_canvas_size_changed), self);
  g_signal_connect(hadjustment, "notify::page-size", G_CALLBACK(waytator_window_viewport_changed), self);
  g_signal_connect(vadjustment, "notify::page-size", G_CALLBACK(waytator_window_viewport_changed), self);
  g_signal_connect(self->drawing_area, "resize", G_CALLBACK(waytator_window_drawing_area_resized), self);
  waytator_window_setup_tool_signals(self);
}
