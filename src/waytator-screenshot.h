#pragma once

#include <gtk/gtk.h>

#include "waytator-types.h"

G_BEGIN_DECLS

//file is set when the screenshot is already on disk, otherwise it only lives in texture
typedef void (*WaytatorScreenshotEditFunc)(GdkTexture *texture,
                                           GFile      *file,
                                           const char *name,
                                           gpointer    user_data);

char *waytator_screenshot_default_save_dir(void);
char *waytator_screenshot_display_path(const char *path);
char *waytator_screenshot_make_name(void);
char *waytator_screenshot_describe_save_error(const char   *save_dir,
                                              const GError *error);
GFile *waytator_screenshot_reserve_file(const char *save_dir,
                                        const char *name,
                                        GError    **error);
void waytator_screenshot_copy_texture(GdkTexture *texture);
void waytator_screenshot_load_settings(char    **save_dir,
                                       gboolean *copy);
void waytator_screenshot_capture_background(GApplication              *app,
                                            WaytatorCaptureMode        mode,
                                            const char                *output_name,
                                            WaytatorScreenshotEditFunc edit,
                                            gpointer                   user_data);

G_END_DECLS
