#pragma once

#include "waytator-types.h"

//one frozen frame of one monitor
typedef struct {
  char *name;               //connector name, e.g. "DP-2", may be NULL
  cairo_surface_t *surface; //upright and opaque image surface, in device pixels
  int x;                    //logical geometry in compositor global space
  int y;
  int width;
  int height;
} WaytatorCaptureOutput;

void waytator_capture_output_free(WaytatorCaptureOutput *output);

//call from the main thread, the portal backend needs GDK to tell the monitors apart
void waytator_capture_outputs_async(GCancellable       *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer            user_data);
//a GPtrArray of WaytatorCaptureOutput, one per monitor, free with g_ptr_array_unref.
//fails with G_IO_ERROR_PERMISSION_DENIED when the portal needs a permission waytator_capture_allow_async gives
GPtrArray *waytator_capture_outputs_finish(GAsyncResult *result,
                                           GError      **error);
//true on GNOME, where a capture is taken in GNOME's own screenshot UI rather than waytator's overlay
gboolean waytator_capture_uses_gnome_ui(void);
//opens GNOME's screenshot UI, the result is what was taken there and the file GNOME saved it to.
//fails with G_IO_ERROR_CANCELLED when the UI was closed without taking anything
void waytator_capture_gnome_ui_async(GAsyncReadyCallback callback,
                                     gpointer            user_data);
GdkTexture *waytator_capture_gnome_ui_finish(GAsyncResult *result,
                                             GFile       **file,
                                             GError      **error);
//allows waytator to take screenshots through the portal from now on, for when someone asked for that
void waytator_capture_allow_async(GAsyncReadyCallback callback,
                                  gpointer            user_data);
gboolean waytator_capture_allow_finish(GAsyncResult *result,
                                       GError      **error);
