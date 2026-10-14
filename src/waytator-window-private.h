#pragma once

#include "waytator-document.h"
#include "waytator-types.h"
#include "waytator-window.h"

struct _WaytatorWindow {
  AdwApplicationWindow parent_instance;

  GtkStack *canvas_stack;
  GtkScrolledWindow *canvas_scroller;
  GtkWidget *empty_page;
  GtkWidget *canvas_surface;
  GtkPicture *picture;
  GtkDrawingArea *drawing_area;
  GtkGesture *touch_pan_gesture;
  GtkGesture *crop_gesture;
  GtkGesture *draw_gesture;
  GtkGesture *zoom_gesture;
  GtkEventController *touch_legacy_controller;
  GtkFixed *ocr_overlay;
  AdwBottomSheet *ocr_panel_bottom_sheet;
  GtkWidget *ocr_panel;
  GtkWidget *ocr_panel_toggle_container;
  GtkToggleButton *ocr_panel_toggle_button;
  GtkButton *ocr_panel_close_button;
  GtkStack *ocr_panel_stack;
  GtkStackSwitcher *ocr_panel_tabs;
  GtkScrolledWindow *ocr_selected_page;
  GtkScrolledWindow *ocr_all_page;
  GtkTextView *ocr_selected_text_view;
  GtkTextView *ocr_all_text_view;
  GtkWidget *start_actions;
  GtkWidget *start_window_controls_pill;
  GtkWindowControls *start_window_controls;
  GListModel *start_window_controls_children;
  GtkWidget *open_actions;
  GtkButton *capture_button;
  GtkLabel *capture_area_hint;
  GtkLabel *open_hint;
  GtkWidget *file_group;
  GtkMenuButton *file_button;
  GtkLabel *file_label;
  GtkLabel *zoom_label;
  GtkWidget *tool_group;
  GtkToggleButton *pan_tool_button;
  GtkToggleButton *crop_tool_button;
  GtkToggleButton *brush_tool_button;
  GtkToggleButton *highlighter_tool_button;
  GtkToggleButton *eraser_tool_button;
  GtkMenuButton *shapes_tool_button;
  GtkPopover *shapes_popover;
  GtkToggleButton *rectangle_tool_button;
  GtkToggleButton *circle_tool_button;
  GtkToggleButton *line_tool_button;
  GtkToggleButton *arrow_tool_button;
  GtkToggleButton *ocr_tool_button;
  GtkToggleButton *text_tool_button;
  GtkToggleButton *blur_tool_button;
  GtkButton *rotate_counter_clockwise_button;
  GtkButton *flip_horizontal_button;
  GtkButton *flip_vertical_button;
  GtkWidget *history_actions;
  GtkButton *undo_button;
  GtkButton *redo_button;
  GtkWidget *document_actions;
  GtkMenuButton *save_button;
  GtkButton *save_as_button;
  GMenu *save_menu;
  char *save_menu_key;
  GtkStack *save_icon_stack;
  GtkImage *save_default_icon;
  GtkImage *save_working_icon;
  GtkImage *save_success_icon;
  GtkButton *copy_button;
  GtkButton *copy_keep_open_button;
  GtkMenuButton *app_menu_button;
  GtkWindowControls *end_window_controls;
  GListModel *end_window_controls_children;
  GtkStack *copy_icon_stack;
  GtkImage *copy_default_icon;
  GtkImage *copy_success_icon;
  GtkWidget *zoom_group;
  GtkButton *fit_zoom_button;
  GtkWidget *settings_group;
  GtkColorDialogButton *color_button;
  GtkColorDialogButton *fill_color_button;
  GtkRevealer *size_drawer;
  GtkScale *width_scale;
  GtkWidget *size_presets;
  GtkWidget *size_preset_buttons[WAYTATOR_N_SIZE_PRESETS];
  GtkWidget *size_readout;
  GtkWidget *size_value_box;
  GtkStack *size_value_stack;
  GtkLabel *size_value_label;
  GtkText *size_value_entry;
  GtkToggleButton *size_drawer_button;
  GtkWidget *size_focus_before;
  GtkSpinButton *text_size_spin;
  GtkDropDown *blur_type_dropdown;
  GtkCssProvider *window_css_provider;
  GtkCssProvider *widget_css_provider;
  char *shortcut_accels[WAYTATOR_N_SHORTCUTS];
  char *shortcut_overrides[WAYTATOR_N_SHORTCUTS];
  AdwActionRow *shortcut_rows[WAYTATOR_N_SHORTCUTS];
  AdwPreferencesGroup *palette_group;
  GtkWidget *palette_add_button;
  GtkWidget *tool_shortcut_hints[WAYTATOR_TOOL_BLUR + 1];

