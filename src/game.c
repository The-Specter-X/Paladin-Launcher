#include "game.h"
#include "storage.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <string.h>

#define APP_DIR "paladin-launcher"
#define SHORTCUT_PREFIX "paladin-game-"

static gboolean valid_id(const char *id)
{
    if (!id || strlen(id) != 36) return FALSE;
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (id[i] != '-') return FALSE;
        } else if (!g_ascii_isxdigit(id[i])) return FALSE;
    }
    return TRUE;
}

static char *config_dir(void)
{
    return g_build_filename(g_get_user_config_dir(), APP_DIR, "games", NULL);
}

static char *data_dir(void)
{
    return g_build_filename(g_get_user_data_dir(), APP_DIR, "games", NULL);
}

Game *game_new(const char *name)
{
    Game *game = g_new0(Game, 1);
    game->id = g_uuid_string_random();
    game->name = g_strdup(name);
    game->runner = g_strdup("GE-Proton");
    game->arguments = g_strdup("");
    game->environment = g_strdup("");
    game->root = g_strdup("");
    game->working_dir = g_strdup("");
    game->umu_id = g_strdup("");
    game->last_runner = g_strdup("");
    game->previous_runner = g_strdup("");
    game->last_version = g_strdup("");
    game->last_backup = g_strdup("");
    game->wayland = TRUE;
    game->wow64 = TRUE;
    return game;
}

void game_free(Game *game)
{
    if (!game) return;
    g_free(game->id); g_free(game->name); g_free(game->executable);
    g_free(game->installer); g_free(game->runner); g_free(game->arguments);
    g_free(game->environment);
    g_free(game->root); g_free(game->working_dir); g_free(game->umu_id);
    g_free(game->last_runner); g_free(game->previous_runner);
    g_free(game->last_version); g_free(game->last_backup);
    g_free(game);
}

Game *game_copy(const Game *game)
{
    Game *copy = g_new(Game, 1);
    *copy = *game;
    copy->id = g_strdup(game->id);
    copy->name = g_strdup(game->name);
    copy->executable = g_strdup(game->executable);
    copy->installer = g_strdup(game->installer);
    copy->runner = g_strdup(game->runner);
    copy->arguments = g_strdup(game->arguments);
    copy->environment = g_strdup(game->environment);
    copy->root = g_strdup(game->root);
    copy->working_dir = g_strdup(game->working_dir);
    copy->umu_id = g_strdup(game->umu_id);
    copy->last_runner = g_strdup(game->last_runner);
    copy->previous_runner = g_strdup(game->previous_runner);
    copy->last_version = g_strdup(game->last_version);
    copy->last_backup = g_strdup(game->last_backup);
    return copy;
}

char *game_data_path(const Game *game)
{
    g_autofree char *base = data_dir();
    return g_build_filename(base, game->id, NULL);
}

char *game_prefix_path(const Game *game)
{
    g_autofree char *base = game_data_path(game);
    return g_build_filename(base, "prefix", NULL);
}

char *game_config_path(const Game *game)
{
    g_autofree char *base = config_dir();
    g_autofree char *filename = g_strconcat(game->id, ".ini", NULL);
    return g_build_filename(base, filename, NULL);
}

char *game_log_path(const Game *game, gboolean errors)
{
    g_autofree char *filename = g_strdup_printf("%s.%s.log", game->id,
                                                errors ? "err" : "out");
    return g_build_filename(g_get_user_state_dir(), APP_DIR, "logs", filename, NULL);
}

