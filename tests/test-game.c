#include "game.h"
#include "storage.h"
#include <glib/gstdio.h>
#include <string.h>

static char *root;

static void paths_are_separate(void)
{
    g_autoptr(Game) first = game_new("One");
    g_autoptr(Game) second = game_new("Two");
    g_autofree char *a = game_prefix_path(first);
    g_autofree char *b = game_prefix_path(second);
    g_assert_cmpstr(a, !=, b);
    g_assert_true(g_str_has_prefix(a, root));
    g_assert_true(g_str_has_prefix(b, root));
}

static void save_load_and_shortcut(void)
{
    g_autoptr(Game) game = game_new("Game; with spaces");
    game->executable = g_strdup("/tmp/game with spaces.exe");
    g_free(game->arguments); game->arguments = g_strdup("--windowed 'two words'");
    game->ready = TRUE;
    game->nvapi = NVAPI_FORCED;
    g_autoptr(GError) error = NULL;
    g_assert_true(game_save(game, &error));
    g_assert_no_error(error);
    g_autoptr(Game) loaded = game_load(game->id, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(loaded->name, ==, game->name);
    g_assert_true(loaded->wow64);
    g_assert_true(loaded->wayland);
    g_assert_cmpint(loaded->nvapi, ==, NVAPI_FORCED);
    g_assert_cmpstr(loaded->arguments, ==, game->arguments);
    g_assert_true(game_write_shortcuts(loaded, &error));
    g_assert_no_error(error);
    g_autofree char *shortcut = g_strdup_printf("%s/applications/paladin-game-%s.desktop",
                                               g_get_user_data_dir(), game->id);
    g_autoptr(GKeyFile) desktop = g_key_file_new();
    g_assert_true(g_key_file_load_from_file(desktop, shortcut, G_KEY_FILE_NONE, &error));
    g_assert_no_error(error);
    g_autofree char *exec = g_key_file_get_string(desktop, "Desktop Entry", "Exec", NULL);
    g_autofree char *expected = g_strdup_printf("paladin-launcher --play %s", game->id);
    g_assert_cmpstr(exec, ==, expected);
    g_assert_true(game_remove(game, &error));
    g_assert_no_error(error);
    g_assert_false(g_file_test(shortcut, G_FILE_TEST_EXISTS));
}

static void deletion_never_follows_links(void)
{
    g_autoptr(Game) game = game_new("Safe removal");
    g_autoptr(GError) error = NULL;
    g_assert_true(game_save(game, &error));
    g_autofree char *prefix = game_prefix_path(game);
    g_assert_cmpint(g_mkdir_with_parents(prefix, 0700), ==, 0);
    g_autofree char *outside = g_build_filename(root, "outside.txt", NULL);
    g_assert_true(g_file_set_contents(outside, "preserve", -1, &error));
    g_autofree char *link = g_build_filename(prefix, "outside-link", NULL);
    g_autoptr(GFile) shortcut = g_file_new_for_path(link);
    g_assert_true(g_file_make_symbolic_link(shortcut, outside, NULL, &error));
    g_assert_true(game_remove(game, &error));
    g_assert_no_error(error);
    g_assert_true(g_file_test(outside, G_FILE_TEST_EXISTS));
}

static void candidate_selection(void)
{
    g_autoptr(Game) game = game_new("Space Quest");
    g_autofree char *prefix = game_prefix_path(game);
    g_autofree char *folder = g_build_filename(prefix, "drive_c", "Program Files",
                                               "Space Quest", NULL);
    g_assert_cmpint(g_mkdir_with_parents(folder, 0700), ==, 0);
    g_autofree char *wanted = g_build_filename(folder, "SpaceQuest.exe", NULL);
    g_autofree char *unwanted = g_build_filename(folder, "uninstall.exe", NULL);
    g_assert_true(g_file_set_contents(wanted, "", 0, NULL));
    g_assert_true(g_file_set_contents(unwanted, "", 0, NULL));
    g_autoptr(GPtrArray) candidates = game_find_executables(game);
    g_assert_cmpuint(candidates->len, ==, 1);
    g_assert_cmpstr(g_ptr_array_index(candidates, 0), ==, wanted);
    g_autoptr(GError) error = NULL;
    g_assert_true(game_remove(game, &error));
    g_assert_no_error(error);
}

static void copy_guards_and_nested_root(void)
{
    g_autofree char *source = g_build_filename(root, "source", NULL);
    g_autofree char *bin = g_build_filename(source, "bin", NULL);
    g_assert_cmpint(g_mkdir_with_parents(bin, 0700), ==, 0);
    g_autofree char *exe = g_build_filename(bin, "game.exe", NULL);
    g_autofree char *asset = g_build_filename(source, "assets.dat", NULL);
    g_assert_true(g_file_set_contents(exe, "exe", -1, NULL));
    g_assert_true(g_file_set_contents(asset, "assets", -1, NULL));
    g_autoptr(GError) error = NULL;
    g_assert_false(storage_copy(source, source, NULL, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT); g_clear_error(&error);
    g_autofree char *descendant = g_build_filename(source, "copy", NULL);
    g_assert_false(storage_copy(source, descendant, NULL, &error)); g_clear_error(&error);
    g_assert_false(g_file_test(descendant, G_FILE_TEST_EXISTS));
    g_autofree char *alias = g_build_filename(root, "alias", NULL);
    g_autoptr(GFile) link = g_file_new_for_path(alias);
    g_assert_true(g_file_make_symbolic_link(link, source, NULL, NULL));
    g_assert_false(storage_copy(source, alias, NULL, &error)); g_clear_error(&error);
    g_autoptr(Game) game = game_new("Nested game");
    game->executable = g_strdup(exe);
    g_free(game->root); game->root = g_strdup(source);
    game->managed_files = TRUE;
    g_assert_true(game_save(game, NULL));
    g_autoptr(GCancellable) cancel = g_cancellable_new();
    g_cancellable_cancel(cancel);
    g_assert_false(game_import(game, cancel, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED); g_clear_error(&error);
    g_assert_true(game_import(game, NULL, &error)); g_assert_no_error(error);
    g_assert_true(g_str_has_suffix(game->executable, "/files/bin/game.exe"));
    g_autofree char *copied_asset = g_build_filename(game->root, "assets.dat", NULL);
    g_assert_true(g_file_test(copied_asset, G_FILE_TEST_IS_REGULAR));
    g_assert_true(game_remove(game, NULL));

    g_autofree char *bad = g_build_filename(source, "linked-asset", NULL);
    g_autoptr(GFile) nested = g_file_new_for_path(bad);
    g_assert_true(g_file_make_symbolic_link(nested, asset, NULL, NULL));
    g_autofree char *target = g_build_filename(root, "destination", NULL);
    g_assert_false(storage_copy(source, target, NULL, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
}

static void removal_recovery(void)
{
    g_autoptr(Game) game = game_new("Recover removal");
    g_assert_true(game_save(game, NULL));
    g_autoptr(GCancellable) cancel = g_cancellable_new();
    g_cancellable_cancel(cancel);
    g_autofree char *prefix = game_prefix_path(game);
    g_assert_cmpint(g_mkdir_with_parents(prefix, 0700), ==, 0);
    g_autoptr(GError) error = NULL;
    g_assert_false(game_remove_full(game, cancel, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED); g_clear_error(&error);
    g_autoptr(Game) pending = game_load(game->id, &error);
    g_assert_true(pending->removing);
    g_assert_false(pending->ready);
    g_autofree char *diagnostics = g_build_filename(g_get_user_state_dir(), "paladin-launcher", "logs", game->id, NULL);
    g_assert_cmpint(g_mkdir_with_parents(diagnostics, 0700), ==, 0);
    g_assert_true(game_remove_full(pending, NULL, &error)); g_assert_no_error(error);
    g_assert_false(g_file_test(diagnostics, G_FILE_TEST_EXISTS));
    g_assert_null(game_load(game->id, NULL));
    g_autofree char *file = g_build_filename(root, "not-directory", NULL);
    g_assert_true(g_file_set_contents(file, "data", -1, NULL));
    g_autofree char *impossible = g_build_filename(file, "child", NULL);
    g_assert_false(storage_remove(impossible, NULL, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY);
}

static void logs_and_publisher_scan(void)
{
    g_autofree char *log = g_build_filename(root, "binary.log", NULL);
    char bytes[8192]; memset(bytes, 'x', sizeof(bytes));
    bytes[8100] = '\0'; bytes[8101] = (char) 0xff;
    g_assert_true(g_file_set_contents(log, bytes, sizeof(bytes), NULL));
    g_autofree char *tail = storage_read_tail(log, 256, NULL);
    g_assert_true(g_utf8_validate(tail, -1, NULL));
    g_assert_cmpuint(strlen(tail), <, 400);
    g_assert_nonnull(strstr(tail, "Showing the end"));
    g_autoptr(Game) game = game_new("Crash game");
    g_autofree char *prefix = game_prefix_path(game);
    g_autofree char *folder = g_build_filename(prefix, "drive_c", "Sierra", "Crash", NULL);
    g_assert_cmpint(g_mkdir_with_parents(folder, 0700), ==, 0);
    g_autofree char *exe = g_build_filename(folder, "Crash.exe", NULL);
    g_assert_true(g_file_set_contents(exe, "", 0, NULL));
    g_autoptr(GPtrArray) found = game_find_executables_full(game, NULL, NULL);
    g_assert_cmpuint(found->len, ==, 1);
    g_assert_cmpstr(g_ptr_array_index(found, 0), ==, exe);
    g_assert_null(game_load("../../bad", NULL));
    g_assert_true(game_remove(game, NULL));
}

int main(int argc, char **argv)
{
    root = g_dir_make_tmp("paladin-tests-XXXXXX", NULL);
    g_assert_nonnull(root);
    g_autofree char *data = g_build_filename(root, "data", NULL);
    g_autofree char *config = g_build_filename(root, "config", NULL);
    g_autofree char *cache = g_build_filename(root, "cache", NULL);
    g_setenv("XDG_DATA_HOME", data, TRUE);
    g_setenv("XDG_CONFIG_HOME", config, TRUE);
    g_setenv("XDG_CACHE_HOME", cache, TRUE);
    g_autofree char *state = g_build_filename(root, "state", NULL);
    g_setenv("XDG_STATE_HOME", state, TRUE);
    g_setenv("WAYLAND_DISPLAY", "test-display", TRUE);
    g_setenv("DISPLAY", ":99", TRUE);
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/game/isolated-prefixes", paths_are_separate);
    g_test_add_func("/game/metadata-and-menu", save_load_and_shortcut);
    g_test_add_func("/game/symlink-removal", deletion_never_follows_links);
    g_test_add_func("/game/launcher-candidates", candidate_selection);
    g_test_add_func("/storage/copy-and-nested-root", copy_guards_and_nested_root);
    g_test_add_func("/storage/removal-recovery", removal_recovery);
    g_test_add_func("/storage/logs-and-publisher-scan", logs_and_publisher_scan);
    int result = g_test_run();
    g_assert_true(storage_remove(root, NULL, NULL));
    g_free(root);
    return result;
}
