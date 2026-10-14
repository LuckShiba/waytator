#pragma once

#include <glib.h>

G_BEGIN_DECLS

//the preferences that are read without a window too, by --notify and the capture overlay
typedef struct {
  //empty when saving is switched off
  char *screenshot_save_dir;
  gboolean screenshot_copy;
  gboolean floating_controls_blur;
  double floating_controls_opacity;
} WaytatorSettings;

char *waytator_settings_path(void);
void waytator_settings_init(WaytatorSettings *settings);
void waytator_settings_clear(WaytatorSettings *settings);
void waytator_settings_read(WaytatorSettings *settings,
                            GKeyFile         *key_file);
void waytator_settings_write(const WaytatorSettings *settings,
                             GKeyFile               *key_file);
void waytator_settings_load(WaytatorSettings *settings);

G_END_DECLS
