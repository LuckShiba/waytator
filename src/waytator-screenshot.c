#include "waytator-screenshot.h"

#include "waytator-capture-overlay.h"
#include "waytator-export.h"
#include "waytator-settings.h"
#include "waytator-window.h"

#include <string.h>

#define WAYTATOR_NOTIFICATIONS_NAME "org.freedesktop.Notifications"
#define WAYTATOR_NOTIFICATIONS_PATH "/org/freedesktop/Notifications"
//nobody answers a notification this late, and without a limit an ignored one would keep us around forever
#define WAYTATOR_SCREENSHOT_WAIT_SECONDS 600

//a capture taken in the background, from the moment it is asked for until its notification is gone
typedef struct {
  GApplication *app;
  WaytatorCaptureMode mode;
  char *output_name;
  //set when the overlay asked for a new frame, which brings the overlay back rather than finish the capture
  gboolean retake;
  guint delay_id;
  //whether waytator's windows leave the screen for this capture, and the ones that did
  gboolean hide;
  GPtrArray *hidden;
  WaytatorScreenshotEditFunc edit;
  gpointer user_data;
  GDestroyNotify user_data_free;
  GdkTexture *texture;
  char *name;
  WaytatorSettings settings;
  GFile *file;
  char *save_error;
  gboolean copied;
  GDBusConnection *connection;
  guint32 notification_id;
  //the notification on screen asks for the permission the portal needs, rather than show a capture
  gboolean asking_permission;
  guint action_subscription;
  guint closed_subscription;
  guint wait_timeout_id;
} WaytatorScreenshot;

//the home directory as a tilde, the way people write it
char *
waytator_screenshot_display_path(const char *path)
{
  const char *home = g_get_home_dir();

  if (g_str_has_prefix(path, home) && (path[strlen(home)] == '/' || path[strlen(home)] == '\0'))
    return g_strconcat("~", path + strlen(home), NULL);

  return g_strdup(path);
}

char *
waytator_screenshot_make_name(void)
{
  g_autoptr(GDateTime) now = g_date_time_new_now_local();

  return g_date_time_format(now, "Screenshot from %Y-%m-%d %H-%M-%S.png");
}

static void
waytator_screenshot_free(WaytatorScreenshot *shot)
{
  if (shot->action_subscription != 0)
    g_dbus_connection_signal_unsubscribe(shot->connection, shot->action_subscription);
  if (shot->closed_subscription != 0)
    g_dbus_connection_signal_unsubscribe(shot->connection, shot->closed_subscription);
  if (shot->wait_timeout_id != 0)
    g_source_remove(shot->wait_timeout_id);
  if (shot->delay_id != 0)
    g_source_remove(shot->delay_id);

  //a capture that was cancelled or failed brings the windows back here
  waytator_window_restore_all(shot->hidden);
  g_ptr_array_unref(shot->hidden);
  g_clear_object(&shot->connection);
  g_clear_object(&shot->texture);
  g_clear_object(&shot->file);
  g_free(shot->output_name);
  g_free(shot->name);
  waytator_settings_clear(&shot->settings);
  g_free(shot->save_error);
  if (shot->user_data_free != NULL)
    shot->user_data_free(shot->user_data);
  //the application stayed alive for this, with no window of its own
  g_application_release(shot->app);
  g_object_unref(shot->app);
  g_free(shot);
}

static void
waytator_screenshot_edit(WaytatorScreenshot *shot)
{
  shot->edit(shot->texture, shot->file, shot->name, shot->save_error != NULL, shot->user_data);
}

//the reason in words someone who is not looking at a terminal can act on
char *
waytator_screenshot_describe_save_error(const char   *save_dir,
                                        const GError *error)
{
  g_autofree char *folder = waytator_screenshot_display_path(save_dir);

  if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED)
      || g_error_matches(error, G_IO_ERROR, G_IO_ERROR_READ_ONLY))
    return g_strdup_printf("Waytator is not allowed to write to %s.", folder);

  if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE))
    return g_strdup_printf("There is no space left to save to %s.", folder);

  if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY))
    return g_strdup_printf("%s is not a folder.", folder);

  return g_strdup_printf("Could not save to %s: %s.", folder, error->message);
}

