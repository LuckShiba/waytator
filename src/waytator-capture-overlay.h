#pragma once

#include <gtk/gtk.h>

#include "waytator-capture.h"

G_BEGIN_DECLS

//texture is NULL when the selection was cancelled, mode is whichever one was active last
typedef void (*WaytatorCaptureOverlayCallback)(GdkTexture         *texture,
                                               WaytatorCaptureMode mode,
                                               gpointer            user_data);

//how long the compositor gets to take a window off the screen before a frame is grabbed
#define WAYTATOR_CAPTURE_SETTLE_MS 300

//the hide box was flipped or a delay was picked, both need a new frame: the overlay is gone and the caller
//takes one again, after waiting delay_seconds
typedef void (*WaytatorCaptureOverlayRetakeCallback)(gboolean            hide_waytator,
                                                     WaytatorCaptureMode mode,
                                                     guint               delay_seconds,
                                                     gpointer            user_data);

GdkTexture *waytator_capture_output_create_texture(WaytatorCaptureOutput *output);
WaytatorCaptureOutput *waytator_capture_pick_output(GPtrArray          *outputs,
                                                    WaytatorCaptureMode mode,
                                                    const char         *output_name,
                                                    GError            **error);
//offer_hide puts the hide box on the overlay, hide_waytator is what it starts out as
gboolean waytator_capture_overlay_show(GPtrArray                           *outputs,
                                       WaytatorCaptureMode                  mode,
                                       gboolean                             offer_hide,
                                       gboolean                             hide_waytator,
                                       WaytatorCaptureOverlayRetakeCallback retake,
                                       WaytatorCaptureOverlayCallback       callback,
                                       gpointer                             user_data);

G_END_DECLS