gboolean game_save(const Game *game, GError **error)
{
    g_return_val_if_fail(game && valid_id(game->id), FALSE);
    g_autofree char *path = game_config_path(game);
    g_autofree char *dir = g_path_get_dirname(path);
    if (g_mkdir_with_parents(dir, 0700) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Cannot create game settings directory: %s", g_strerror(errno));
        return FALSE;
    }

    g_autoptr(GKeyFile) key = g_key_file_new();
    g_key_file_set_string(key, "Game", "Name", game->name ? game->name : "");
    g_key_file_set_string(key, "Game", "Executable", game->executable ? game->executable : "");
    g_key_file_set_string(key, "Game", "Installer", game->installer ? game->installer : "");
    g_key_file_set_string(key, "Game", "Runner", game->runner ? game->runner : "GE-Proton");
    g_key_file_set_string(key, "Game", "Arguments", game->arguments ? game->arguments : "");
    g_key_file_set_string(key, "Game", "Environment", game->environment ? game->environment : "");
    g_key_file_set_string(key, "Game", "Root", game->root ? game->root : "");
    g_key_file_set_string(key, "Game", "WorkingDirectory", game->working_dir ? game->working_dir : "");
    g_key_file_set_string(key, "Game", "UmuId", game->umu_id ? game->umu_id : "");
    g_key_file_set_string(key, "Game", "LastRunner", game->last_runner ? game->last_runner : "");
    g_key_file_set_string(key, "Game", "PreviousRunner", game->previous_runner ? game->previous_runner : "");
    g_key_file_set_string(key, "Game", "LastVersion", game->last_version ? game->last_version : "");
    g_key_file_set_string(key, "Game", "LastBackup", game->last_backup ? game->last_backup : "");
    g_key_file_set_boolean(key, "Game", "DiscoveryPending", game->discover_pending);
    g_key_file_set_boolean(key, "Game", "Removing", game->removing);
    g_key_file_set_boolean(key, "Game", "ManagedFiles", game->managed_files);
    g_key_file_set_boolean(key, "Game", "Wayland", game->wayland);
    g_key_file_set_boolean(key, "Game", "WoW64", game->wow64);
    g_key_file_set_boolean(key, "Game", "FSR", game->fsr);
    g_key_file_set_integer(key, "Game", "NvapiMode", game->nvapi);
    g_key_file_set_boolean(key, "Game", "WineD3D", game->wined3d);
    g_key_file_set_boolean(key, "Game", "DesktopShortcut", game->desktop_shortcut);
    g_key_file_set_boolean(key, "Game", "Ready", game->ready);
    return g_key_file_save_to_file(key, path, error);
}

static char *read_string(GKeyFile *key, const char *name)
{
    char *value = g_key_file_get_string(key, "Game", name, NULL);
    return value ? value : g_strdup("");
}

static gboolean read_boolean(GKeyFile *key, const char *name, gboolean fallback)
{
    return g_key_file_has_key(key, "Game", name, NULL) ?
        g_key_file_get_boolean(key, "Game", name, NULL) : fallback;
}

Game *game_load(const char *id, GError **error)
{
    if (!valid_id(id)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid game ID");
        return NULL;
    }
    g_autofree char *base = config_dir();
    g_autofree char *filename = g_strconcat(id, ".ini", NULL);
    g_autofree char *path = g_build_filename(base, filename, NULL);
    g_autoptr(GKeyFile) key = g_key_file_new();
    if (!g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, error)) return NULL;
    Game *game = g_new0(Game, 1);
    game->id = g_strdup(id);
    game->name = read_string(key, "Name");
    game->executable = read_string(key, "Executable");
    game->installer = read_string(key, "Installer");
    game->runner = read_string(key, "Runner");
    game->arguments = read_string(key, "Arguments");
    game->environment = read_string(key, "Environment");
    game->root = read_string(key, "Root");
    game->working_dir = read_string(key, "WorkingDirectory");
    game->umu_id = read_string(key, "UmuId");
    game->last_runner = read_string(key, "LastRunner");
    game->previous_runner = read_string(key, "PreviousRunner");
    game->last_version = read_string(key, "LastVersion");
    game->last_backup = read_string(key, "LastBackup");
    game->discover_pending = read_boolean(key, "DiscoveryPending", FALSE);
    game->removing = read_boolean(key, "Removing", FALSE);
    game->managed_files = read_boolean(key, "ManagedFiles", FALSE);
    game->wayland = read_boolean(key, "Wayland", TRUE);
    game->wow64 = read_boolean(key, "WoW64", TRUE);
    game->fsr = read_boolean(key, "FSR", FALSE);
    game->nvapi = g_key_file_has_key(key, "Game", "NvapiMode", NULL) ?
        g_key_file_get_integer(key, "Game", "NvapiMode", NULL) :
        (read_boolean(key, "NVAPI", FALSE) ? NVAPI_FORCED : NVAPI_AUTO);
    game->wined3d = read_boolean(key, "WineD3D", FALSE);
    game->desktop_shortcut = read_boolean(key, "DesktopShortcut", FALSE);
    game->ready = read_boolean(key, "Ready", FALSE);
    if (!*game->root && *game->executable) {
        g_free(game->root); game->root = g_path_get_dirname(game->executable);
    }
    if (!g_key_file_has_key(key, "Game", "DiscoveryPending", NULL))
        game->discover_pending = !game->ready && *game->installer;
    if (!*game->name) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Game has no title");
        game_free(game);
        return NULL;
    }
    if (!*game->runner) {
        g_free(game->runner);
        game->runner = g_strdup("GE-Proton");
    }
    return game;
}