//the name is only as precise as a second, two captures inside one get a counter
static char *
waytator_screenshot_candidate_name(const char *name,
                                   int         attempt)
{
  const char *extension = strrchr(name, '.');
  const int stem_length = extension != NULL ? (int) (extension - name) : (int) strlen(name);

  if (attempt == 0)
    return g_strdup(name);

  return g_strdup_printf("%.*s-%d%s", stem_length, name, attempt, extension != NULL ? extension : "");
}

static GFile *
waytator_screenshot_ensure_folder(const char *save_dir,
                                  GError    **error)
{
  g_autoptr(GFile) folder = g_file_new_for_path(save_dir);
  g_autoptr(GError) folder_error = NULL;

  if (!g_file_make_directory_with_parents(folder, NULL, &folder_error)
      && !g_error_matches(folder_error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
    g_propagate_error(error, g_steal_pointer(&folder_error));
    return NULL;
  }

  return g_steal_pointer(&folder);
}

//creating the file is what claims the name, so two screenshots taken at once never share one
static GFileOutputStream *
waytator_screenshot_create_file(const char *save_dir,
                                const char *name,
                                GFile     **file,
                                GError    **error)
{
  g_autoptr(GFile) folder = waytator_screenshot_ensure_folder(save_dir, error);

  if (folder == NULL)
    return NULL;

  for (int attempt = 0; attempt < 1000; attempt++) {
    g_autofree char *candidate = waytator_screenshot_candidate_name(name, attempt);
    g_autoptr(GFile) child = g_file_get_child(folder, candidate);
    g_autoptr(GError) create_error = NULL;
    GFileOutputStream *stream = g_file_create(child, G_FILE_CREATE_NONE, NULL, &create_error);

    if (stream != NULL) {
      *file = g_steal_pointer(&child);
      return stream;
    }

    if (!g_error_matches(create_error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
      g_propagate_error(error, g_steal_pointer(&create_error));
      return NULL;
    }
  }

  g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS, "too many screenshots share the name %s", name);
  return NULL;
}

//an empty file under a free name, for a save that writes the file itself
GFile *
waytator_screenshot_reserve_file(const char *save_dir,
                                 const char *name,
                                 GError    **error)
{
  g_autoptr(GFile) file = NULL;
  g_autoptr(GFileOutputStream) stream = waytator_screenshot_create_file(save_dir, name, &file, error);

  if (stream == NULL)
    return NULL;

  if (!g_output_stream_close(G_OUTPUT_STREAM(stream), NULL, error)) {
    g_file_delete(file, NULL, NULL);
    return NULL;
  }

  return g_steal_pointer(&file);
}

static GFile *
waytator_screenshot_write_file(const char *save_dir,
                               const char *name,
                               GBytes     *png,
                               GError    **error)
{
  g_autoptr(GFile) file = NULL;
  g_autoptr(GFileOutputStream) stream = waytator_screenshot_create_file(save_dir, name, &file, error);

  if (stream == NULL)
    return NULL;

  if (!g_output_stream_write_all(G_OUTPUT_STREAM(stream),
                                 g_bytes_get_data(png, NULL),
                                 g_bytes_get_size(png),
                                 NULL,
                                 NULL,
                                 error)
      || !g_output_stream_close(G_OUTPUT_STREAM(stream), NULL, error)) {
    g_file_delete(file, NULL, NULL);
    return NULL;
  }

  return g_steal_pointer(&file);
}

//encoding a whole screen takes long enough to notice, so this runs off the main thread
static GBytes *
waytator_screenshot_encode_png(GdkTexture *texture)
{
  g_autoptr(GdkTextureDownloader) downloader = gdk_texture_downloader_new(texture);
  g_autoptr(GBytes) pixels = NULL;
  g_autoptr(GdkPixbuf) pixbuf = NULL;
  char *buffer = NULL;
  gsize length = 0;
  gsize stride = 0;

  //a screenshot is opaque, so there is no alpha to keep
  gdk_texture_downloader_set_format(downloader, GDK_MEMORY_R8G8B8);
  pixels = gdk_texture_downloader_download_bytes(downloader, &stride);
  pixbuf = gdk_pixbuf_new_from_bytes(pixels,
                                     GDK_COLORSPACE_RGB,
                                     FALSE,
                                     8,
                                     gdk_texture_get_width(texture),
                                     gdk_texture_get_height(texture),
                                     stride);

  if (!gdk_pixbuf_save_to_buffer(pixbuf, &buffer, &length, "png", NULL, "compression", WAYTATOR_PNG_COMPRESSION, NULL))
    return gdk_texture_save_to_png_bytes(texture);

  return g_bytes_new_take(buffer, length);
}

//a capture on its way to the folder and the clipboard
typedef struct {
  GdkTexture *texture;
  char *name;
  char *save_dir;
  gboolean copy;
  GBytes *png;
  GFile *file;
  char *save_error;
  WaytatorScreenshotStoredFunc done;
  gpointer user_data;
} WaytatorScreenshotStore;

static void
waytator_screenshot_store_free(WaytatorScreenshotStore *store)
{
  g_clear_object(&store->texture);
  g_clear_object(&store->file);
  g_clear_pointer(&store->png, g_bytes_unref);
  g_free(store->name);
  g_free(store->save_dir);
  g_free(store->save_error);
  g_free(store);
}

static void
waytator_screenshot_store_finish(WaytatorScreenshotStore *store,
                                 gboolean                 copied)
{
  store->done(store->file, store->save_error, copied, store->user_data);
  waytator_screenshot_store_free(store);
  g_application_release(g_application_get_default());
}

static void
waytator_screenshot_store_thread(GTask        *task,
                                 gpointer      source_object,
                                 gpointer      task_data,
                                 GCancellable *cancellable)
{
  WaytatorScreenshotStore *store = task_data;
  g_autoptr(GError) error = NULL;

  (void) source_object;
  (void) cancellable;

  store->png = waytator_screenshot_encode_png(store->texture);
  if (*store->save_dir != '\0') {
    store->file = waytator_screenshot_write_file(store->save_dir, store->name, store->png, &error);
    if (store->file == NULL)
      store->save_error = waytator_screenshot_describe_save_error(store->save_dir, error);
  }

  g_task_return_boolean(task, TRUE);
}

static void
waytator_screenshot_copy_done(GObject      *source_object,
                              GAsyncResult *result,
                              gpointer      user_data)
{
  GSubprocess *process = G_SUBPROCESS(source_object);

  waytator_screenshot_store_finish(user_data,
                                   g_subprocess_communicate_finish(process, result, NULL, NULL, NULL)
                                   && g_subprocess_get_successful(process));
}

static void
waytator_screenshot_stored(GObject      *source_object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  WaytatorScreenshotStore *store = user_data;
  g_autoptr(GSubprocess) process = NULL;

  (void) source_object;
  (void) result;

  if (!store->copy) {
    waytator_screenshot_store_finish(store, FALSE);
    return;
  }

  //wl-copy holds the image on its own, so it stays on the clipboard after waytator is gone
  process = g_subprocess_new(G_SUBPROCESS_FLAGS_STDIN_PIPE
                               | G_SUBPROCESS_FLAGS_STDOUT_SILENCE
                               | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
                             NULL,
                             "wl-copy",
                             "--type",
                             "image/png",
                             NULL);
  if (process == NULL) {
    waytator_screenshot_store_finish(store, FALSE);
    return;
  }

  g_subprocess_communicate_async(process, store->png, NULL, waytator_screenshot_copy_done, store);
}

//saves and copies a capture as the settings say, encoding a big one takes a moment so it runs off the main thread
void
waytator_screenshot_store(GdkTexture                  *texture,
                          const char                  *name,
                          const WaytatorSettings      *settings,
                          WaytatorScreenshotStoredFunc done,
                          gpointer                     user_data)
{
  WaytatorScreenshotStore *store = g_new0(WaytatorScreenshotStore, 1);
  g_autoptr(GTask) task = NULL;

  store->texture = g_object_ref(texture);
  store->name = g_strdup(name);
  store->save_dir = g_strdup(settings->screenshot_save_dir);
  store->copy = settings->screenshot_copy;
  store->done = done;
  store->user_data = user_data;

  //closing the last window must not cut a save off halfway
  g_application_hold(g_application_get_default());
  task = g_task_new(NULL, NULL, waytator_screenshot_stored, store);
  g_task_set_task_data(task, store, NULL);
  g_task_run_in_thread(task, waytator_screenshot_store_thread);
}

static void
waytator_screenshot_wait_over(WaytatorScreenshot *shot)
{
  waytator_screenshot_free(shot);
}

static void
waytator_screenshot_withdrawn(GObject      *source_object,
                              GAsyncResult *result,
                              gpointer      user_data)
{
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source_object), result, NULL);

  waytator_screenshot_wait_over(user_data);
}

