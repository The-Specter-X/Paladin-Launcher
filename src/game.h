#pragma once

#include <gio/gio.h>

typedef enum { NVAPI_AUTO, NVAPI_DISABLED, NVAPI_FORCED } NvapiMode;

typedef struct {
    char *id;
    char *name;
    char *executable;
    char *installer;
    char *runner;
    char *arguments;
    char *environment;
    char *root;
    char *working_dir;
    char *umu_id;
    char *last_runner;
    char *previous_runner;
    char *last_version;
    char *last_backup;
    gboolean managed_files;
    gboolean wayland;
    gboolean wow64;
    gboolean fsr;
    NvapiMode nvapi;
    gboolean wined3d;
    gboolean desktop_shortcut;
    gboolean ready;
    gboolean discover_pending;
    gboolean removing;
} Game;

Game *game_new(const char *name);
void game_free(Game *game);
Game *game_copy(const Game *game);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(Game, game_free)

char *game_data_path(const Game *game);
char *game_prefix_path(const Game *game);
char *game_config_path(const Game *game);
char *game_log_path(const Game *game, gboolean errors);
gboolean game_save(const Game *game, GError **error);
Game *game_load(const char *id, GError **error);
GPtrArray *game_load_all(void); /* Game*, owned by array */
gboolean game_remove(const Game *game, GError **error);
gboolean game_write_shortcuts(const Game *game, GError **error);

gboolean game_remove_full(const Game *game, GCancellable *cancel, GError **error);
gboolean game_import(Game *game, GCancellable *cancel, GError **error);
gboolean game_reset_prefix(Game *game, GCancellable *cancel, GError **error);
gboolean game_prefix_exists(const Game *game);

/* Copying rejects links explicitly rather than silently skipping files. */
gboolean game_copy_folder(const char *source, const char *destination,
                          GCancellable *cancellable, GError **error);
/* Candidate executable paths sorted with the most likely first. */
GPtrArray *game_find_executables(const Game *game); /* char*, owned by array */

GPtrArray *game_find_executables_full(const Game *game, GCancellable *cancel, GError **error);
