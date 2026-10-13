#include "waytator-screenshot.h"

#include "waytator-capture-overlay.h"

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
  WaytatorScreenshotEditFunc edit;
  gpointer user_data;
  GdkTexture *texture;
  char *name;
  char *save_dir;
  gboolean copy;
  GBytes *png;
  GFile *file;
  char *save_error;
  gboolean copied;
  GDBusConnection *connection;
  guint32 notification_id;
  guint action_subscription;
  guint closed_subscription;
  guint wait_timeout_id;
} WaytatorScreenshot;

char *
waytator_screenshot_default_save_dir(void)
{
  const char *pictures = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);

  if (pictures != NULL)
    return g_build_filename(pictures, "Screenshots", NULL);

  return g_build_filename(g_get_home_dir(), "Pictures", "Screenshots", NULL);
}

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

//save_dir comes back empty when saving is switched off
void
waytator_screenshot_load_settings(char    **save_dir,
                                  gboolean *copy)
{
  g_autofree char *path = g_build_filename(g_get_user_config_dir(), WAYTATOR_SETTINGS_FILE, NULL);
  g_autoptr(GKeyFile) key_file = g_key_file_new();

  *save_dir = NULL;
  *copy = TRUE;

  if (g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, NULL)) {
    if (g_key_file_has_key(key_file, WAYTATOR_SETTINGS_GROUP, "screenshot_save_dir", NULL))
      *save_dir = g_key_file_get_string(key_file, WAYTATOR_SETTINGS_GROUP, "screenshot_save_dir", NULL);
    if (g_key_file_has_key(key_file, WAYTATOR_SETTINGS_GROUP, "screenshot_copy", NULL))
      *copy = g_key_file_get_boolean(key_file, WAYTATOR_SETTINGS_GROUP, "screenshot_copy", NULL);
  }

  if (*save_dir == NULL)
    *save_dir = waytator_screenshot_default_save_dir();
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

  g_clear_object(&shot->connection);
  g_clear_object(&shot->texture);
  g_clear_object(&shot->file);
  g_clear_pointer(&shot->png, g_bytes_unref);
  g_free(shot->output_name);
  g_free(shot->name);
  g_free(shot->save_dir);
  g_free(shot->save_error);
  //the application stayed alive for this, with no window of its own
  g_application_release(shot->app);
  g_object_unref(shot->app);
  g_free(shot);
}

static void
waytator_screenshot_edit(WaytatorScreenshot *shot)
{
  shot->edit(shot->texture, shot->file, shot->name, shot->user_data);
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

//a name in the folder that nothing uses yet, for a save that writes the file itself
GFile *
waytator_screenshot_reserve_file(const char *save_dir,
                                 const char *name,
                                 GError    **error)
{
  g_autoptr(GFile) folder = waytator_screenshot_ensure_folder(save_dir, error);

  if (folder == NULL)
    return NULL;

  for (int attempt = 0; attempt < 1000; attempt++) {
    g_autofree char *candidate = waytator_screenshot_candidate_name(name, attempt);
    g_autoptr(GFile) file = g_file_get_child(folder, candidate);

    if (!g_file_query_exists(file, NULL))
      return g_steal_pointer(&file);
  }

  g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS, "too many screenshots share the name %s", name);
  return NULL;
}

static GFile *
waytator_screenshot_write_file(const char *save_dir,
                               const char *name,
                               GBytes     *png,
                               GError    **error)
{
  g_autoptr(GFile) folder = waytator_screenshot_ensure_folder(save_dir, error);

  if (folder == NULL)
    return NULL;

  for (int attempt = 0; attempt < 1000; attempt++) {
    g_autofree char *candidate = waytator_screenshot_candidate_name(name, attempt);
    g_autoptr(GFile) file = g_file_get_child(folder, candidate);
    g_autoptr(GFileOutputStream) stream = NULL;
    g_autoptr(GError) create_error = NULL;

    stream = g_file_create(file, G_FILE_CREATE_NONE, NULL, &create_error);
    if (stream == NULL) {
      if (g_error_matches(create_error, G_IO_ERROR, G_IO_ERROR_EXISTS))
        continue;

      g_propagate_error(error, g_steal_pointer(&create_error));
      return NULL;
    }

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

  g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS, "too many screenshots share the name %s", name);
  return NULL;
}

//encoding a whole screen takes long enough to notice, so this runs off the main thread
static void
waytator_screenshot_store_thread(GTask        *task,
                                 gpointer      source_object,
                                 gpointer      task_data,
                                 GCancellable *cancellable)
{
  WaytatorScreenshot *shot = task_data;
  g_autoptr(GError) error = NULL;

  (void) source_object;
  (void) cancellable;

  shot->png = gdk_texture_save_to_png_bytes(shot->texture);
  if (*shot->save_dir != '\0') {
    shot->file = waytator_screenshot_write_file(shot->save_dir, shot->name, shot->png, &error);
    if (shot->file == NULL)
      shot->save_error = waytator_screenshot_describe_save_error(shot->save_dir, error);
  }

  g_task_return_boolean(task, TRUE);
}

static void
waytator_screenshot_wait_over(WaytatorScreenshot *shot)
{
  waytator_screenshot_free(shot);
}