//nobody is listening for its buttons after this, so the notification goes too rather than stay with buttons that do nothing
static gboolean
waytator_screenshot_wait_timed_out(gpointer user_data)
{
  WaytatorScreenshot *shot = user_data;

  shot->wait_timeout_id = 0;
  g_dbus_connection_signal_unsubscribe(shot->connection, shot->action_subscription);
  g_dbus_connection_signal_unsubscribe(shot->connection, shot->closed_subscription);
  shot->action_subscription = 0;
  shot->closed_subscription = 0;
  //quitting before the daemon has the request would lose it
  g_dbus_connection_call(shot->connection,
                         WAYTATOR_NOTIFICATIONS_NAME,
                         WAYTATOR_NOTIFICATIONS_PATH,
                         WAYTATOR_NOTIFICATIONS_NAME,
                         "CloseNotification",
                         g_variant_new("(u)", shot->notification_id),
                         NULL,
                         G_DBUS_CALL_FLAGS_NONE,
                         -1,
                         NULL,
                         waytator_screenshot_withdrawn,
                         shot);
  return G_SOURCE_REMOVE;
}

static void
waytator_screenshot_opened(GObject      *source_object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  WaytatorScreenshot *shot = user_data;
  g_autoptr(GError) error = NULL;

  (void) source_object;

  if (!g_app_info_launch_default_for_uri_finish(result, &error))
    g_warning("Could not open the screenshot: %s", error->message);

  waytator_screenshot_wait_over(shot);
}

