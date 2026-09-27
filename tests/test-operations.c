#include "operations.h"
#include "runner.h"
#include "storage.h"
#include <glib/gstdio.h>
#include <string.h>

static char *root, *exe;
static guint completions;
static gboolean was_cancelled;
static char *last_error;

static void completed(Operations *ops, const char *id, int kind,
                      const char *message, gboolean cancelled, gpointer unused)
{
    (void) ops; (void) id; (void) kind; (void) unused;
    completions++;
    was_cancelled = cancelled;
    g_free(last_error); last_error = g_strdup(message);
}

static Game *saved_game(const char *name, const char *mode)
{
    Game *game = game_new(name);
    game->executable = g_strdup(exe);
    game->ready = TRUE;
    g_free(game->environment); game->environment = g_strdup_printf("TEST_MODE=%s", mode);
    g_assert_true(game_save(game, NULL));
    return game;
}

static void wait_idle(Operations *ops, const char *id)
{
    gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (operations_busy(ops, id) && g_get_monotonic_time() < deadline) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    g_assert_false(operations_busy(ops, id));
}

static void concurrent_and_cancel(void)
{
    g_autoptr(Operations) ops = operations_new(NULL);
    g_signal_connect(ops, "completed", G_CALLBACK(completed), NULL);
    g_autoptr(Game) a = saved_game("Slow", "slow");
    g_autoptr(Game) b = saved_game("Fast", "ok");
    g_autoptr(GError) error = NULL;
    g_assert_true(operations_run(ops, a->id, NULL, OP_PLAY, &error));
    g_assert_true(operations_run(ops, b->id, NULL, OP_PLAY, &error));
    g_assert_false(operations_start(ops, a->id, OP_REMOVE, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_BUSY); g_clear_error(&error);
    g_assert_false(operations_save_settings(ops, a, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_BUSY); g_clear_error(&error);
    wait_idle(ops, b->id);
    g_assert_true(operations_busy(ops, a->id));
    operations_stop(ops, a->id);
    g_assert_true(operations_busy(ops, a->id));
    wait_idle(ops, a->id);
    g_assert_true(was_cancelled);
    g_assert_null(last_error);
    g_assert_true(operations_save_settings(ops, a, &error));
    g_assert_no_error(error);
}

static void failures_and_stale_settings(void)
{
    g_autoptr(Operations) ops = operations_new(NULL);
    g_signal_connect(ops, "completed", G_CALLBACK(completed), NULL);
    g_autoptr(Game) a = saved_game("Failure", "fail");
    g_autoptr(GError) spawn_error = NULL;
    g_assert_false(operations_run(ops, a->id, "/missing/setup.exe", OP_INSTALL, &spawn_error));
    g_assert_nonnull(spawn_error);
    g_autoptr(Game) unstarted = game_load(a->id, NULL);
    g_assert_false(unstarted->discover_pending);
    g_assert_true(operations_run(ops, a->id, NULL, OP_PLAY, NULL));
    wait_idle(ops, a->id);
    g_assert_nonnull(strstr(last_error, "status 7"));
    g_assert_false(was_cancelled);
    g_free(a->environment); a->environment = g_strdup("TEST_MODE=signal");
    g_assert_true(game_save(a, NULL));
    g_assert_true(operations_run(ops, a->id, NULL, OP_PLAY, NULL));
    wait_idle(ops, a->id);
    g_assert_nonnull(strstr(last_error, "signal"));

    g_autoptr(Game) draft = game_copy(a);
    g_free(a->last_version); a->last_version = g_strdup("completed-while-dialog-open");
    a->discover_pending = TRUE;
    g_assert_true(game_save(a, NULL));
    g_free(draft->name); draft->name = g_strdup("Edited title");
    g_assert_true(operations_save_settings(ops, draft, NULL));
    g_autoptr(Game) saved = game_load(a->id, NULL);
    g_assert_cmpstr(saved->last_version, ==, "completed-while-dialog-open");
    g_assert_true(saved->discover_pending);
    g_assert_cmpstr(saved->name, ==, "Edited title");
}

static void architecture_and_validation(void)
{
    g_autoptr(Game) game = saved_game("Validation", "ok");
    g_autoptr(GError) error = NULL;
    game->fsr = TRUE;
    g_assert_false(runner_validate(game, &error)); g_clear_error(&error);
    game->wayland = FALSE; game->wined3d = TRUE;
    g_assert_false(runner_validate(game, &error)); g_clear_error(&error);
    game->fsr = FALSE;
    g_autofree char *prefix = game_prefix_path(game);
    g_autofree char *drive = g_build_filename(prefix, "drive_c", NULL);
    g_assert_cmpint(g_mkdir_with_parents(drive, 0700), ==, 0);
    game->wow64 = FALSE;
    g_assert_false(runner_validate(game, &error)); g_clear_error(&error);
    game->wow64 = TRUE;
    const char *bad[] = {"PROTON_FORCE_NVAPI=1", "PROTON_USE_WAYLAND=1", "XDG_DATA_HOME=/tmp", "BAD-NAME=x", "missing-equals"};
    for (guint i = 0; i < G_N_ELEMENTS(bad); i++) {
        g_free(game->environment); game->environment = g_strdup(bad[i]);
        g_assert_false(runner_validate(game, &error)); g_assert_nonnull(error); g_clear_error(&error);
    }
    g_free(game->environment); game->environment = g_strdup("");
    g_free(game->arguments); game->arguments = g_strdup("'unterminated");
    g_assert_false(runner_validate(game, &error)); g_clear_error(&error);
    g_free(game->arguments); game->arguments = g_strdup("");
    g_assert_true(game_reset_prefix(game, NULL, &error));
    g_assert_no_error(error);
    game->wow64 = FALSE;
    g_assert_true(runner_validate(game, &error));
    g_autofree char *backup = g_build_filename(game->last_backup, "prefix", "drive_c", NULL);
    g_assert_true(g_file_test(backup, G_FILE_TEST_IS_DIR));
}

int main(int argc, char **argv)
{
    root = g_dir_make_tmp("paladin-operations-XXXXXX", NULL);
    const char *vars[] = {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"};
    for (guint i = 0; i < G_N_ELEMENTS(vars); i++) {
        g_autofree char *dir = g_build_filename(root, vars[i], NULL);
        g_setenv(vars[i], dir, TRUE);
    }
    g_setenv("WAYLAND_DISPLAY", "fake", TRUE);
    g_autofree char *script = g_build_filename(root, "umu-run", NULL);
    g_assert_true(g_file_set_contents(script,
        "#!/bin/sh\ncase \"$TEST_MODE\" in slow) exec sleep 20;; fail) exit 7;; signal) kill -TERM $$;; esac\nexit 0\n", -1, NULL));
    g_assert_cmpint(g_chmod(script, 0700), ==, 0);
    g_autofree char *path = g_strconcat(root, ":", g_getenv("PATH"), NULL);
    g_setenv("PATH", path, TRUE);
    exe = g_build_filename(root, "game.exe", NULL);
    g_assert_true(g_file_set_contents(exe, "", 0, NULL));
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/operations/concurrent-and-cancel", concurrent_and_cancel);
    g_test_add_func("/operations/failure-and-stale-dialog", failures_and_stale_settings);
    g_test_add_func("/operations/validation-and-reset", architecture_and_validation);
    int result = g_test_run();
    g_assert_cmpuint(completions, ==, 4);
    g_assert_true(storage_remove(root, NULL, NULL));
    g_free(root); g_free(exe); g_free(last_error);
    return result;
}