static gint by_name(gconstpointer a, gconstpointer b)
{
    const Game *left = *(Game * const *) a;
    const Game *right = *(Game * const *) b;
    return g_utf8_collate(left->name, right->name);
}

GPtrArray *game_load_all(void)
{
    GPtrArray *games = g_ptr_array_new_with_free_func((GDestroyNotify) game_free);
    g_autofree char *dir = config_dir();
    g_autoptr(GDir) entries = g_dir_open(dir, 0, NULL);
    if (!entries) return games;
    const char *filename;
    while ((filename = g_dir_read_name(entries))) {
        if (!g_str_has_suffix(filename, ".ini")) continue;
        g_autofree char *id = g_strndup(filename, strlen(filename) - 4);
        g_autoptr(GError) error = NULL;
        Game *game = game_load(id, &error);
        if (game) g_ptr_array_add(games, game);
        else g_warning("Skipping game %s: %s", id, error->message);
    }
    g_ptr_array_sort(games, by_name);
    return games;
}

static char *shortcut_path(const Game *game, gboolean desktop)
{
    const char *base = desktop ? g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP) : NULL;
    g_autofree char *applications = NULL;
    if (!desktop) {
        applications = g_build_filename(g_get_user_data_dir(), "applications", NULL);
        base = applications;
    }
    if (!base) return NULL;
    g_autofree char *filename = g_strdup_printf(SHORTCUT_PREFIX "%s.desktop", game->id);
    return g_build_filename(base, filename, NULL);
}

gboolean game_write_shortcuts(const Game *game, GError **error)
{
    g_return_val_if_fail(valid_id(game->id), FALSE);
    g_autoptr(GKeyFile) key = g_key_file_new();
    g_key_file_set_string(key, "Desktop Entry", "Type", "Application");
    g_key_file_set_string(key, "Desktop Entry", "Name", game->name);
    g_key_file_set_string(key, "Desktop Entry", "Comment", "Play with Paladin Launcher");
    g_key_file_set_string(key, "Desktop Entry", "Icon", "applications-games");
    g_key_file_set_string(key, "Desktop Entry", "Categories", "Game;");
    g_autofree char *command = g_strdup_printf("paladin-launcher --play %s", game->id);
    g_key_file_set_string(key, "Desktop Entry", "Exec", command);
    g_autofree char *text = g_key_file_to_data(key, NULL, error);
    if (!text) return FALSE;

    for (int i = 0; i < 2; i++) {
        g_autofree char *path = shortcut_path(game, i == 1);
        if (!path) continue;
        if (!game->ready || (i == 1 && !game->desktop_shortcut)) {
            if (g_unlink(path) != 0 && errno != ENOENT) {
                g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                            "Cannot remove shortcut: %s", g_strerror(errno));
                return FALSE;
            }
            continue;
        }
        g_autofree char *dir = g_path_get_dirname(path);
        if (g_mkdir_with_parents(dir, 0700) != 0) {
            g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                        "Cannot create shortcut directory: %s", g_strerror(errno));
            return FALSE;
        }
        if (!g_file_set_contents(path, text, -1, error)) return FALSE;
        if (i == 1 && g_chmod(path, 0755) != 0) {
            g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                        "Cannot make desktop shortcut executable: %s", g_strerror(errno));
            return FALSE;
        }
    }
    return TRUE;
}

gboolean game_remove_full(const Game *game, GCancellable *cancel, GError **error)
{
    g_return_val_if_fail(valid_id(game->id), FALSE);
    g_autoptr(Game) pending = game_copy(game);
    pending->removing = TRUE;
    pending->ready = FALSE;
    if (!game_save(pending, error)) return FALSE;
    g_autofree char *path = game_data_path(game);
    if (!storage_remove(path, cancel, error)) return FALSE;
    /* Keep the tombstone until data, logs and shortcuts have all been removed. */
    for (int i = 0; i < 2; i++) {
        g_autofree char *log = game_log_path(game, i == 1);
        if (!storage_remove(log, cancel, error)) return FALSE;
        g_autofree char *base = g_path_get_basename(log);
        g_autofree char *legacy = g_build_filename(g_get_user_cache_dir(), APP_DIR, "logs", base, NULL);
        if (!storage_remove(legacy, cancel, error)) return FALSE;
    }
    g_autofree char *diagnostics = g_build_filename(g_get_user_state_dir(), APP_DIR, "logs", game->id, NULL);
    if (!storage_remove(diagnostics, cancel, error)) return FALSE;
    if (!game_write_shortcuts(pending, error)) return FALSE;
    g_autofree char *config = game_config_path(game);
    if (g_unlink(config) != 0 && errno != ENOENT) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Cannot remove game settings: %s", g_strerror(errno));
        return FALSE;
    }
    return TRUE;
}

