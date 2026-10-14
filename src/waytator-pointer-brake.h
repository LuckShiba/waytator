#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

//holds the real pointer still and reports how hard it is pushed, so a widget can move a cursor of its own at
//whatever speed it likes: a wayland client cannot move or slow down the real one
typedef struct _WaytatorPointerBrake WaytatorPointerBrake;

//dx and dy are in the widget's logical pixels, only sent while the pointer is held
typedef void (*WaytatorPointerBrakeMoved)(double   dx,
                                          double   dy,
                                          gpointer user_data);
//the compositor let go of the pointer on its own, the brake still has to be released
typedef void (*WaytatorPointerBrakeLost)(gpointer user_data);

//NULL when the compositor has no way to hold the pointer
WaytatorPointerBrake *waytator_pointer_brake_new(GtkWidget                *widget,
                                                 WaytatorPointerBrakeMoved moved,
                                                 WaytatorPointerBrakeLost  lost,
                                                 gpointer                  user_data);
//x and y are where the widget's own cursor ended up, the compositor is asked to put the real one there
void waytator_pointer_brake_release(WaytatorPointerBrake *brake,
                                    double                x,
                                    double                y);

G_END_DECLS
