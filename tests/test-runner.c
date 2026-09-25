#include "game.h"
#include "runner.h"
#include <glib/gstdio.h>
#include <string.h>

static char *root;

static void runner_isolates_launches(void)
{
    g_autofree char *bin = g_build_filename(root, "bin", NULL);
    g_assert_cmpint(g_mkdir_with_parents(bin, 0700), ==, 0);
    g_autofree char *fake = g_build_filename(bin, "umu-run", NULL);
    const char *script = "#!/bin/sh\n"
        "printf 'prefix=%s\\nrunner=%s\\nwayland=%s\\nwow64=%s\\nnvapi=%s\\ncustom=%s\\ncount=%s\\nthird=%s\\n' "
        "\"$WINEPREFIX\" \"$PROTONPATH\" \"$PROTON_ENABLE_WAYLAND\" "
        "\"$PROTON_USE_WOW64\" \"$PROTON_ENABLE_NVAPI\" \"$CUSTOM_FLAG\" \"$#\" \"$3\"\n";
    g_assert_true(g_file_set_contents(fake, script, -1, NULL));
    g_assert_cmpint(g_chmod(fake, 0755), ==, 0);
    g_autofree char *old_path = g_strdup(g_getenv("PATH"));
    g_autofree char *new_path = g_strconcat(bin, ":", old_path ? old_path : "", NULL);
    g_setenv("PATH", new_path, TRUE);
    g_autofree char *exe = g_build_filename(root, "My Game.exe", NULL);
    g_assert_true(g_file_set_contents(exe, "", 0, NULL));

    g_autoptr(Game) first = game_new("First");
    g_free(first->arguments);
    first->arguments = g_strdup("--foo 'two words'");
    g_free(first->environment);
    first->environment = g_strdup("CUSTOM_FLAG=one");
    first->nvapi = TRUE;
    g_autoptr(Game) second = game_new("Second");
    second->wayland = FALSE;
    second->wow64 = FALSE;
    g_autoptr(GError) error = NULL;
    g_autoptr(GSubprocess) a = runner_start(first, exe, FALSE, &error);
    g_assert_no_error(error);
    g_assert_nonnull(a);
    g_autoptr(GSubprocess) b = runner_start(second, exe, FALSE, &error);
    g_assert_no_error(error);
    g_assert_nonnull(b);
    g_assert_true(g_subprocess_wait_check(a, NULL, &error));
    g_assert_true(g_subprocess_wait_check(b, NULL, &error));
    g_autofree char *log_a = game_log_path(first, FALSE);
    g_autofree char *log_b = game_log_path(second, FALSE);
    g_autofree char *output_a = NULL, *output_b = NULL;
    g_assert_true(g_file_get_contents(log_a, &output_a, NULL, &error));
    g_assert_true(g_file_get_contents(log_b, &output_b, NULL, &error));
    g_autofree char *prefix_a = game_prefix_path(first);
    g_autofree char *prefix_b = game_prefix_path(second);
    g_autofree char *expected_a = g_strdup_printf("prefix=%s\n", prefix_a);
    g_autofree char *expected_b = g_strdup_printf("prefix=%s\n", prefix_b);
    g_assert_nonnull(strstr(output_a, expected_a));
    g_assert_nonnull(strstr(output_b, expected_b));
    g_assert_nonnull(strstr(output_a, "runner=GE-Proton\nwayland=1\nwow64=1\nnvapi=1\ncustom=one\ncount=3\nthird=two words"));
    g_assert_nonnull(strstr(output_b, "wayland=0\nwow64=0\nnvapi=0\ncustom=\ncount=1\n"));
}

static void rejects_reserved_environment(void)
{
    g_autoptr(Game) game = game_new("Invalid settings");
    g_free(game->environment);
    game->environment = g_strdup("WINEPREFIX=/tmp/other-game");
    g_autofree char *exe = g_build_filename(root, "My Game.exe", NULL);
    g_autoptr(GError) error = NULL;
    g_autoptr(GSubprocess) process = runner_start(game, exe, FALSE, &error);
    g_assert_null(process);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
}

int main(int argc, char **argv)
{
    root = g_dir_make_tmp("paladin-runner-tests-XXXXXX", NULL);
    g_assert_nonnull(root);
    g_autofree char *data = g_build_filename(root, "data", NULL);
    g_autofree char *config = g_build_filename(root, "config", NULL);
    g_autofree char *cache = g_build_filename(root, "cache", NULL);
    g_setenv("XDG_DATA_HOME", data, TRUE);
    g_setenv("XDG_CONFIG_HOME", config, TRUE);
    g_setenv("XDG_CACHE_HOME", cache, TRUE);
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/runner/per-game-environment", runner_isolates_launches);
    g_test_add_func("/runner/reserved-variables", rejects_reserved_environment);
    return g_test_run();
}
