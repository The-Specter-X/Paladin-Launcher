#include "ui.h"
#include "storage.h"
#include <string.h>

static GtkApplication *app;
static GtkWidget *window;
static Game *first, *second;
static guint stage;

static GtkWidget *find_widget(GtkWidget *widget, GType type, const char *label)
{
    if (g_type_is_a(G_OBJECT_TYPE(widget), type) &&
        (!label || (GTK_IS_BUTTON(widget) && g_strcmp0(gtk_button_get_label(GTK_BUTTON(widget)), label) == 0)))
        return widget;
    if (!GTK_IS_CONTAINER(widget)) return NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
    GtkWidget *found = NULL;
    for (GList *item = children; item && !found; item = item->next)
        found = find_widget(item->data, type, label);
    g_list_free(children);
    return found;
}

static GtkWidget *dialog(void)
{
    GList *windows = gtk_window_list_toplevels();
    GtkWidget *found = NULL;
    for (GList *item = windows; item; item = item->next)
        if (GTK_IS_DIALOG(item->data) && gtk_widget_get_visible(item->data)) { found = item->data; break; }
    g_list_free(windows);
    return found;
}

static void select_game(const char *id)
{
    GtkWidget *list = find_widget(window, GTK_TYPE_LIST_BOX, NULL);
    g_assert_nonnull(list);
    GList *rows = gtk_container_get_children(GTK_CONTAINER(list));
    gboolean found = FALSE;
    for (GList *item = rows; item; item = item->next) {
        if (g_strcmp0(g_object_get_data(item->data, "game-id"), id) == 0) {
            gtk_list_box_select_row(GTK_LIST_BOX(list), item->data); found = TRUE; break;
        }
    }
    g_list_free(rows);
    g_assert_true(found);
}

static gboolean mutate_during_dialog(gpointer unused)
{
    (void) unused;
    GtkWidget *active = dialog();
    g_assert_nonnull(active);
    if (stage == 0) {
        g_free(first->last_version); first->last_version = g_strdup("background-completed");
        g_assert_true(game_save(first, NULL));
        g_signal_emit_by_name(ui_operations(app), "changed", first->id);
        select_game(second->id);
        gtk_dialog_response(GTK_DIALOG(active), GTK_RESPONSE_ACCEPT);
    } else {
        g_signal_emit_by_name(ui_operations(app), "changed", second->id);
        select_game(second->id);
        gtk_dialog_response(GTK_DIALOG(active), GTK_RESPONSE_ACCEPT);
    }
    return G_SOURCE_REMOVE;
}

static gboolean dismiss_candidate(gpointer unused)
{
    (void) unused;
    GtkWidget *active = dialog();
    if (!active) return G_SOURCE_CONTINUE;
    g_assert_true(g_str_has_prefix(gtk_window_get_title(GTK_WINDOW(active)), "Choose the launcher for"));
    gtk_dialog_response(GTK_DIALOG(active), GTK_RESPONSE_ACCEPT);
    return G_SOURCE_REMOVE;
}

static void lifetime_regression(void)
{
    ui_prepare(app);
    ui_activate(app);
    window = GTK_WIDGET(gtk_application_get_active_window(app));
    if (!window) window = GTK_WIDGET(gtk_application_get_windows(app)->data);
    g_assert_true(gtk_widget_get_visible(window));
    select_game(first->id);
    GtkWidget *settings = find_widget(window, GTK_TYPE_BUTTON, "Settings");
    g_assert_nonnull(settings);
    g_idle_add(mutate_during_dialog, NULL);
    gtk_button_clicked(GTK_BUTTON(settings));
    g_autoptr(Game) saved = game_load(first->id, NULL);
    g_assert_cmpstr(saved->last_version, ==, "background-completed");
    select_game(first->id);
    GtkWidget *remove = find_widget(window, GTK_TYPE_BUTTON, "Remove game");
    g_assert_nonnull(remove);
    stage = 1;
    g_idle_add(mutate_during_dialog, NULL);
    gtk_button_clicked(GTK_BUTTON(remove));
    gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (operations_busy(ui_operations(app), first->id) && g_get_monotonic_time() < deadline) {
        g_main_context_iteration(NULL, FALSE); g_usleep(1000);
    }
    g_assert_false(operations_busy(ui_operations(app), first->id));
    g_assert_null(game_load(first->id, NULL));
    g_autoptr(Game) survivor = game_load(second->id, NULL);
    g_assert_nonnull(survivor);

    /* Pending installer discovery must survive closing and reopening the window. */
    gtk_widget_destroy(window);
    second->discover_pending = TRUE; second->ready = FALSE;
    g_assert_true(game_save(second, NULL));
    g_autofree char *prefix = game_prefix_path(second);
    g_autofree char *folder = g_build_filename(prefix, "drive_c", "Publisher", NULL);
    g_assert_cmpint(g_mkdir_with_parents(folder, 0700), ==, 0);
    g_autofree char *exe = g_build_filename(folder, "game.exe", NULL);
    g_assert_true(g_file_set_contents(exe, "", 0, NULL));
    ui_activate(app);
    window = GTK_WIDGET(gtk_application_get_windows(app)->data);
    guint timer = g_timeout_add(10, dismiss_candidate, NULL);
    gboolean ready = FALSE;
    deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (!ready && g_get_monotonic_time() < deadline) {
        g_main_context_iteration(NULL, FALSE);
        g_autoptr(Game) current = game_load(second->id, NULL);
        ready = current && current->ready && !current->discover_pending;
        g_usleep(1000);
    }
    if (!ready) g_source_remove(timer);
    g_assert_true(ready);
    gtk_widget_destroy(window);
}

int main(int argc, char **argv)
{
    g_autofree char *root = g_dir_make_tmp("paladin-ui-XXXXXX", NULL);
    const char *vars[] = {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"};
    for (guint i = 0; i < G_N_ELEMENTS(vars); i++) {
        g_autofree char *dir = g_build_filename(root, vars[i], NULL); g_setenv(vars[i], dir, TRUE);
    }
    g_setenv("GSETTINGS_BACKEND", "memory", TRUE);
    g_setenv("NO_AT_BRIDGE", "1", TRUE);
    gtk_test_init(&argc, &argv, NULL);
    app = gtk_application_new("io.github.thespecterx.PaladinTests", G_APPLICATION_NON_UNIQUE);
    g_assert_true(g_application_register(G_APPLICATION(app), NULL, NULL));
    first = game_new("A first"); second = game_new("B second");
    g_assert_true(game_save(first, NULL)); g_assert_true(game_save(second, NULL));
    g_test_add_func("/ui/dialog-refresh-and-restart", lifetime_regression);
    int result = g_test_run();
    game_free(first); game_free(second); g_object_unref(app);
    g_assert_true(storage_remove(root, NULL, NULL));
    return result;
}