static void waytator_screenshot_take_after(WaytatorScreenshot *shot,
                                           gboolean            wait,
                                           guint               delay_seconds);
static void waytator_screenshot_failed(WaytatorScreenshot *shot,
                                       const GError       *error);

static void
waytator_screenshot_allowed(GObject      *source_object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  WaytatorScreenshot *shot = user_data;
  g_autoptr(GError) error = NULL;

  (void) source_object;

  if (!waytator_capture_allow_finish(result, &error)) {
    g_prefix_error(&error, "Could not allow screenshots: ");
    waytator_screenshot_failed(shot, error);
    return;
  }

  //from the start, once the notification that asked has left the screen
  shot->retake = FALSE;
  waytator_screenshot_take_after(shot, TRUE, 0);
}

static void
waytator_screenshot_answer_permission(WaytatorScreenshot *shot,
                                      gboolean            allow)
{
  g_dbus_connection_signal_unsubscribe(shot->connection, shot->action_subscription);
  g_dbus_connection_signal_unsubscribe(shot->connection, shot->closed_subscription);
  shot->action_subscription = 0;
  shot->closed_subscription = 0;
  if (shot->wait_timeout_id != 0)
    g_source_remove(shot->wait_timeout_id);
  shot->wait_timeout_id = 0;
  shot->notification_id = 0;
  shot->asking_permission = FALSE;

  if (allow)
    waytator_capture_allow_async(waytator_screenshot_allowed, shot);
  else
    waytator_screenshot_wait_over(shot);
}