gboolean game_remove(const Game *game, GError **error)
{
    return game_remove_full(game, NULL, error);
}

gboolean game_copy_folder(const char *source, const char *destination,
                          GCancellable *cancel, GError **error)
{
    return storage_copy(source, destination, cancel, error);
}

gboolean game_import(Game *game, GCancellable *cancel, GError **error)
{
    g_autofree char *root = storage_resolve(game->root, error);
    if (!root) return FALSE;
    g_autofree char *exe = storage_resolve(game->executable, error);
    if (!exe) return FALSE;
    g_autoptr(GFile) from = g_file_new_for_path(root);
    g_autoptr(GFile) executable = g_file_new_for_path(exe);
    g_autofree char *relative = g_file_get_relative_path(from, executable);
    if (!relative || !g_file_test(exe, G_FILE_TEST_IS_REGULAR)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "The game EXE must be a file inside the selected game folder");
        return FALSE;
    }
    g_autofree char *data = game_data_path(game);
    g_autofree char *target = g_build_filename(data, "files", NULL);
    if (!storage_copy(root, target, cancel, error)) return FALSE;
    g_autofree char *copied = g_build_filename(target, relative, NULL);
    if (!g_file_test(copied, G_FILE_TEST_IS_REGULAR)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "The copied game EXE is missing");
        return FALSE;
    }
    g_free(game->executable); game->executable = g_strdup(copied);
    g_free(game->root); game->root = g_strdup(target);
    g_free(game->working_dir); game->working_dir = g_strdup(target);
    game->ready = TRUE;
    return game_save(game, error) && game_write_shortcuts(game, error);
}

gboolean game_prefix_exists(const Game *game)
{
    g_autofree char *prefix = game_prefix_path(game);
    g_autofree char *registry = g_build_filename(prefix, "system.reg", NULL);
    g_autofree char *drive = g_build_filename(prefix, "drive_c", NULL);
    return g_file_test(registry, G_FILE_TEST_EXISTS) || g_file_test(drive, G_FILE_TEST_IS_DIR);
}

gboolean game_reset_prefix(Game *game, GCancellable *cancel, GError **error)
{
    if (cancel && g_cancellable_set_error_if_cancelled(cancel, error)) return FALSE;
    g_autofree char *prefix = game_prefix_path(game);
    g_autofree char *data = game_data_path(game);
    g_autofree char *id = g_uuid_string_random();
    g_autofree char *backup = g_build_filename(data, "backups", id, NULL);
    if (g_mkdir_with_parents(backup, 0700) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "Cannot create prefix backup: %s", g_strerror(errno));
        return FALSE;
    }
    g_autofree char *config = game_config_path(game);
    g_autofree char *settings = g_build_filename(backup, "game.ini", NULL);
    g_autoptr(GFile) source = g_file_new_for_path(config);
    g_autoptr(GFile) dest = g_file_new_for_path(settings);
    if (!g_file_copy(source, dest, G_FILE_COPY_NONE, cancel, NULL, NULL, error)) return FALSE;
    g_autofree char *saved = g_build_filename(backup, "prefix", NULL);
    if (g_rename(prefix, saved) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "Cannot back up the prefix: %s", g_strerror(errno));
        return FALSE;
    }
    g_free(game->last_backup); game->last_backup = g_strdup(backup);
    g_autoptr(GFile) prefix_file = g_file_new_for_path(prefix);
    g_autoptr(GFile) exe = g_file_new_for_path(game->executable && *game->executable ? game->executable : prefix);
    if (g_file_equal(exe, prefix_file) || g_file_has_prefix(exe, prefix_file)) {
        game->ready = FALSE;
        g_free(game->root); game->root = g_strdup("");
        g_free(game->executable); game->executable = g_strdup("");
        g_free(game->working_dir); game->working_dir = g_strdup("");
    }
    game->discover_pending = FALSE;
    if (!game_save(game, error)) {
        /* The old metadata is still authoritative; put its prefix back. */
        if (g_rename(saved, prefix) != 0)
            g_warning("Prefix preserved at %s; rollback failed: %s", saved, g_strerror(errno));
        return FALSE;
    }
    return game_write_shortcuts(game, error);
}