  GFile *current_file;
  char *source_name;
  GdkTexture *texture;
  cairo_surface_t *image_surface;
  WaytatorDocument *document;
  WaytatorStroke *current_stroke;
  double zoom;
  gboolean fit_mode;
  WaytatorTool active_tool;
  gboolean drawing;
  guint copy_feedback_timeout_id;
  guint save_spinner_timeout_id;
  guint save_feedback_timeout_id;
  gint64 save_feedback_started_at;
  gboolean copy_in_progress;
  gboolean auto_copy_pending;
  gboolean close_after_copy;
  gboolean close_copy_pending;
  gboolean save_in_progress;
  gboolean never_saved;
  gboolean from_capture;
  GFile *adopt_file;
  char *screenshot_save_dir;
  gboolean screenshot_copy;
  WaytatorCaptureMode capture_mode;
  gboolean capture_hide_waytator;
  gboolean capture_in_progress;
  gboolean capture_window_was_visible;
  gboolean capture_from_ui;
  gboolean capture_retake;
  GPtrArray *capture_hidden_windows;
  char *capture_output_name;
  guint capture_delay_id;
  WaytatorAfterSave after_save;
  char *saved_path;
  guint ocr_generation;
  gboolean ocr_running;
  double last_draw_x;
  double last_draw_y;
  double drag_start_hvalue;
  double drag_start_vvalue;
  double pinch_start_zoom;
  double pinch_anchor_rel_x;
  double pinch_anchor_rel_y;
  double crop_start_x;
  double crop_start_y;
  double crop_end_x;
  double crop_end_y;

  double pointer_x;
  double pointer_y;
  double pointer_widget_x;
  double pointer_widget_y;
  gboolean pointer_in;
  WaytatorWindowBackgroundMode window_background_mode;
  gboolean updating_ui;
  gboolean size_drawer_open;
  gboolean size_editing;
  gboolean size_scrubbing;
  double size_scrub_start;
  gboolean interaction_has_undo_step;
  gboolean blur_commit_warning_accepted;
  gboolean blur_commit_warning_showing;
  GdkEventSequence *active_touch_draw_sequence;
  GdkEventSequence *cancelled_touch_draw_sequence;
  GHashTable *active_touch_sequences;
  GHashTable *touch_tap_points;
  gboolean touch_tap_candidate;
  gboolean touch_tap_cancelled;
  guint touch_tap_max_points;
  gint64 touch_tap_started_at;
  gboolean close_on_copy;
  int close_on_copy_override;
  GdkModifierType angle_snap_modifiers;
  gboolean allow_highlighter_overlap;
  gboolean floating_controls_blur;
  gboolean auto_copy_latest_change;
  double window_background_opacity;
  double floating_controls_opacity;
  GdkRGBA default_primary_color;
  GdkRGBA default_highlighter_color;
  GdkRGBA default_fill_color;
  GdkRGBA palette_colors[WAYTATOR_MAX_PALETTE_COLORS];
  guint n_palette_colors;
  double tool_widths[WAYTATOR_TOOL_BLUR + 1];
  GdkRGBA tool_colors[WAYTATOR_TOOL_BLUR + 1];
  GdkRGBA tool_fill_colors[WAYTATOR_TOOL_BLUR + 1];
  int blur_type;
  WaytatorEraserStyle eraser_style;
  GPtrArray *ocr_lines;
  WaytatorOcrLine *selected_ocr_line;
  char *ocr_all_text;
};

