#include "runner.h"
#include "storage.h"
#include <errno.h>
#include <glib/gstdio.h>
#include <gmodule.h>
#include <string.h>

static gboolean invalid(GError **error, const char *message)
{
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, message);
    return FALSE;
}

static gboolean reserved(const char *key)
{
    const char *names[] = {"WINEPREFIX", "WINEARCH", "PROTONPATH", "GAMEID", "UMU_ID",
        "PROTON_ENABLE_WAYLAND", "PROTON_USE_WAYLAND", "WINE_GRAPHICS_DRIVER",
        "PROTON_USE_WOW64", "PROTON_ADD_CONFIG", "STEAM_COMPAT_CONFIG",
        "PROTON_ENABLE_NVAPI", "PROTON_DISABLE_NVAPI", "PROTON_FORCE_NVAPI",
        "PROTON_USE_WINED3D", "PROTON_USE_WINED3D11", "WINE_FULLSCREEN_FSR",
        "PROTON_LOG_DIR", "PROTON_DEBUG_DIR", "PROTON_CRASH_REPORT_DIR",
        "HOME", "PROTON_VERB", "UMU_NO_PROTON", NULL};
    for (guint i = 0; names[i]; i++) if (g_str_equal(key, names[i])) return TRUE;
    return g_str_has_prefix(key, "STEAM_COMPAT_") || g_str_has_prefix(key, "XDG_");
}

static gboolean custom_environment(GSubprocessLauncher *launcher, const char *contents,
                                   GError **error)
{
    g_auto(GStrv) lines = g_strsplit(contents ? contents : "", "\n", -1);
    for (guint i = 0; lines[i]; i++) {
        char *line = g_strchug(lines[i]);
        if (!*line || *line == '#') continue;
        char *equals = strchr(line, '=');
        if (!equals) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                        "Environment line %u must be NAME=value", i + 1);
            return FALSE;
        }
        *equals = '\0';
        char *key = g_strstrip(line);
        gboolean valid = *key && (g_ascii_isalpha(*key) || *key == '_');
        for (const char *p = key; *p; p++)
            if (!g_ascii_isalnum(*p) && *p != '_') valid = FALSE;
        if (!valid || reserved(key)) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                        "Environment line %u has an invalid name or conflicts with a Paladin control: %s",
                        i + 1, key);
            return FALSE;
        }
        if (launcher) g_subprocess_launcher_setenv(launcher, key, equals + 1, TRUE);
    }
    return TRUE;
}

gboolean runner_validate(const Game *game, GError **error)
{
    g_autofree char *name = g_strdup(game->name);
    if (!name || !*g_strstrip(name) || !g_utf8_validate(name, -1, NULL))
        return invalid(error, "Enter a valid game title");
    if (game->removing) return invalid(error, "Removal is incomplete. Finish removing this entry first.");
    if (game->nvapi < NVAPI_AUTO || game->nvapi > NVAPI_FORCED)
        return invalid(error, "Choose Automatic, Disabled or Forced NVAPI");
    if (game->wayland && game->fsr)
        return invalid(error, "Fullscreen FSR requires XWayland. Disable FSR or native Wayland.");
    if (game->fsr && game->wined3d)
        return invalid(error, "Fullscreen FSR requires Vulkan, not the WineD3D OpenGL fallback.");
    if (game->working_dir && *game->working_dir &&
        (!g_path_is_absolute(game->working_dir) || !g_file_test(game->working_dir, G_FILE_TEST_IS_DIR)))
        return invalid(error, "Choose an existing absolute working directory");
    if (game->umu_id && *game->umu_id) {
        if (!g_str_has_prefix(game->umu_id, "umu-"))
            return invalid(error, "An optional umu game ID must start with umu-");
        for (const char *p = game->umu_id; *p; p++)
            if (!g_ascii_isalnum(*p) && *p != '-' && *p != '_')
                return invalid(error, "The umu game ID contains an invalid character");
    }
    g_autoptr(Game) current = game_load(game->id, NULL);
    if (current && current->wow64 != game->wow64 && game_prefix_exists(game))
        return invalid(error, "Back up and reset this game's prefix before changing its WoW64 mode.");
    if (game->runner && !g_str_equal(game->runner, "GE-Proton")) {
        if (!g_path_is_absolute(game->runner))
            return invalid(error, "Use GE-Proton for latest, or select an absolute runner directory");
        g_autofree char *script = g_build_filename(game->runner, "proton", NULL);
        if (!g_file_test(script, G_FILE_TEST_IS_REGULAR))
            return invalid(error, "The selected runner directory has no proton launcher");
        g_autofree char *source = storage_read_tail(script, 512 * 1024, error);
        if (!source) return FALSE;
        if ((game->wow64 && !strstr(source, "PROTON_USE_WOW64")) ||
            (game->wayland && !strstr(source, "PROTON_ENABLE_WAYLAND") && !strstr(source, "PROTON_USE_WAYLAND")) ||
            (game->nvapi != NVAPI_AUTO && !strstr(source, "PROTON_DISABLE_NVAPI")) ||
            (game->fsr && !strstr(source, "GE-Proton")))
            return invalid(error, "This runner does not advertise the selected compatibility options. "
                                  "Use a current GE-Proton release or disable those options.");
    } else if (!game->runner) return invalid(error, "Choose a Proton runner");
    gint argc;
    g_auto(GStrv) argv = NULL;
    if (game->arguments && *game->arguments &&
        !g_shell_parse_argv(game->arguments, &argc, &argv, error)) return FALSE;
    return custom_environment(NULL, game->environment, error);
}

