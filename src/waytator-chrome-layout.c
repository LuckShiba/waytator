#include "waytator-window-private.h"

//the floating controls fit any window from a phone up: they measure what they need and give up room in a
//fixed order, the least needed while editing first. the tools are the only thing that moves, between a dock
//at the bottom with their options right above, and a rail on the left when the window is short

#define WAYTATOR_CHROME_MARGIN 16
#define WAYTATOR_CHROME_MARGIN_TIGHT 8
#define WAYTATOR_CHROME_GAP 10
#define WAYTATOR_CHROME_GAP_TIGHT 6
#define WAYTATOR_CHROME_TOOL_GAP 4
#define WAYTATOR_CHROME_TOOL_GAP_TIGHT 2
//between the controls and the fitted image
#define WAYTATOR_CHROME_CLEARANCE 8
//below this the dock and its options would take more than 30% of the height, so the tools go to the side
#define WAYTATOR_RAIL_BELOW_HEIGHT 630
//a title narrower than this says too little to be worth the space
#define WAYTATOR_TITLE_MIN_WIDTH 180
//below this, Open gives way if that makes room for the title
#define WAYTATOR_TITLE_COMFY_WIDTH 220

static int
waytator_chrome_width(GtkWidget *widget)
{
  int natural = 0;

  if (!gtk_widget_get_visible(widget))
    return 0;

  gtk_widget_measure(widget, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &natural, NULL, NULL);
  return natural;
}

static int
waytator_chrome_height(GtkWidget *widget)
{
  int natural = 0;

  if (!gtk_widget_get_visible(widget))
    return 0;

  gtk_widget_measure(widget, GTK_ORIENTATION_VERTICAL, -1, NULL, &natural, NULL, NULL);
  return natural;
}

