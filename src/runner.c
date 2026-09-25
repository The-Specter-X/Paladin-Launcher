#include "runner.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <string.h>

static gboolean env_key_is_valid(const char *key)
{
    if (!key || !*key || !(g_ascii_isalpha(*key) || *key == '_')) return FALSE;
    for (const char *p = key + 1; *p; p++)
        if (!(g_ascii_isalnum(*p) || *p == '_')) return FALSE;
    return TRUE;
}

static gboolean add_custom_env(GSubprocessLauncher *launcher, const char *contents,
                               GError **error)
{
    g_auto(GStrv) lines = g_strsplit(contents ? contents : "", "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char *line = g_strstrip(lines[i]);
        if (!*line || *line == '#') continue;
        char *separator = strchr(line, '=');
        if (!separator) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                        "Environment line %d must be NAME=value", i + 1);
            return FALSE;
        }
        *separator = '\0';
        char *key = g_strstrip(line);
        if (!env_key_is_valid(key) ||
            g_str_equal(key, "WINEPREFIX") || g_str_equal(key, "PROTONPATH") ||
            g_str_equal(key, "GAMEID") || g_str_equal(key, "PROTON_ENABLE_WAYLAND") ||
            g_str_equal(key, "PROTON_USE_WOW64")) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                        "Invalid or reserved environment variable on line %d", i + 1);
            return FALSE;
        }
        g_subprocess_launcher_setenv(launcher, key, separator + 1, TRUE);
    }
    return TRUE;
}

GSubprocess *runner_start(const Game *game, const char *executable,
                          gboolean installer, GError **error)
{
    if (!executable || !g_path_is_absolute(executable) ||
        !g_file_test(executable, G_FILE_TEST_IS_REGULAR)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                            "Select an existing game or installer executable");
        return NULL;
    }
    if (!g_find_program_in_path("umu-run")) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                            "umu-run is missing. Install umu-launcher to play Windows games.");
        return NULL;
    }

    g_autofree char *prefix = game_prefix_path(game);
    g_autofree char *log = game_log_path(game, FALSE);
    g_autofree char *err_log = game_log_path(game, TRUE);
    g_autofree char *log_dir = g_path_get_dirname(log);
    if (g_mkdir_with_parents(prefix, 0700) != 0 ||
        g_mkdir_with_parents(log_dir, 0700) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Cannot prepare game directories: %s", g_strerror(errno));
        return NULL;
    }
    g_autofree char *cwd = g_path_get_dirname(executable);
    g_autoptr(GSubprocessLauncher) launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_NONE);
    g_subprocess_launcher_set_cwd(launcher, cwd);
    g_subprocess_launcher_set_stdout_file_path(launcher, log);
    g_subprocess_launcher_set_stderr_file_path(launcher, err_log);
    g_subprocess_launcher_setenv(launcher, "WINEPREFIX", prefix, TRUE);
    g_subprocess_launcher_setenv(launcher, "PROTONPATH",
                                 game->runner && *game->runner ? game->runner : "GE-Proton", TRUE);
    g_subprocess_launcher_setenv(launcher, "GAMEID", "umu-default", TRUE);
    g_subprocess_launcher_setenv(launcher, "PROTON_ENABLE_WAYLAND",
                                 game->wayland ? "1" : "0", TRUE);
    g_subprocess_launcher_setenv(launcher, "PROTON_USE_WOW64",
                                 game->wow64 ? "1" : "0", TRUE);
    g_subprocess_launcher_setenv(launcher, "WINE_FULLSCREEN_FSR",
                                 game->fsr ? "1" : "0", TRUE);
    g_subprocess_launcher_setenv(launcher, "PROTON_USE_WINED3D",
                                 game->wined3d ? "1" : "0", TRUE);
    if (!add_custom_env(launcher, game->environment, error)) return NULL;

    g_autofree char *extra = installer ? NULL : g_strdup(game->arguments);
    gint argc = 0;
    g_auto(GStrv) parsed = NULL;
    if (extra && *extra && !g_shell_parse_argv(extra, &argc, &parsed, error)) return NULL;
    g_autoptr(GPtrArray) argv = g_ptr_array_new();
    g_ptr_array_add(argv, "umu-run");
    g_ptr_array_add(argv, (gpointer) executable);
    for (int i = 0; i < argc; i++) g_ptr_array_add(argv, parsed[i]);
    g_ptr_array_add(argv, NULL);
    return g_subprocess_launcher_spawnv(launcher, (const gchar * const *) argv->pdata, error);
}