static void
waytator_screenshot_action_invoked(GDBusConnection *connection,
                                   const char      *sender_name,
                                   const char      *object_path,
                                   const char      *interface_name,
                                   const char      *signal_name,
                                   GVariant        *parameters,
                                   gpointer         user_data)
{
  WaytatorScreenshot *shot = user_data;
  const char *action = NULL;
  guint32 id = 0;

  (void) connection;
  (void) sender_name;
  (void) object_path;
  (void) interface_name;
  (void) signal_name;

  //anyone on the bus can send this signal, with any body
  if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(us)")))
    return;

  g_variant_get(parameters, "(u&s)", &id, &action);
  if (id != shot->notification_id)
    return;

  if (shot->asking_permission) {
    waytator_screenshot_answer_permission(shot, g_strcmp0(action, "allow") == 0 || g_strcmp0(action, "default") == 0);
    return;
  }

  if (g_strcmp0(action, "open") == 0 && shot->file != NULL) {
    g_autofree char *uri = g_file_get_uri(shot->file);

    //the viewer is started over the bus, quitting before it answers would cancel that
    g_dbus_connection_signal_unsubscribe(shot->connection, shot->action_subscription);
    g_dbus_connection_signal_unsubscribe(shot->connection, shot->closed_subscription);
    shot->action_subscription = 0;
    shot->closed_subscription = 0;
    if (shot->wait_timeout_id != 0)
      g_source_remove(shot->wait_timeout_id);
    shot->wait_timeout_id = 0;
    g_app_info_launch_default_for_uri_async(uri, NULL, NULL, waytator_screenshot_opened, shot);
    return;
  }

  waytator_screenshot_edit(shot);
  waytator_screenshot_wait_over(shot);
}

static void
waytator_screenshot_notification_closed(GDBusConnection *connection,
                                        const char      *sender_name,
                                        const char      *object_path,
                                        const char      *interface_name,
                                        const char      *signal_name,
                                        GVariant        *parameters,
                                        gpointer         user_data)
{
  WaytatorScreenshot *shot = user_data;
  guint32 id = 0;
  guint32 reason = 0;

  (void) connection;
  (void) sender_name;
  (void) object_path;
  (void) interface_name;
  (void) signal_name;

  if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(uu)")))
    return;

  g_variant_get(parameters, "(uu)", &id, &reason);
  if (id == shot->notification_id)
    waytator_screenshot_wait_over(shot);
}

static void
waytator_screenshot_notified(GObject      *source_object,
                             GAsyncResult *result,
                             gpointer      user_data)
{
  WaytatorScreenshot *shot = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source_object), result, &error);

  //with nothing to show a notification the editor is the only way left to hand the capture over,
  //and there is no other way to ask for the permission
  if (reply == NULL) {
    g_debug("Could not show a notification: %s", error->message);
    if (!shot->asking_permission)
      waytator_screenshot_edit(shot);
    waytator_screenshot_free(shot);
    return;
  }

  g_variant_get(reply, "(u)", &shot->notification_id);
  shot->wait_timeout_id = g_timeout_add_seconds(WAYTATOR_SCREENSHOT_WAIT_SECONDS,
                                                waytator_screenshot_wait_timed_out,
                                                shot);
}

static char *
waytator_screenshot_notification_body(WaytatorScreenshot *shot)
{
  if (shot->save_error != NULL)
    return shot->copied
         ? g_strdup_printf("%s It was copied to the clipboard.", shot->save_error)
         : g_strdup_printf("%s Choose Edit to keep it.", shot->save_error);

  if (shot->file != NULL) {
    g_autofree char *folder = waytator_screenshot_display_path(shot->settings.screenshot_save_dir);

    return shot->copied
         ? g_strdup_printf("Saved to %s and copied to the clipboard.", folder)
         : g_strdup_printf("Saved to %s.", folder);
  }

  return shot->copied
       ? g_strdup("Copied to the clipboard.")
       : g_strdup("Due to your settings, this screenshot is neither saved nor copied. Choose Edit to keep it.");
}