typedef struct { char *path; int score; } Candidate;
static void candidate_free(Candidate *c) { g_free(c->path); g_free(c); }

static int score_exe(const Game *game, const char *path)
{
    g_autofree char *basename = g_path_get_basename(path);
    g_autofree char *base = g_ascii_strdown(basename, -1);
    if (!g_str_has_suffix(base, ".exe") ||
        g_str_has_prefix(base, "unins") || g_str_has_prefix(base, "setup") ||
        g_str_has_prefix(base, "install") || g_str_has_prefix(base, "vcredist") ||
        g_str_has_prefix(base, "dxsetup") || g_str_has_prefix(base, "crashreport") ||
        g_str_has_prefix(base, "unitycrash") || g_str_has_prefix(base, "redist")) return -1;
    g_autofree char *title = g_ascii_strdown(game->name, -1);
    g_autoptr(GString) simple_title = g_string_new("");
    g_autoptr(GString) simple_base = g_string_new("");
    for (const char *p = title; *p; p++) if (g_ascii_isalnum(*p)) g_string_append_c(simple_title, *p);
    for (const char *p = base; *p; p++) if (g_ascii_isalnum(*p)) g_string_append_c(simple_base, *p);
    int score = 10;
    if (simple_title->len > 2 && strstr(simple_base->str, simple_title->str)) score += 100;
    if (strstr(base, "launcher")) score += 5;
    if (strstr(path, "Program Files")) score += 5;
    if (strstr(base, "helper") || strstr(base, "config") || strstr(base, "updater")) score -= 25;
    return score;
}

static gboolean scan_exes(const Game *game, const char *dir, GPtrArray *found,
                           guint depth, guint *visited, GCancellable *cancel, GError **error)
{
    if (cancel && g_cancellable_set_error_if_cancelled(cancel, error)) return FALSE;
    if (depth > 16 || found->len >= 1000 || *visited >= 100000) return TRUE;
    g_autoptr(GFile) directory = g_file_new_for_path(dir);
    g_autoptr(GFileEnumerator) entries = g_file_enumerate_children(directory,
        G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel, error);
    if (!entries) return FALSE;
    g_autoptr(GFileInfo) item = NULL;
    while ((item = g_file_enumerator_next_file(entries, cancel, error))) {
        (*visited)++;
        const char *name = g_file_info_get_name(item);
        g_autofree char *path = g_build_filename(dir, name, NULL);
        GFileType type = g_file_info_get_file_type(item);
        if (type == G_FILE_TYPE_DIRECTORY) {
            /* Ignore Windows itself and shared installer caches, not publisher folders. */
            if (!(depth == 0 && (g_ascii_strcasecmp(name, "windows") == 0 ||
                                g_ascii_strcasecmp(name, "ProgramData") == 0)))
                if (!scan_exes(game, path, found, depth + 1, visited, cancel, error)) return FALSE;
        } else if (type == G_FILE_TYPE_REGULAR) {
            int score = score_exe(game, path);
            if (score >= 0) {
                Candidate *c = g_new0(Candidate, 1);
                c->path = g_strdup(path); c->score = score;
                g_ptr_array_add(found, c);
            }
        }
        g_clear_object(&item);
        if (found->len >= 1000 || *visited >= 100000) break;
    }
    return !(error && *error);
}

static gint candidate_compare(gconstpointer a, gconstpointer b)
{
    const Candidate *left = *(Candidate * const *) a;
    const Candidate *right = *(Candidate * const *) b;
    if (left->score != right->score) return right->score - left->score;
    return g_strcmp0(left->path, right->path);
}

GPtrArray *game_find_executables_full(const Game *game, GCancellable *cancel, GError **error)
{
    g_autofree char *prefix = game_prefix_path(game);
    g_autofree char *drive = g_build_filename(prefix, "drive_c", NULL);
    g_autoptr(GPtrArray) found = g_ptr_array_new_with_free_func((GDestroyNotify) candidate_free);
    guint visited = 0;
    if (!scan_exes(game, drive, found, 0, &visited, cancel, error)) return NULL;
    g_ptr_array_sort(found, candidate_compare);
    GPtrArray *result = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < found->len; i++) {
        Candidate *c = g_ptr_array_index(found, i);
        g_ptr_array_add(result, g_strdup(c->path));
    }
    return result;
}

GPtrArray *game_find_executables(const Game *game)
{
    GPtrArray *found = game_find_executables_full(game, NULL, NULL);
    return found ? found : g_ptr_array_new_with_free_func(g_free);
}