char *runner_preflight_report(void)
{
    g_autofree char *umu = g_find_program_in_path("umu-run");
    GModule *vulkan = g_module_open("libvulkan.so.1", G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
    g_autofree char *report = g_strdup_printf(
        "umu-run: %s\nWayland session: %s\nXWayland display: %s\nVulkan loader: %s\n\n"
        "Install games from local files or mounted discs. No game downloads are offered.\n"
        "umu may need internet to prepare or update the shared GE-Proton and Linux runtime. "
        "Prepare both beforehand for use without a connection.\n"
        "A Vulkan loader alone does not confirm GPU compatibility; check your distro's graphics drivers.",
        umu ? umu : "Missing — install umu-launcher", g_getenv("WAYLAND_DISPLAY") ? "Available" : "Not detected",
        g_getenv("DISPLAY") ? "Available" : "Not detected", vulkan ? "Available" : "Not found (WineD3D may work for older games)");
    if (vulkan) g_module_close(vulkan);
    return g_steal_pointer(&report);
}

GSubprocess *runner_start(const Game *game, const char *executable,
                          gboolean utility, GError **error)
{
    if (!runner_validate(game, error)) return NULL;
    if (!executable || !g_path_is_absolute(executable) || !g_file_test(executable, G_FILE_TEST_IS_REGULAR)) {
        invalid(error, "Select an existing local game or installer executable");
        return NULL;
    }
    g_autofree char *umu = g_find_program_in_path("umu-run");
    if (!umu) {
        invalid(error, "umu-run is missing. Install umu-launcher and prepare its runtime; open Runtime information for details.");
        return NULL;
    }
    if (game->wayland && !g_getenv("WAYLAND_DISPLAY")) {
        invalid(error, "No Wayland display was detected. Start a Wayland session or select XWayland in this game's settings.");
        return NULL;
    }
    if (!game->wayland && !g_getenv("DISPLAY")) {
        invalid(error, "No XWayland display was detected. Enable XWayland in your Wayland session or use native Wayland.");
        return NULL;
    }
    g_autofree char *prefix = game_prefix_path(game);
    g_autofree char *log = game_log_path(game, FALSE);
    g_autofree char *err_log = game_log_path(game, TRUE);
    g_autofree char *log_dir = g_path_get_dirname(log);
    g_autofree char *diagnostics = g_build_filename(log_dir, game->id, NULL);
    if (g_mkdir_with_parents(prefix, 0700) || g_mkdir_with_parents(diagnostics, 0700)) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "Cannot prepare game directories: %s", g_strerror(errno));
        return NULL;
    }
    g_autofree char *cwd = utility || !game->working_dir || !*game->working_dir ?
        g_path_get_dirname(executable) : g_strdup(game->working_dir);
    g_autoptr(GSubprocessLauncher) launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_NONE);
    g_subprocess_launcher_set_cwd(launcher, cwd);
    g_subprocess_launcher_set_stdout_file_path(launcher, log);
    g_subprocess_launcher_set_stderr_file_path(launcher, err_log);
    /* Clear aliases inherited from the shell so controls have predictable precedence. */
    const char *clear[] = {"PROTON_USE_WAYLAND", "PROTON_ENABLE_NVAPI", "PROTON_ADD_CONFIG",
        "STEAM_COMPAT_CONFIG", "PROTON_USE_WINED3D11", "WINE_GRAPHICS_DRIVER", "WINEARCH", NULL};
    for (guint i = 0; clear[i]; i++) g_subprocess_launcher_unsetenv(launcher, clear[i]);
    g_subprocess_launcher_setenv(launcher, "WINEPREFIX", prefix, TRUE);
    g_subprocess_launcher_setenv(launcher, "PROTONPATH", game->runner, TRUE);
    g_subprocess_launcher_setenv(launcher, "GAMEID", game->umu_id && *game->umu_id ? game->umu_id : "umu-default", TRUE);
    g_subprocess_launcher_setenv(launcher, "PROTON_VERB", "waitforexitandrun", TRUE);
    g_subprocess_launcher_unsetenv(launcher, "UMU_NO_PROTON");
    g_subprocess_launcher_setenv(launcher, "PROTON_ENABLE_WAYLAND", game->wayland ? "1" : "0", TRUE);
    g_subprocess_launcher_setenv(launcher, "PROTON_USE_WOW64", game->wow64 ? "1" : "0", TRUE);
    g_subprocess_launcher_setenv(launcher, "WINE_FULLSCREEN_FSR", game->fsr ? "1" : "0", TRUE);
    /* Automatic deliberately leaves the runner's own per-game defaults intact. */
    g_subprocess_launcher_unsetenv(launcher, "PROTON_DISABLE_NVAPI");
    g_subprocess_launcher_unsetenv(launcher, "PROTON_FORCE_NVAPI");
    if (game->nvapi == NVAPI_DISABLED) g_subprocess_launcher_setenv(launcher, "PROTON_DISABLE_NVAPI", "1", TRUE);
    if (game->nvapi == NVAPI_FORCED) g_subprocess_launcher_setenv(launcher, "PROTON_FORCE_NVAPI", "1", TRUE);
    g_subprocess_launcher_setenv(launcher, "PROTON_USE_WINED3D", game->wined3d ? "1" : "0", TRUE);
    g_subprocess_launcher_setenv(launcher, "PROTON_LOG_DIR", diagnostics, TRUE);
    g_subprocess_launcher_setenv(launcher, "PROTON_DEBUG_DIR", diagnostics, TRUE);
    g_subprocess_launcher_setenv(launcher, "PROTON_CRASH_REPORT_DIR", diagnostics, TRUE);
    if (!custom_environment(launcher, game->environment, error)) return NULL;
    gint argc = 0;
    g_auto(GStrv) parsed = NULL;
    if (!utility && game->arguments && *game->arguments &&
        !g_shell_parse_argv(game->arguments, &argc, &parsed, error)) return NULL;
    g_autoptr(GPtrArray) argv = g_ptr_array_new();
    g_ptr_array_add(argv, umu);
    g_ptr_array_add(argv, (gpointer) executable);
    for (int i = 0; i < argc; i++) g_ptr_array_add(argv, parsed[i]);
    g_ptr_array_add(argv, NULL);
    return g_subprocess_launcher_spawnv(launcher, (const gchar * const *) argv->pdata, error);
}