static void waytator_screenshot_send(WaytatorScreenshot *shot,
                                     const char         *summary,
                                     const char         *body,
                                     GVariantBuilder    *actions,
                                     GVariantBuilder    *hints);

static void
waytator_screenshot_notify(WaytatorScreenshot *shot)
{
  g_autofree char *body = waytator_screenshot_notification_body(shot);
  GVariantBuilder actions;
  GVariantBuilder hints;

  //without a session bus there is nobody to notify, the editor is what is left
  if (shot->connection == NULL) {
    waytator_screenshot_edit(shot);
    waytator_screenshot_free(shot);
    return;
  }

  //clicking the notification itself does the same as Edit
  g_variant_builder_init(&actions, G_VARIANT_TYPE("as"));
  g_variant_builder_add(&actions, "s", "default");
  g_variant_builder_add(&actions, "s", "Edit");
  if (shot->file != NULL) {
    g_variant_builder_add(&actions, "s", "open");
    g_variant_builder_add(&actions, "s", "Open");
  }
  g_variant_builder_add(&actions, "s", "edit");
  g_variant_builder_add(&actions, "s", "Edit");

  g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&hints, "{sv}", "desktop-entry", g_variant_new_string(g_application_get_application_id(shot->app)));
  if (shot->file != NULL) {
    g_autofree char *path = g_file_get_path(shot->file);

    if (path != NULL)
      g_variant_builder_add(&hints, "{sv}", "image-path", g_variant_new_string(path));
  }

  waytator_screenshot_send(shot,
                           shot->save_error != NULL ? "Screenshot not saved" : "Screenshot captured",
                           body,
                           &actions,
                           &hints);
}

//shows the notification and listens to it, until it is closed or the wait runs out
static void
waytator_screenshot_send(WaytatorScreenshot *shot,
                         const char         *summary,
                         const char         *body,
                         GVariantBuilder    *actions,
                         GVariantBuilder    *hints)
{
  //listening before asking, a quick click could otherwise get here first
  shot->action_subscription = g_dbus_connection_signal_subscribe(shot->connection,
                                                                 WAYTATOR_NOTIFICATIONS_NAME,
                                                                 WAYTATOR_NOTIFICATIONS_NAME,
                                                                 "ActionInvoked",
                                                                 WAYTATOR_NOTIFICATIONS_PATH,
                                                                 NULL,
                                                                 G_DBUS_SIGNAL_FLAGS_NONE,
                                                                 waytator_screenshot_action_invoked,
                                                                 shot,
                                                                 NULL);
  shot->closed_subscription = g_dbus_connection_signal_subscribe(shot->connection,
                                                                 WAYTATOR_NOTIFICATIONS_NAME,
                                                                 WAYTATOR_NOTIFICATIONS_NAME,
                                                                 "NotificationClosed",
                                                                 WAYTATOR_NOTIFICATIONS_PATH,
                                                                 NULL,
                                                                 G_DBUS_SIGNAL_FLAGS_NONE,
                                                                 waytator_screenshot_notification_closed,
                                                                 shot,
                                                                 NULL);

  g_dbus_connection_call(shot->connection,
                         WAYTATOR_NOTIFICATIONS_NAME,
                         WAYTATOR_NOTIFICATIONS_PATH,
                         WAYTATOR_NOTIFICATIONS_NAME,
                         "Notify",
                         g_variant_new("(susssasa{sv}i)",
                                       "Waytator",
                                       (guint32) 0,
                                       g_application_get_application_id(shot->app),
                                       summary,
                                       body,
                                       actions,
                                       hints,
                                       -1),
                         G_VARIANT_TYPE("(u)"),
                         G_DBUS_CALL_FLAGS_NONE,
                         -1,
                         NULL,
                         waytator_screenshot_notified,
                         shot);
}