static gboolean
waytator_screenshot_wait_timed_out(gpointer user_data)
{
  WaytatorScreenshot *shot = user_data;

  shot->wait_timeout_id = 0;
  waytator_screenshot_wait_over(shot);
  return G_SOURCE_REMOVE;
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

  g_variant_get(parameters, "(u&s)", &id, &action);
  if (id != shot->notification_id)
    return;

  if (g_strcmp0(action, "open") == 0 && shot->file != NULL) {
    g_autofree char *uri = g_file_get_uri(shot->file);

    g_app_info_launch_default_for_uri_async(uri, NULL, NULL, NULL, NULL);
  } else {
    waytator_screenshot_edit(shot);
  }

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

  //with nothing to show a notification the editor is the only way left to hand the capture over
  if (reply == NULL) {
    g_debug("Could not show a notification: %s", error->message);
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
    g_autofree char *folder = waytator_screenshot_display_path(shot->save_dir);

    return shot->copied
         ? g_strdup_printf("Saved to %s and copied to the clipboard.", folder)
         : g_strdup_printf("Saved to %s.", folder);
  }

  return shot->copied
       ? g_strdup("Copied to the clipboard.")
       : g_strdup("Due to your settings, this screenshot is neither saved nor copied. Choose Edit to keep it.");
}

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
                                       shot->save_error != NULL ? "Screenshot not saved" : "Screenshot captured",
                                       body,
                                       &actions,
                                       &hints,
                                       -1),
                         G_VARIANT_TYPE("(u)"),
                         G_DBUS_CALL_FLAGS_NONE,
                         -1,
                         NULL,
                         waytator_screenshot_notified,
                         shot);
}

static void
waytator_screenshot_copy_done(GObject      *source_object,
                              GAsyncResult *result,
                              gpointer      user_data)
{
  WaytatorScreenshot *shot = user_data;
  GSubprocess *process = G_SUBPROCESS(source_object);

  shot->copied = g_subprocess_communicate_finish(process, result, NULL, NULL, NULL)
              && g_subprocess_get_successful(process);
  waytator_screenshot_notify(shot);
}

static void
waytator_screenshot_stored(GObject      *source_object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  WaytatorScreenshot *shot = user_data;
  g_autoptr(GSubprocess) process = NULL;

  (void) source_object;
  (void) result;

  if (!shot->copy) {
    waytator_screenshot_notify(shot);
    return;
  }

  //there is no window to keep a clipboard alive, wl-copy holds the image on its own
  process = g_subprocess_new(G_SUBPROCESS_FLAGS_STDIN_PIPE
                               | G_SUBPROCESS_FLAGS_STDOUT_SILENCE
                               | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
                             NULL,
                             "wl-copy",
                             "--type",
                             "image/png",
                             NULL);
  if (process == NULL) {
    waytator_screenshot_notify(shot);
    return;
  }

  g_subprocess_communicate_async(process, shot->png, NULL, waytator_screenshot_copy_done, shot);
}

static void
waytator_screenshot_captured(WaytatorScreenshot *shot,
                             GdkTexture         *texture)
{
  g_autoptr(GTask) task = NULL;

  shot->texture = g_object_ref(texture);
  shot->name = waytator_screenshot_make_name();
  waytator_screenshot_load_settings(&shot->save_dir, &shot->copy);

  if (*shot->save_dir == '\0' && !shot->copy) {
    waytator_screenshot_notify(shot);
    return;
  }

  task = g_task_new(NULL, NULL, waytator_screenshot_stored, shot);
  g_task_set_task_data(task, shot, NULL);
  g_task_run_in_thread(task, waytator_screenshot_store_thread);
}

//nothing was captured, so there is nothing to offer but the reason
static void
waytator_screenshot_failed(WaytatorScreenshot *shot,
                           const GError       *error)
{
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

static void
waytator_screenshot_outputs_ready(GObject      *source_object,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
  WaytatorScreenshot *shot = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) outputs = waytator_capture_outputs_finish(result, &error);
  WaytatorCaptureOutput *output;

  (void) source_object;

  if (outputs == NULL) {
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      waytator_screenshot_free(shot);
    else
      waytator_screenshot_failed(shot, error);
    return;
  }

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

  if (waytator_capture_overlay_show(g_steal_pointer(&outputs),
                                    shot->mode,
                                    waytator_screenshot_overlay_done,
                                    shot))
    return;

  g_set_error_literal(&error,
                      G_IO_ERROR,
                      G_IO_ERROR_FAILED,
                      "Could not match the capture to a screen");
  waytator_screenshot_failed(shot, error);
}

//captures without showing a window: the result is saved, copied and announced, edit is how it reaches the editor
void
waytator_screenshot_capture_background(GApplication              *app,
                                       WaytatorCaptureMode        mode,
                                       const char                *output_name,
                                       WaytatorScreenshotEditFunc edit,
                                       gpointer                   user_data)
{
  WaytatorScreenshot *shot = g_new0(WaytatorScreenshot, 1);

  shot->app = g_object_ref(app);
  shot->mode = mode;
  shot->output_name = g_strdup(output_name);
  shot->edit = edit;
  shot->user_data = user_data;
  if (g_application_get_dbus_connection(app) != NULL)
    shot->connection = g_object_ref(g_application_get_dbus_connection(app));
  g_application_hold(app);

  waytator_capture_outputs_async(NULL, waytator_screenshot_outputs_ready, shot);
}