static void
waytator_chrome_place_tools(WaytatorWindow *self,
                            gboolean        rail)
{
  GtkWidget *tools = self->tool_group;
  GtkWidget *target = rail ? self->tool_rail : self->dock_box;

  if (gtk_widget_get_parent(tools) != target) {
    g_object_ref(tools);
    gtk_box_remove(GTK_BOX(gtk_widget_get_parent(tools)), tools);
    gtk_box_append(GTK_BOX(target), tools);
    g_object_unref(tools);
  }

  gtk_orientable_set_orientation(GTK_ORIENTABLE(tools), rail ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_halign(tools, rail ? GTK_ALIGN_START : GTK_ALIGN_CENTER);
  //from the rail the shapes open to the side, from the dock upwards
  gtk_popover_set_position(self->shapes_popover, rail ? GTK_POS_RIGHT : GTK_POS_TOP);
  gtk_widget_set_visible(self->tool_rail, rail && self->texture != NULL);
}

static void
waytator_chrome_apply(WaytatorWindow            *self,
                      const WaytatorChromeFolds *folds)
{
  const gboolean has_image = self->texture != NULL;
  const int margin = folds->tight ? WAYTATOR_CHROME_MARGIN_TIGHT : WAYTATOR_CHROME_MARGIN;
  const int gap = folds->tight ? WAYTATOR_CHROME_GAP_TIGHT : WAYTATOR_CHROME_GAP;
  const gboolean full_zoom = folds->zoom == WAYTATOR_ZOOM_FULL;

  self->folds = *folds;
  waytator_chrome_place_tools(self, folds->rail);

  gtk_widget_set_visible(self->open_actions, has_image && folds->capture);
  gtk_widget_set_visible(self->open_label_button, folds->open == WAYTATOR_OPEN_LABEL);
  gtk_widget_set_visible(self->open_icon_button, folds->open == WAYTATOR_OPEN_ICON);
  gtk_widget_set_visible(self->open_separator, folds->open != WAYTATOR_OPEN_FOLDED);
  gtk_widget_set_visible(self->file_group, has_image && folds->title);
  gtk_widget_set_visible(self->zoom_group, has_image && folds->zoom != WAYTATOR_ZOOM_MENU);
  gtk_widget_set_visible(self->zoom_out_button, full_zoom);
  gtk_widget_set_visible(GTK_WIDGET(self->zoom_label), full_zoom);
  gtk_widget_set_visible(self->zoom_in_button, full_zoom);
  gtk_widget_set_visible(GTK_WIDGET(self->fit_zoom_button), full_zoom);
  gtk_widget_set_visible(GTK_WIDGET(self->zoom_chip), folds->zoom == WAYTATOR_ZOOM_CHIP);
  waytator_window_update_save_menu(self);
  gtk_widget_set_visible(GTK_WIDGET(self->copy_keep_open_button), waytator_window_closes_on_copy(self) && folds->keep_open);

  gtk_widget_set_margin_start(self->top_bar, margin);
  gtk_widget_set_margin_end(self->top_bar, margin);
  gtk_widget_set_margin_top(self->top_bar, margin);
  gtk_widget_set_margin_start(self->bottom_bar, margin);
  gtk_widget_set_margin_end(self->bottom_bar, margin);
  gtk_widget_set_margin_bottom(self->bottom_bar, margin);
  gtk_widget_set_margin_start(self->tool_rail, margin);
  gtk_widget_set_margin_start(self->ocr_panel, margin);
  gtk_widget_set_margin_end(self->ocr_panel, margin);
  gtk_widget_set_margin_bottom(self->ocr_panel, margin);
  gtk_box_set_spacing(GTK_BOX(self->start_actions), gap);
  gtk_box_set_spacing(GTK_BOX(self->end_actions), gap);
  gtk_box_set_spacing(GTK_BOX(self->tool_group), folds->tight ? WAYTATOR_CHROME_TOOL_GAP_TIGHT : WAYTATOR_CHROME_TOOL_GAP);
}

//the widest options any tool has, measured once: color, fill, sizes and the size readout together
static int
waytator_chrome_options_max_width(WaytatorWindow *self)
{
  GtkWidget *shown[] = { GTK_WIDGET(self->color_button), GTK_WIDGET(self->fill_color_button), self->size_presets, self->size_readout };
  GtkWidget *hidden[] = { GTK_WIDGET(self->text_size_spin), GTK_WIDGET(self->blur_type_dropdown), self->crop_actions };
  const gboolean was_visible = gtk_widget_get_visible(self->settings_group);

  if (self->options_max_width > 0)
    return self->options_max_width;

  gtk_widget_set_visible(self->settings_group, TRUE);
  for (guint i = 0; i < G_N_ELEMENTS(shown); i++)
    gtk_widget_set_visible(shown[i], TRUE);
  for (guint i = 0; i < G_N_ELEMENTS(hidden); i++)
    gtk_widget_set_visible(hidden[i], FALSE);
  self->options_max_width = waytator_chrome_width(self->settings_group);
  gtk_widget_set_visible(self->settings_group, was_visible);
  waytator_window_update_tool_ui(self);
  return self->options_max_width;
}

static int
waytator_chrome_title_room(WaytatorWindow *self,
                           int             width)
{
  const int start = waytator_chrome_width(self->start_actions);
  const int end = waytator_chrome_width(self->end_actions);

  //the title is centred, so the wider side decides
  return width - 2 * MAX(start, end) - 2 * WAYTATOR_CHROME_GAP - 2 * WAYTATOR_CHROME_MARGIN;
}

static gboolean
waytator_chrome_bar_fits(WaytatorWindow *self,
                         int             width)
{
  const int margin = self->folds.tight ? WAYTATOR_CHROME_MARGIN_TIGHT : WAYTATOR_CHROME_MARGIN;
  const int gap = self->folds.tight ? WAYTATOR_CHROME_GAP_TIGHT : WAYTATOR_CHROME_GAP;

  return waytator_chrome_width(self->start_actions) + waytator_chrome_width(self->end_actions) + gap + 2 * margin <= width;
}

static gboolean
waytator_chrome_layout_main_menu(WaytatorWindow *self);

static void
waytator_chrome_update_insets(WaytatorWindow *self)
{
  const int margin = self->folds.tight ? WAYTATOR_CHROME_MARGIN_TIGHT : WAYTATOR_CHROME_MARGIN;
  const int top = margin + waytator_chrome_height(self->top_bar) + WAYTATOR_CHROME_CLEARANCE;
  const int bottom = margin + waytator_chrome_height(self->bottom_bar) + WAYTATOR_CHROME_CLEARANCE;
  const int start = self->folds.rail ? margin + waytator_chrome_width(self->tool_rail) + WAYTATOR_CHROME_CLEARANCE : 0;

  if (top == self->inset_top && bottom == self->inset_bottom && start == self->inset_start)
    return;

  self->inset_top = top;
  self->inset_bottom = bottom;
  self->inset_start = start;
  if (self->texture != NULL && self->fit_mode) {
    waytator_window_apply_zoom_mode(self);
    waytator_window_update_zoom_label(self);
  }
}

void
waytator_window_update_chrome_layout(WaytatorWindow *self)
{
  const int width = self->chrome_width;
  WaytatorChromeFolds folds = {
    .rail = self->chrome_height > 0 && self->chrome_height < WAYTATOR_RAIL_BELOW_HEIGHT,
    .open = WAYTATOR_OPEN_LABEL,
    .capture = TRUE,
    .title = TRUE,
    .zoom = WAYTATOR_ZOOM_FULL,
    .save = TRUE,
    .keep_open = TRUE,
  };
  int middle;
  int zoom_full;
  int zoom_chip;
  int room;

  g_clear_handle_id(&self->chrome_layout_id, g_source_remove);

  if (width <= 0 || self->texture == NULL) {
    waytator_chrome_apply(self, &folds);
    waytator_chrome_layout_main_menu(self);
    return;
  }

  waytator_chrome_apply(self, &folds);

  //zoom keeps its full controls while they fit beside the tool dock, or beside the options when the tools are
  //in the rail
  middle = folds.rail ? waytator_chrome_options_max_width(self) : waytator_chrome_width(self->tool_group);
  zoom_full = waytator_chrome_width(self->zoom_group);
  folds.zoom = WAYTATOR_ZOOM_CHIP;
  waytator_chrome_apply(self, &folds);
  zoom_chip = waytator_chrome_width(self->zoom_group);
  if (middle + 2 * (zoom_full + WAYTATOR_CHROME_GAP) + 2 * WAYTATOR_CHROME_MARGIN <= width)
    folds.zoom = WAYTATOR_ZOOM_FULL;
  else if (middle + 2 * (zoom_chip + WAYTATOR_CHROME_GAP) + 2 * WAYTATOR_CHROME_MARGIN > width)
    folds.zoom = WAYTATOR_ZOOM_MENU;
  waytator_chrome_apply(self, &folds);

  //Open gives way for the title, but only while that actually frees room for it
  room = waytator_chrome_title_room(self, width);
  for (WaytatorOpenFold to = WAYTATOR_OPEN_ICON; to <= WAYTATOR_OPEN_FOLDED && room < WAYTATOR_TITLE_COMFY_WIDTH; to++) {
    const WaytatorOpenFold before = folds.open;
    int folded_room;

    folds.open = to;
    waytator_chrome_apply(self, &folds);
    folded_room = waytator_chrome_title_room(self, width);
    if (folded_room <= room) {
      folds.open = before;
      waytator_chrome_apply(self, &folds);
      break;
    }
    room = folded_room;
  }
  if (room < WAYTATOR_TITLE_MIN_WIDTH) {
    folds.title = FALSE;
    waytator_chrome_apply(self, &folds);
  }

  //the dock only ever needs the tighter spacing
  if (!folds.rail && waytator_chrome_width(self->tool_group) + 2 * WAYTATOR_CHROME_MARGIN > width) {
    folds.tight = TRUE;
    waytator_chrome_apply(self, &folds);
  }

  //without a title the bar still has to fit: starting a new capture goes first, then Save, then the second copy
  //button, before either of which Open and the spacing give way
  for (int step = 0; step < 6 && !waytator_chrome_bar_fits(self, width); step++) {
    switch (step) {
    case 0:
      if (folds.open == WAYTATOR_OPEN_LABEL)
        folds.open = WAYTATOR_OPEN_ICON;
      break;
    case 1:
      folds.open = WAYTATOR_OPEN_FOLDED;
      break;
    case 2:
      folds.tight = TRUE;
      break;
    case 3:
      folds.capture = FALSE;
      break;
    case 4:
      folds.save = FALSE;
      break;
    default:
      folds.keep_open = FALSE;
      break;
    }
    waytator_chrome_apply(self, &folds);
  }

  waytator_chrome_layout_main_menu(self);
  waytator_chrome_update_insets(self);
}

static gboolean
waytator_chrome_layout_idle(gpointer user_data)
{
  WaytatorWindow *self = WAYTATOR_WINDOW(user_data);

  self->chrome_layout_id = 0;
  waytator_window_update_chrome_layout(self);
  return G_SOURCE_REMOVE;
}

void
waytator_window_queue_chrome_layout(WaytatorWindow *self)
{
  if (self->chrome_layout_id != 0 || self->disposed || self->top_bar == NULL)
    return;

  //never while sizes are being handed out, it changes what is being sized
  self->chrome_layout_id = g_idle_add_full(G_PRIORITY_HIGH_IDLE, waytator_chrome_layout_idle, self, NULL);
}

static void
waytator_chrome_watcher_resized(GtkDrawingArea *area,
                                int             width,
                                int             height,
                                gpointer        user_data)
{
  WaytatorWindow *self = WAYTATOR_WINDOW(user_data);

  (void) area;

  if (width == self->chrome_width && height == self->chrome_height)
    return;

  self->chrome_width = width;
  self->chrome_height = height;
  waytator_window_queue_chrome_layout(self);
}

//what no longer has a button of its own ends up in the main menu, next to Preferences and About
static gboolean
waytator_chrome_layout_main_menu(WaytatorWindow *self)
{
  const WaytatorChromeFolds *folds = &self->folds;
  const gboolean closes_on_copy = waytator_window_closes_on_copy(self);
  const gboolean file_section = !folds->title || !folds->save || (closes_on_copy && !folds->keep_open);
  const char *file_name = gtk_label_get_text(self->file_label);
  g_autofree char *key = g_strdup_printf("%d%d%d%d%d%d%d:%s", folds->title, folds->save, folds->keep_open, folds->capture,
                                         folds->zoom, folds->open, closes_on_copy, file_name);
  g_autoptr(GMenu) common = g_menu_new();

  if (g_strcmp0(key, self->app_menu_key) == 0)
    return FALSE;

  g_free(self->app_menu_key);
  self->app_menu_key = g_steal_pointer(&key);
  g_menu_remove_all(self->app_menu);

  if (file_section) {
    g_autoptr(GMenu) section = g_menu_new();
    g_autofree char *heading = g_utf8_strlen(file_name, -1) > 40
                             ? g_strdup_printf("%.*s…%s",
                                               (int) (g_utf8_offset_to_pointer(file_name, 18) - file_name),
                                               file_name,
                                               g_utf8_offset_to_pointer(file_name, g_utf8_strlen(file_name, -1) - 18))
                             : g_strdup(file_name);

    if (!folds->save) {
      g_menu_append(section, "Save", "win.save-image");
      g_menu_append(section, "Save as…", "win.save-as");
    }
    if (!folds->keep_open && closes_on_copy)
      g_menu_append(section, "Copy without closing", "win.copy-keep-open");
    g_menu_append(section, "Open file", "win.open-current-file");
    g_menu_append(section, "Open containing folder", "win.open-containing-folder");
    g_menu_append_section(self->app_menu, heading, G_MENU_MODEL(section));
  }

  if (!folds->capture) {
    g_autoptr(GMenu) section = g_menu_new();

    g_menu_append(section, "Capture area", "win.capture-area");
    g_menu_append(section, "Open image…", "win.open");
    g_menu_append_section(self->app_menu, NULL, G_MENU_MODEL(section));
  }

  if (folds->zoom == WAYTATOR_ZOOM_MENU) {
    g_autoptr(GMenu) section = g_menu_new();
    g_autoptr(GMenuItem) row = g_menu_item_new(NULL, NULL);

    g_menu_item_set_attribute(row, "custom", "s", "zoom-row");
    g_menu_append_item(section, row);
    g_menu_append_section(self->app_menu, NULL, G_MENU_MODEL(section));
  }

  g_menu_append(common, "Preferences", "win.preferences");
  g_menu_append(common, "About Waytator", "win.about");
  g_menu_append_section(self->app_menu, NULL, G_MENU_MODEL(common));

  //a custom row only lands in a menu that already has its place, so it moves in again each time
  if (folds->zoom == WAYTATOR_ZOOM_MENU) {
    GtkPopoverMenu *popover = GTK_POPOVER_MENU(gtk_menu_button_get_popover(self->app_menu_button));

    if (gtk_widget_get_parent(self->zoom_menu_row) != NULL)
      gtk_popover_menu_remove_child(popover, self->zoom_menu_row);
    gtk_popover_menu_add_child(popover, self->zoom_menu_row, "zoom-row");
  }

  //Open goes into the camera button's menu once its own button is gone
  if (g_menu_model_get_n_items(G_MENU_MODEL(self->capture_menu)) > 1)
    g_menu_remove(self->capture_menu, 0);
  if (folds->open == WAYTATOR_OPEN_FOLDED) {
    g_autoptr(GMenu) section = g_menu_new();

    g_menu_append(section, "Open image…", "win.open");
    g_menu_prepend_section(self->capture_menu, NULL, G_MENU_MODEL(section));
  }
  return TRUE;
}

static GtkWidget *
waytator_chrome_zoom_row(GtkLabel **label)
{
  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  GtkWidget *zoom_out = gtk_button_new_from_icon_name("zoom-out-symbolic");
  GtkWidget *zoom_in = gtk_button_new_from_icon_name("zoom-in-symbolic");
  GtkWidget *fit = gtk_button_new_with_label("Fit");
  GtkWidget *value = gtk_label_new("100%");

  gtk_widget_add_css_class(row, "zoom-row");
  gtk_actionable_set_action_name(GTK_ACTIONABLE(zoom_out), "win.zoom-out");
  gtk_actionable_set_action_name(GTK_ACTIONABLE(zoom_in), "win.zoom-in");
  gtk_actionable_set_action_name(GTK_ACTIONABLE(fit), "win.zoom-fit");
  gtk_widget_set_tooltip_text(zoom_out, "Zoom out");
  gtk_widget_set_tooltip_text(zoom_in, "Zoom in");
  gtk_widget_set_tooltip_text(fit, "Fit image to window");
  gtk_widget_add_css_class(value, "zoom-label");
  gtk_label_set_width_chars(GTK_LABEL(value), 5);
  for (GtkWidget *button = zoom_out; button != NULL; button = button == zoom_out ? zoom_in : button == zoom_in ? fit : NULL)
    gtk_widget_add_css_class(button, "flat");
  gtk_box_append(GTK_BOX(row), zoom_out);
  gtk_box_append(GTK_BOX(row), value);
  gtk_box_append(GTK_BOX(row), zoom_in);
  gtk_box_append(GTK_BOX(row), fit);
  *label = GTK_LABEL(value);
  return row;
}

static gboolean
waytator_chrome_hide_zoom_readout(gpointer user_data)
{
  WaytatorWindow *self = WAYTATOR_WINDOW(user_data);

  self->zoom_osd_timeout_id = 0;
  gtk_widget_set_visible(GTK_WIDGET(self->zoom_osd), FALSE);
  return G_SOURCE_REMOVE;
}

//with zoom folded into the menu, the level shows over the image for a moment while it changes
void
waytator_window_flash_zoom_readout(WaytatorWindow *self)
{
  if (self->folds.zoom != WAYTATOR_ZOOM_MENU || self->texture == NULL)
    return;

  gtk_widget_set_visible(GTK_WIDGET(self->zoom_osd), TRUE);
  g_clear_handle_id(&self->zoom_osd_timeout_id, g_source_remove);
  self->zoom_osd_timeout_id = g_timeout_add(900, waytator_chrome_hide_zoom_readout, self);
}

void
waytator_window_setup_chrome_layout(WaytatorWindow *self)
{
  GtkWidget *popover = gtk_popover_new();
  GtkWidget *app_popover;

  //the chip keeps the zoom controls in a popover
  gtk_popover_set_child(GTK_POPOVER(popover), waytator_chrome_zoom_row(&self->zoom_popover_label));
  gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
  gtk_menu_button_set_popover(self->zoom_chip, popover);

  self->app_menu = g_menu_new();
  app_popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(self->app_menu));
  gtk_popover_set_has_arrow(GTK_POPOVER(app_popover), FALSE);
  gtk_popover_set_position(GTK_POPOVER(app_popover), GTK_POS_BOTTOM);
  self->zoom_menu_row = g_object_ref_sink(waytator_chrome_zoom_row(&self->zoom_menu_label));
  gtk_menu_button_set_popover(self->app_menu_button, app_popover);

  g_signal_connect(self->chrome_watcher, "resize", G_CALLBACK(waytator_chrome_watcher_resized), self);
  waytator_window_update_zoom_label(self);
  waytator_window_update_chrome_layout(self);
}