static void
waytator_screenshot_notify_stored(GFile      *file,
                                  const char *save_error,
                                  gboolean    copied,
                                  gpointer    user_data)
{
  WaytatorScreenshot *shot = user_data;

  shot->file = file != NULL ? g_object_ref(file) : NULL;
  shot->save_error = g_strdup(save_error);
  shot->copied = copied;
  waytator_screenshot_notify(shot);
}

static void
waytator_screenshot_captured(WaytatorScreenshot *shot,
                             GdkTexture         *texture)
{
  waytator_window_restore_all(shot->hidden);
  shot->texture = g_object_ref(texture);
  shot->name = waytator_screenshot_make_name();

  if (*shot->settings.screenshot_save_dir == '\0' && !shot->settings.screenshot_copy) {
    waytator_screenshot_notify(shot);
    return;
  }

  waytator_screenshot_store(texture, shot->name, &shot->settings, waytator_screenshot_notify_stored, shot);
}

//the portal turned the capture down until it is allowed to take screenshots, which a click on this gives
static void
waytator_screenshot_ask_permission(WaytatorScreenshot *shot)
{
  GVariantBuilder actions;
  GVariantBuilder hints;

  waytator_window_restore_all(shot->hidden);
  shot->asking_permission = TRUE;

  g_variant_builder_init(&actions, G_VARIANT_TYPE("as"));
  g_variant_builder_add(&actions, "s", "default");
  g_variant_builder_add(&actions, "s", "Allow");
  g_variant_builder_add(&actions, "s", "allow");
  g_variant_builder_add(&actions, "s", "Allow");

  g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&hints, "{sv}", "desktop-entry", g_variant_new_string(g_application_get_application_id(shot->app)));

  waytator_screenshot_send(shot,
                           "Allow Screenshots?",
                           "Waytator needs a one-time permission to take screenshots. "
                           "Choose Allow and the screenshot is taken again.",
                           &actions,
                           &hints);
}

//nothing was captured, so there is nothing to offer but the reason
static void
waytator_screenshot_failed(WaytatorScreenshot *shot,
                           const GError       *error)
{
  if (shot->connection != NULL && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED)) {
    waytator_screenshot_ask_permission(shot);
    return;
  }

  g_printerr("Could not take a screenshot: %s\n", error->message);
  if (shot->connection != NULL)
    g_dbus_connection_call(shot->connection,
                           WAYTATOR_NOTIFICATIONS_NAME,
                           WAYTATOR_NOTIFICATIONS_PATH,
                           WAYTATOR_NOTIFICATIONS_NAME,
                           "Notify",
                           g_variant_new("(susssasa{sv}i)",
                                         "Waytator",
                                         (guint32) 0,
                                         g_application_get_application_id(shot->app),
                                         "Screenshot failed",
                                         error->message,
                                         NULL,
                                         NULL,
                                         -1),
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           -1,
                           NULL,
                           NULL,
                           NULL);
  waytator_screenshot_free(shot);
}

static void
waytator_screenshot_overlay_done(GdkTexture         *texture,
                                 WaytatorCaptureMode mode,
                                 gpointer            user_data)
{
  WaytatorScreenshot *shot = user_data;

  (void) mode;

  if (texture == NULL)
    waytator_screenshot_free(shot);
  else
    waytator_screenshot_captured(shot, texture);
}

static void waytator_screenshot_overlay_retake(gboolean            hide_waytator,
                                               WaytatorCaptureMode mode,
                                               guint               delay_seconds,
                                               gpointer            user_data);

