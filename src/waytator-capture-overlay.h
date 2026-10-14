#pragma once

#include <gtk/gtk.h>

#include "waytator-capture.h"

G_BEGIN_DECLS

//texture is NULL when the selection was cancelled, mode is whichever one was active last
typedef void (*WaytatorCaptureOverlayCallback)(GdkTexture         *texture,
                                               WaytatorCaptureMode mode,
                                               gpointer            user_data);

//the hide box was flipped, which needs a new frame: the overlay is gone and the caller takes one again
typedef void (*WaytatorCaptureOverlayRetakeCallback)(gboolean            hide_waytator,
                                                     WaytatorCaptureMode mode,
                                                     gpointer            user_data);

GdkTexture *waytator_capture_output_create_texture(WaytatorCaptureOutput *output);
WaytatorCaptureOutput *waytator_capture_pick_output(GPtrArray          *outputs,
                                                    WaytatorCaptureMode mode,
                                                    const char         *output_name,
                                                    GError            **error);
//without a retake callback there is no hide box
gboolean waytator_capture_overlay_show(GPtrArray                           *outputs,
                                       WaytatorCaptureMode                  mode,
                                       gboolean                             hide_waytator,
                                       WaytatorCaptureOverlayRetakeCallback retake,
                                       WaytatorCaptureOverlayCallback       callback,
                                       gpointer                             user_data);

G_END_DECLS