GPtrArray *waytator_window_strokes(WaytatorWindow *self);
void waytator_window_update_ocr_overlay(WaytatorWindow *self);
void waytator_window_maybe_start_ocr(WaytatorWindow *self);
void waytator_window_update_ocr_panel(WaytatorWindow *self);
void waytator_window_reset_save_button(WaytatorWindow *self);
void waytator_window_update_history_buttons(WaytatorWindow *self);
void waytator_window_clear_history(WaytatorWindow *self);
void waytator_window_record_undo_step(WaytatorWindow *self);
void waytator_window_maybe_auto_copy_latest_change(WaytatorWindow *self);
void waytator_window_trigger_copy(WaytatorWindow *self);
gboolean waytator_window_shortcut_is_active(WaytatorWindow  *self,
                                            WaytatorShortcut shortcut);
const char *waytator_window_shortcut_accel(WaytatorWindow  *self,
                                           WaytatorShortcut shortcut);
void waytator_window_activate_shortcut(WaytatorWindow  *self,
                                       WaytatorShortcut shortcut);
const char *waytator_window_shortcut_title(WaytatorShortcut shortcut);
int waytator_window_shortcut_conflict(WaytatorWindow  *self,
                                      WaytatorShortcut shortcut,
                                      gboolean         with_overrides);
void waytator_window_select_tool(WaytatorWindow *self,
                                 WaytatorTool    tool);
void waytator_window_step_tool_size(WaytatorWindow *self,
                                    int             direction);
void waytator_window_apply_palette_color(WaytatorWindow *self,
                                         guint           index);
void waytator_window_setup_tool_shortcut_hints(WaytatorWindow *self);
void waytator_window_update_tool_shortcut_hints(WaytatorWindow *self);
void waytator_window_restore_strokes(WaytatorWindow *self,
                                     GPtrArray      *strokes);

gboolean waytator_window_get_display_rect(WaytatorWindow *self,
                                          double          widget_width,
                                          double          widget_height,
                                          double         *display_x,
                                          double         *display_y,
                                          double         *display_width,
                                          double         *display_height);
gboolean waytator_window_get_image_point(WaytatorWindow *self,
                                         double          widget_x,
                                         double          widget_y,
                                         gboolean        clamp_to_image,
                                         double         *image_x,
                                         double         *image_y);
void waytator_window_set_adjustment_clamped(GtkAdjustment *adjustment,
                                            double         value);
gboolean waytator_window_get_pointer_viewport_position(WaytatorWindow *self,
                                                       double         *x,
                                                       double         *y);
void waytator_window_get_viewport_center(WaytatorWindow *self,
                                         double         *x,
                                         double         *y);
double waytator_window_get_effective_zoom(WaytatorWindow *self);
void waytator_window_apply_zoom_mode(WaytatorWindow *self);
void waytator_window_update_zoom_label(WaytatorWindow *self);
void waytator_window_update_picture_size(WaytatorWindow *self);
void waytator_window_refresh_document_state(WaytatorWindow *self);
cairo_surface_t *waytator_window_render_composited_surface(WaytatorWindow *self);
void waytator_window_apply_crop(WaytatorWindow *self,
                                int             left,
                                int             top,
                                int             width,
                                int             height);
void waytator_window_set_zoom_at(WaytatorWindow *self,
                                 double          zoom,
                                 double          viewport_x,
                                 double          viewport_y);
void waytator_window_update_size_controls(WaytatorWindow *self);
void waytator_window_save_preferences(WaytatorWindow *self);
void waytator_window_update_tool_ui(WaytatorWindow *self);
void waytator_window_queue_fit_zoom(WaytatorWindow *self);
void waytator_window_sync_state(WaytatorWindow *self);
void waytator_window_install_history_actions(GtkWidgetClass *widget_class);
void waytator_window_setup_tool_signals(WaytatorWindow *self);
void waytator_window_install_canvas_actions(GtkWidgetClass *widget_class);
void waytator_window_setup_controllers(WaytatorWindow *self);
void waytator_window_setup_signals(WaytatorWindow *self);
void waytator_window_drawing_area_draw(GtkDrawingArea *area,
                                       cairo_t        *cr,
                                       int             width,
                                       int             height,
                                       gpointer        user_data);