static void
waytator_screenshot_outputs_ready(GObject      *source_object,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
  WaytatorScreenshot *shot = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) outputs = waytator_capture_outputs_finish(result, &error);
  WaytatorCaptureOutput *output = NULL;

  (void) source_object;

  if (outputs == NULL) {
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      waytator_screenshot_free(shot);
    else
      waytator_screenshot_failed(shot, error);
    return;
  }

  if (!shot->retake)
    output = waytator_capture_pick_output(outputs, shot->mode, shot->output_name, &error);
  if (error != NULL) {
    waytator_screenshot_failed(shot, error);
    return;
  }

  if (output != NULL) {
    g_autoptr(GdkTexture) texture = waytator_capture_output_create_texture(output);

    waytator_screenshot_captured(shot, texture);
    return;
  }

  //the box only means something when there is a waytator window that would be in the picture
  if (waytator_capture_overlay_show(g_steal_pointer(&outputs),
                                    shot->mode,
                                    shot->hidden->len > 0 || waytator_window_any_visible(GTK_APPLICATION(shot->app)),
                                    shot->hide,
                                    waytator_screenshot_overlay_retake,
                                    waytator_screenshot_overlay_done,
                                    shot))
    return;

  g_set_error_literal(&error,
                      G_IO_ERROR,
                      G_IO_ERROR_FAILED,
                      "Could not match the capture to a screen");
  waytator_screenshot_failed(shot, error);
}

static gboolean
waytator_screenshot_take(gpointer user_data)
{
  WaytatorScreenshot *shot = user_data;

  shot->delay_id = 0;
  waytator_capture_outputs_async(NULL, waytator_screenshot_outputs_ready, shot);
  return G_SOURCE_REMOVE;
}

//wait is for whatever else has to leave the screen first, like the overlay of a retake,
//the delay is the one someone asked for and is long enough for that on its own
static void
waytator_screenshot_take_after(WaytatorScreenshot *shot,
                               gboolean            wait,
                               guint               delay_seconds)
{
  guint delay_ms = delay_seconds * 1000;

  if (shot->hide)
    wait |= waytator_window_hide_all(GTK_APPLICATION(shot->app), shot->hidden);
  else
    wait |= waytator_window_restore_all(shot->hidden);

  if (wait)
    delay_ms = MAX(delay_ms, WAYTATOR_CAPTURE_SETTLE_MS);

  if (delay_ms == 0) {
    waytator_screenshot_take(shot);
    return;
  }

  shot->delay_id = g_timeout_add(delay_ms, waytator_screenshot_take, shot);
}

static void
waytator_screenshot_overlay_retake(gboolean            hide_waytator,
                                   WaytatorCaptureMode mode,
                                   guint               delay_seconds,
                                   gpointer            user_data)
{
  WaytatorScreenshot *shot = user_data;

  //the box is for this capture only, the preference stays as it was
  shot->hide = hide_waytator;
  shot->mode = mode;
  shot->retake = TRUE;
  waytator_screenshot_take_after(shot, TRUE, delay_seconds);
}

//captures without showing a window: the result is saved, copied and announced, edit is how it reaches the editor
void
waytator_screenshot_capture_background(GApplication              *app,
                                       WaytatorCaptureMode        mode,
                                       const char                *output_name,
                                       guint                      delay_seconds,
                                       WaytatorScreenshotEditFunc edit,
                                       gpointer                   user_data,
                                       GDestroyNotify             user_data_free)
{
  WaytatorScreenshot *shot = g_new0(WaytatorScreenshot, 1);

  shot->app = g_object_ref(app);
  shot->mode = mode;
  shot->output_name = g_strdup(output_name);
  shot->edit = edit;
  shot->user_data = user_data;
  shot->user_data_free = user_data_free;
  waytator_settings_load(&shot->settings);
  shot->hide = shot->settings.capture_hide_waytator;
  shot->hidden = g_ptr_array_new_with_free_func(g_object_unref);
  if (g_application_get_dbus_connection(app) != NULL)
    shot->connection = g_object_ref(g_application_get_dbus_connection(app));
  g_application_hold(app);

  waytator_screenshot_take_after(shot, FALSE, delay_seconds);
}
