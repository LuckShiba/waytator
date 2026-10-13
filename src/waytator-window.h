#pragma once

#include <adwaita.h>

#include "waytator-types.h"

G_BEGIN_DECLS

#define WAYTATOR_TYPE_WINDOW (waytator_window_get_type())

G_DECLARE_FINAL_TYPE(WaytatorWindow, waytator_window, WAYTATOR, WINDOW, AdwApplicationWindow)

WaytatorWindow *waytator_window_new(AdwApplication *app);
gboolean waytator_window_open_file(WaytatorWindow *self,
                                   GFile          *file,
                                   GError        **error);
gboolean waytator_window_open_bytes(WaytatorWindow *self,
                                    GBytes         *bytes,
                                    const char     *display_name,
                                    GError        **error);
void waytator_window_open_capture(WaytatorWindow *self,
                                  GdkTexture     *texture,
                                  const char     *display_name);
gboolean waytator_window_is_empty(WaytatorWindow *self);
WaytatorWindow *waytator_window_for_new_image(GtkApplication *app);
void waytator_window_start_capture(WaytatorWindow     *self,
                                   WaytatorCaptureMode mode,
                                   const char         *output_name);
gboolean waytator_window_shortcut_is_valid(const char *shortcut);
void waytator_window_clear_overrides(WaytatorWindow *self);
void waytator_window_set_close_on_copy_override(WaytatorWindow *self,
                                                gboolean        close_on_copy);
gboolean waytator_window_set_shortcut_override(WaytatorWindow *self,
                                               const char     *name,
                                               const char     *shortcut);

G_END_DECLS
