#include "game.h"
#include <glib/gstdio.h>

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
    game->arguments = g_strdup("--windowed 'two words'");
    game->ready = TRUE;
    game->nvapi = TRUE;
    g_autoptr(GError) error = NULL;
    g_assert_true(game_save(game, &error));
    g_assert_no_error(error);
    g_autoptr(Game) loaded = game_load(game->id, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(loaded->name, ==, game->name);
    g_assert_true(loaded->wow64);
    g_assert_true(loaded->wayland);
    g_assert_true(loaded->nvapi);
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
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/game/isolated-prefixes", paths_are_separate);
    g_test_add_func("/game/metadata-and-menu", save_load_and_shortcut);
    g_test_add_func("/game/symlink-removal", deletion_never_follows_links);
    g_test_add_func("/game/launcher-candidates", candidate_selection);
    return g_test_run();
}
