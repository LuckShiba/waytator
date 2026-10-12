#include <adwaita.h>
#include <sys/stat.h>
#include <unistd.h>

#include "waytator-window.h"

#define WAYTATOR_APP_ID "dev.faetalize.waytator"

static GBytes *
app_read_stream_bytes(GInputStream *stream,
                      GError      **error)
{
  g_autoptr(GByteArray) buffer = g_byte_array_new();

  while (TRUE) {
    g_autoptr(GBytes) chunk = NULL;
    gsize chunk_size;
    const guint8 *chunk_data;

    chunk = g_input_stream_read_bytes(stream, 64 * 1024, NULL, error);
    if (chunk == NULL)
      return NULL;

    chunk_data = g_bytes_get_data(chunk, &chunk_size);
    if (chunk_size == 0)
      break;

    g_byte_array_append(buffer, chunk_data, chunk_size);
  }

  if (buffer->len == 0) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_ARGUMENT,
                "No image data was provided on stdin");
    return NULL;
  }

  return g_byte_array_free_to_bytes(g_steal_pointer(&buffer));
}

static gboolean
app_should_read_stdin(void)
{
  struct stat stdin_stat;

  if (isatty(STDIN_FILENO))
    return FALSE;

  if (fstat(STDIN_FILENO, &stdin_stat) != 0)
    return FALSE;

  return S_ISFIFO(stdin_stat.st_mode)
      || S_ISREG(stdin_stat.st_mode);
}

static WaytatorWindow *
app_get_or_create_window(AdwApplication *app)
{
  GtkWindow *window = gtk_application_get_active_window(GTK_APPLICATION(app));

  if (window == NULL)
    window = GTK_WINDOW(waytator_window_new(app));

  return WAYTATOR_WINDOW(window);
}

static void
quit_action(GSimpleAction *action,
            GVariant      *parameter,
            gpointer       user_data)
{
  GApplication *app = G_APPLICATION(user_data);

  (void) action;
  (void) parameter;

  g_application_quit(app);
}

static const GActionEntry app_actions[] = {
  { .name = "quit", .activate = quit_action },
};

static void
app_activate(GApplication *app,
             gpointer      user_data)
{
  (void) user_data;

  gtk_window_present(GTK_WINDOW(app_get_or_create_window(ADW_APPLICATION(app))));
}

static void
app_open(GApplication  *app,
         GFile        **files,
         int            n_files,
         const char    *hint,
         gpointer       user_data)
{
  WaytatorWindow *window;
  g_autoptr(GError) error = NULL;

  (void) hint;
  (void) user_data;

  window = app_get_or_create_window(ADW_APPLICATION(app));

  if (n_files > 0 && files[0] != NULL && !waytator_window_open_file(window, files[0], &error)) {
    g_printerr("Error loading image: %s\n", error->message);
    return;
  }

  gtk_window_present(GTK_WINDOW(window));
}

#define APP_USAGE \
  "Usage: %s [OPTIONS] [IMAGE|-|--stdin]\n" \
  "\n" \
  "  --name NAME                             Name for an image read from stdin\n" \
  "  --close-on-copy, --no-close-on-copy     Override the \"Close on copy\" preference\n" \
  "  --close-window-shortcut SHORTCUT        Override a shortcut for this launch\n" \
  "  --copy-shortcut SHORTCUT\n" \
  "  --copy-without-closing-shortcut SHORTCUT\n" \
  "  --save-shortcut SHORTCUT\n" \
  "  --save-and-close-shortcut SHORTCUT\n" \
  "  --save-copy-and-close-shortcut SHORTCUT\n" \
  "\n" \
  "SHORTCUT is a key or key combination such as \"s\" or \"<Control><Shift>s\",\n" \
  "\"right-click\", or \"none\". Overrides last for this launch only.\n"

static const struct {
  const char *option;
  const char *name;
} app_shortcut_options[] = {
  { "--close-window-shortcut", "close-window" },
  { "--copy-shortcut", "copy" },
  { "--copy-without-closing-shortcut", "copy-without-closing" },
  { "--save-shortcut", "save" },
  { "--save-and-close-shortcut", "save-and-close" },
  { "--save-copy-and-close-shortcut", "save-copy-and-close" },
};

static int
app_find_shortcut_option(const char *argument)
{
  for (guint i = 0; i < G_N_ELEMENTS(app_shortcut_options); i++) {
    if (g_strcmp0(argument, app_shortcut_options[i].option) == 0)
      return i;
  }

  return -1;
}

