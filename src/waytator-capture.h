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
  double scale;             //surface pixels per logical unit
} WaytatorCaptureOutput;

void waytator_capture_output_free(WaytatorCaptureOutput *output);

//call from the main thread, the portal backend needs GDK to tell the monitors apart
void waytator_capture_outputs_async(GCancellable       *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer            user_data);
//a GPtrArray of WaytatorCaptureOutput, one per monitor, free with g_ptr_array_unref
GPtrArray *waytator_capture_outputs_finish(GAsyncResult *result,
                                           GError      **error);