char *runner_result(GSubprocess *process)
{
    if (g_subprocess_get_if_signaled(process))
        return g_strdup_printf("The launcher was terminated by signal %d. Open Logs for details.",
                                g_subprocess_get_term_sig(process));
    if (!g_subprocess_get_successful(process))
        return g_strdup_printf("The game or installer exited with status %d. Open Logs for details.",
                                g_subprocess_get_exit_status(process));
    return NULL;
}

void runner_record_success(Game *game)
{
    g_autofree char *prefix = game_prefix_path(game);
    g_autofree char *file = g_build_filename(prefix, "version", NULL);
    g_autofree char *version = storage_read_tail(file, 256, NULL);
    if (version) g_strstrip(version);
    g_autofree char *resolved = NULL;
    if (g_path_is_absolute(game->runner)) resolved = g_strdup(game->runner);
    else if (version && *version && !strchr(version, '/') && !strchr(version, '\n')) {
        const char *folders[] = {"Steam/compatibilitytools.d", "umu/compatibilitytools", NULL};
        for (guint i = 0; folders[i]; i++) {
            g_autofree char *candidate = g_build_filename(g_get_user_data_dir(), folders[i], version, NULL);
            g_autofree char *script = g_build_filename(candidate, "proton", NULL);
            if (g_file_test(script, G_FILE_TEST_IS_REGULAR)) { resolved = g_strdup(candidate); break; }
        }
    }
    if (resolved && g_strcmp0(resolved, game->last_runner)) {
        g_free(game->previous_runner); game->previous_runner = g_strdup(game->last_runner);
        g_free(game->last_runner); game->last_runner = g_strdup(resolved);
    }
    if (version && *version) { g_free(game->last_version); game->last_version = g_strdup(version); }
}
