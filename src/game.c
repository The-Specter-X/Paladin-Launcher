#include "game.h"

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
    game->wayland = TRUE;
    game->wow64 = TRUE;
    return game;
}

void game_free(Game *game)
{
    if (!game) return;
    g_free(game->id); g_free(game->name); g_free(game->executable);
    g_free(game->installer); g_free(game->runner); g_free(game->arguments);
    g_free(game->environment); g_free(game);
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
    return g_build_filename(g_get_user_cache_dir(), APP_DIR, "logs", filename, NULL);
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
    g_key_file_set_boolean(key, "Game", "ManagedFiles", game->managed_files);
    g_key_file_set_boolean(key, "Game", "Wayland", game->wayland);
    g_key_file_set_boolean(key, "Game", "WoW64", game->wow64);
    g_key_file_set_boolean(key, "Game", "FSR", game->fsr);
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
    game->managed_files = read_boolean(key, "ManagedFiles", FALSE);
    game->wayland = read_boolean(key, "Wayland", TRUE);
    game->wow64 = read_boolean(key, "WoW64", TRUE);
    game->fsr = read_boolean(key, "FSR", FALSE);
    game->wined3d = read_boolean(key, "WineD3D", FALSE);
    game->desktop_shortcut = read_boolean(key, "DesktopShortcut", FALSE);
    game->ready = read_boolean(key, "Ready", FALSE);
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

static gboolean delete_tree(GFile *file, GError **error)
{
    GFileType type = g_file_query_file_type(file, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL);
    if (type == G_FILE_TYPE_UNKNOWN) return TRUE;
    if (type == G_FILE_TYPE_DIRECTORY) {
        g_autoptr(GFileEnumerator) items = g_file_enumerate_children(
            file, G_FILE_ATTRIBUTE_STANDARD_NAME, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
            NULL, error);
        if (!items) return FALSE;
        g_autoptr(GFileInfo) info = NULL;
        while ((info = g_file_enumerator_next_file(items, NULL, error))) {
            g_autoptr(GFile) child = g_file_get_child(file, g_file_info_get_name(info));
            if (!delete_tree(child, error)) return FALSE;
            g_clear_object(&info);
        }
        if (error && *error) return FALSE;
    }
    return g_file_delete(file, NULL, error);
}

gboolean game_remove(const Game *game, GError **error)
{
    g_return_val_if_fail(valid_id(game->id), FALSE);
    Game shortcut = *game;
    shortcut.ready = FALSE;
    if (!game_write_shortcuts(&shortcut, error)) return FALSE;
    g_autofree char *path = game_data_path(game);
    g_autoptr(GFile) data = g_file_new_for_path(path);
    if (!delete_tree(data, error)) return FALSE;
    g_autofree char *config = game_config_path(game);
    if (g_unlink(config) != 0 && errno != ENOENT) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Cannot remove game settings: %s", g_strerror(errno));
        return FALSE;
    }
    for (int i = 0; i < 2; i++) {
        g_autofree char *log = game_log_path(game, i == 1);
        g_unlink(log);
    }
    return TRUE;
}

static gboolean copy_tree(GFile *source, GFile *target, GCancellable *cancellable,
                          unsigned int depth, GError **error)
{
    if (depth > 40) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "Game directory nesting is too deep");
        return FALSE;
    }
    GFileType type = g_file_query_file_type(source, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                             cancellable);
    if (type == G_FILE_TYPE_SYMBOLIC_LINK) return TRUE; /* Do not copy outside the selected tree. */
    if (type == G_FILE_TYPE_REGULAR) {
        GFileType target_type = g_file_query_file_type(target,
            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable);
        if (target_type != G_FILE_TYPE_UNKNOWN && target_type != G_FILE_TYPE_REGULAR) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                                "The managed game folder contains an unexpected file or link");
            return FALSE;
        }
        return g_file_copy(source, target,
                           G_FILE_COPY_NOFOLLOW_SYMLINKS | G_FILE_COPY_OVERWRITE,
                           cancellable, NULL, NULL, error);
    }
    if (type != G_FILE_TYPE_DIRECTORY) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            "Selected game contains an unsupported file type");
        return FALSE;
    }
    GFileType existing = g_file_query_file_type(target, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                                 cancellable);
    if (existing != G_FILE_TYPE_UNKNOWN && existing != G_FILE_TYPE_DIRECTORY) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                            "The managed game folder contains an unexpected file or link");
        return FALSE;
    }
    if (!g_file_make_directory_with_parents(target, cancellable, error)) {
        if (!error || !g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_EXISTS)) return FALSE;
        g_clear_error(error);
    }
    g_autoptr(GFileEnumerator) items = g_file_enumerate_children(
        source, G_FILE_ATTRIBUTE_STANDARD_NAME, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
        cancellable, error);
    if (!items) return FALSE;
    g_autoptr(GFileInfo) info = NULL;
    while ((info = g_file_enumerator_next_file(items, cancellable, error))) {
        g_autoptr(GFile) from = g_file_get_child(source, g_file_info_get_name(info));
        g_autoptr(GFile) to = g_file_get_child(target, g_file_info_get_name(info));
        if (!copy_tree(from, to, cancellable, depth + 1, error)) return FALSE;
        g_clear_object(&info);
    }
    return !(error && *error);
}

gboolean game_copy_folder(const char *source, const char *destination,
                          GCancellable *cancellable, GError **error)
{
    g_autoptr(GFile) from = g_file_new_for_path(source);
    g_autoptr(GFile) to = g_file_new_for_path(destination);
    return copy_tree(from, to, cancellable, 0, error);
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
        g_str_has_prefix(base, "dxsetup") || g_str_has_prefix(base, "crash") ||
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

static void scan_exes(const Game *game, const char *dir, GPtrArray *found, int depth)
{
    if (depth > 7 || found->len >= 300) return;
    g_autoptr(GDir) entries = g_dir_open(dir, 0, NULL);
    if (!entries) return;
    const char *name;
    while ((name = g_dir_read_name(entries)) && found->len < 300) {
        g_autofree char *path = g_build_filename(dir, name, NULL);
        g_autoptr(GFile) file = g_file_new_for_path(path);
        GFileType type = g_file_query_file_type(file, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL);
        if (type == G_FILE_TYPE_DIRECTORY) scan_exes(game, path, found, depth + 1);
        else if (type == G_FILE_TYPE_REGULAR) {
            int score = score_exe(game, path);
            if (score >= 0) {
                Candidate *c = g_new0(Candidate, 1);
                c->path = g_strdup(path); c->score = score;
                g_ptr_array_add(found, c);
            }
        }
    }
}

static gint candidate_compare(gconstpointer a, gconstpointer b)
{
    const Candidate *left = *(Candidate * const *) a;
    const Candidate *right = *(Candidate * const *) b;
    if (left->score != right->score) return right->score - left->score;
    return g_strcmp0(left->path, right->path);
}

GPtrArray *game_find_executables(const Game *game)
{
    g_autofree char *prefix = game_prefix_path(game);
    g_autoptr(GPtrArray) found = g_ptr_array_new_with_free_func((GDestroyNotify) candidate_free);
    const char *folders[] = {"Program Files", "Program Files (x86)", NULL};
    for (int i = 0; folders[i]; i++) {
        g_autofree char *dir = g_build_filename(prefix, "drive_c", folders[i], NULL);
        scan_exes(game, dir, found, 0);
    }
    g_ptr_array_sort(found, candidate_compare);
    GPtrArray *result = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < found->len; i++) {
        Candidate *c = g_ptr_array_index(found, i);
        g_ptr_array_add(result, g_strdup(c->path));
    }
    return result;
}