static int
app_command_line(GApplication            *app,
                 GApplicationCommandLine *command_line,
                 gpointer                 user_data)
{
  gchar **arguments = NULL;
  int argc = 0;
  int status = 0;
  gboolean use_stdin = FALSE;
  int close_on_copy = -1;
  const char *shortcuts[G_N_ELEMENTS(app_shortcut_options)] = { NULL };
  const char *stdin_name = "stdin.png";
  g_autoptr(GError) error = NULL;
  g_autoptr(GFile) file = NULL;
  WaytatorWindow *window;

  (void) user_data;

  arguments = g_application_command_line_get_arguments(command_line, &argc);

  for (int i = 1; i < argc; i++) {
    const int shortcut_option = app_find_shortcut_option(arguments[i]);

    if (shortcut_option >= 0) {
      if (i + 1 >= argc) {
        g_application_command_line_printerr(command_line,
                                            "Missing value for %s\n",
                                            arguments[i]);
        status = 1;
        goto done;
      }

      if (!waytator_window_shortcut_is_valid(arguments[i + 1])) {
        g_application_command_line_printerr(command_line,
                                            "Invalid shortcut for %s: %s\n",
                                            arguments[i],
                                            arguments[i + 1]);
        status = 1;
        goto done;
      }

      shortcuts[shortcut_option] = arguments[++i];
      continue;
    }

    if (g_strcmp0(arguments[i], "--close-on-copy") == 0) {
      close_on_copy = 1;
      continue;
    }

    if (g_strcmp0(arguments[i], "--no-close-on-copy") == 0) {
      close_on_copy = 0;
      continue;
    }

    if (g_strcmp0(arguments[i], "--help") == 0 || g_strcmp0(arguments[i], "-h") == 0) {
      g_application_command_line_print(command_line, APP_USAGE, arguments[0]);
      goto done;
    }

    if (g_strcmp0(arguments[i], "--name") == 0) {
      if (i + 1 >= argc) {
        g_application_command_line_printerr(command_line,
                                            "Missing value for --name\n");
        status = 1;
        goto done;
      }

      stdin_name = arguments[++i];
    } else if (g_strcmp0(arguments[i], "-") == 0 || g_strcmp0(arguments[i], "--stdin") == 0) {
      use_stdin = TRUE;
    } else if (arguments[i][0] == '-') {
      g_application_command_line_printerr(command_line,
                                          "Unknown option: %s\n",
                                          arguments[i]);
      status = 1;
      goto done;
    } else if (file == NULL) {
      file = g_application_command_line_create_file_for_arg(command_line, arguments[i]);
    } else {
      g_application_command_line_printerr(command_line, APP_USAGE, arguments[0]);
      status = 1;
      goto done;
    }
  }

  window = app_get_or_create_window(ADW_APPLICATION(app));
  waytator_window_clear_overrides(window);
  if (close_on_copy >= 0)
    waytator_window_set_close_on_copy_override(window, close_on_copy);
  for (guint i = 0; i < G_N_ELEMENTS(app_shortcut_options); i++) {
    if (shortcuts[i] != NULL)
      waytator_window_set_shortcut_override(window, app_shortcut_options[i].name, shortcuts[i]);
  }

  if (use_stdin) {
    g_autoptr(GBytes) bytes = NULL;

    bytes = app_read_stream_bytes(g_application_command_line_get_stdin(command_line), &error);
    if (bytes == NULL || !waytator_window_open_bytes(window, bytes, stdin_name, &error)) {
      g_application_command_line_printerr(command_line,
                                          "Error loading image from stdin: %s\n",
                                          error->message);
      status = 1;
      goto done;
    }
  } else if (file != NULL) {
    if (!waytator_window_open_file(window, file, &error)) {
      g_application_command_line_printerr(command_line,
                                          "Error loading image '%s': %s\n",
                                          arguments[1],
                                          error->message);
      status = 1;
      goto done;
    }
  }

  gtk_window_present(GTK_WINDOW(window));

done:
  g_strfreev(arguments);
  return status;
}

int
main(int   argc,
     char *argv[])
{
  g_autoptr(AdwApplication) app = NULL;
  char **run_argv = argv;
  char **stdin_argv = NULL;
  int run_argc = argc;
  int status;

  if (argc == 1 && app_should_read_stdin()) {
    stdin_argv = g_new0(char *, 3);
    stdin_argv[0] = argv[0];
    stdin_argv[1] = (char *) "--stdin";
    run_argv = stdin_argv;
    run_argc = 2;
  }

  app = adw_application_new(WAYTATOR_APP_ID,
                             G_APPLICATION_HANDLES_COMMAND_LINE | G_APPLICATION_HANDLES_OPEN);
  g_application_set_resource_base_path(G_APPLICATION(app), "/dev/faetalize/waytator");
  gtk_window_set_default_icon_name(WAYTATOR_APP_ID);

  g_action_map_add_action_entries(G_ACTION_MAP(app),
                                  app_actions,
                                  G_N_ELEMENTS(app_actions),
                                  app);

  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.open", (const char *[]) { "<Primary>o", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.preferences", (const char *[]) { "<Primary>comma", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.undo", (const char *[]) { "<Primary>z", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.redo", (const char *[]) { "<Primary><Shift>z", "<Primary>y", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.zoom-in", (const char *[]) { "<Primary>plus", "<Primary>equal", "<Primary>KP_Add", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.zoom-out", (const char *[]) { "<Primary>minus", "<Primary>KP_Subtract", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "win.zoom-fit", (const char *[]) { "<Primary>0", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.quit", (const char *[]) { "<Primary>q", NULL });

  g_signal_connect(app, "activate", G_CALLBACK(app_activate), NULL);
  g_signal_connect(app, "open", G_CALLBACK(app_open), NULL);
  g_signal_connect(app, "command-line", G_CALLBACK(app_command_line), NULL);

  status = g_application_run(G_APPLICATION(app), run_argc, run_argv);
  g_free(stdin_argv);

  return status;
}
