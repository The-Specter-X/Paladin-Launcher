#include "ui.h"
#include "dialogs.h"
#include "runner.h"
#include "storage.h"
#include <libxapp/xapp-gtk-window.h>
#include <glib/gstdio.h>
#include <string.h>

typedef struct {
    GtkApplication *app;
    Operations *operations;
    GtkWidget *window, *list, *detail, *search;
    char *selected_id;
    GHashTable *discovery_attempted;
    gboolean refreshing;
} Ui;

typedef struct { Ui *ui; char *id; } UiJob;
static void refresh(Ui *ui);
static void show_selected(Ui *ui);
static void show_candidates(Ui *ui, const char *id);

Operations *ui_operations(GtkApplication *application)
{
    Operations *operations = g_object_get_data(G_OBJECT(application), "paladin-operations");
    if (!operations) {
        operations = operations_new(G_APPLICATION(application));
        g_object_set_data_full(G_OBJECT(application), "paladin-operations", operations, g_object_unref);
    }
    return operations;
}

static void error(Ui *ui, const char *message)
{
    if (ui->window) dialogs_message(GTK_WINDOW(ui->window), GTK_MESSAGE_ERROR, message);
    else ui_show_failure(ui->app, ui->selected_id, message);
}

static GtkWidget *button(const char *label, const char *icon)
{
    GtkWidget *widget = gtk_button_new_with_label(label);
    if (icon) {
        gtk_button_set_image(GTK_BUTTON(widget), gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON));
        gtk_button_set_always_show_image(GTK_BUTTON(widget), TRUE);
    }
    return widget;
}

static void clear_box(GtkWidget *box)
{
    GList *children = gtk_container_get_children(GTK_CONTAINER(box));
    for (GList *p = children; p; p = p->next) gtk_widget_destroy(p->data);
    g_list_free(children);
}

static char *selection_file(void)
{
    return g_build_filename(g_get_user_config_dir(), "paladin-launcher", "ui.ini", NULL);
}

static void remember_selection(Ui *ui)
{
    if (!ui->selected_id) return;
    g_autofree char *path = selection_file();
    g_autofree char *dir = g_path_get_dirname(path);
    if (g_mkdir_with_parents(dir, 0700) != 0) return;
    g_autoptr(GKeyFile) key = g_key_file_new();
    g_key_file_set_string(key, "Library", "Selected", ui->selected_id);
    g_key_file_save_to_file(key, path, NULL);
}

static void operation_changed(Operations *operations, const char *id, Ui *ui)
{
    (void) operations; (void) id;
    /* A background operation never changes the selected game. */
    if (ui->window) refresh(ui);
}

static void start_file(Ui *ui, const char *id, OperationKind kind)
{
    g_autoptr(GError) problem = NULL;
    if (!operations_start(ui->operations, id, kind, &problem)) error(ui, problem->message);
}

static void operation_completed(Operations *operations, const char *id, int kind,
                                const char *message, gboolean cancelled, Ui *ui)
{
    (void) operations;
    if (cancelled) return;
    if (message) { ui_show_failure(ui->app, id, message); return; }
    if (!ui->window) return;
    if (kind == OP_INSTALL) {
        g_hash_table_add(ui->discovery_attempted, g_strdup(id));
        start_file(ui, id, OP_SCAN);
    } else if (kind == OP_SCAN) show_candidates(ui, id);
}

static void run_selected(GtkButton *unused, Ui *ui)
{
    (void) unused;
    g_autofree char *id = g_strdup(ui->selected_id);
    if (!id) return;
    g_autoptr(GError) problem = NULL;
    if (!operations_run(ui->operations, id, NULL, OP_PLAY, &problem)) error(ui, problem->message);
}

static void run_installer(GtkButton *unused, Ui *ui)
{
    (void) unused;
    g_autofree char *id = g_strdup(ui->selected_id);
    if (!id) return;
    g_autoptr(GError) problem = NULL;
    if (!operations_run(ui->operations, id, NULL, OP_INSTALL, &problem)) error(ui, problem->message);
}

static void run_utility(GtkButton *unused, Ui *ui)
{
    (void) unused;
    g_autofree char *id = g_strdup(ui->selected_id);
    if (!id) return;
    g_autofree char *path = dialogs_choose(GTK_WINDOW(ui->window), "Run another EXE in this game's prefix", NULL, FALSE);
    if (!path) return;
    g_autoptr(GError) problem = NULL;
    if (!operations_run(ui->operations, id, path, OP_UTILITY, &problem)) error(ui, problem->message);
}

static void stop_selected(GtkButton *unused, Ui *ui)
{
    (void) unused;
    if (ui->selected_id) operations_stop(ui->operations, ui->selected_id);
}

static void retry_import(GtkButton *unused, Ui *ui)
{
    (void) unused;
    g_autofree char *id = g_strdup(ui->selected_id);
    if (id) start_file(ui, id, OP_IMPORT);
}

static void discover(GtkButton *unused, Ui *ui)
{
    (void) unused;
    g_autofree char *id = g_strdup(ui->selected_id);
    if (!id) return;
    g_hash_table_add(ui->discovery_attempted, g_strdup(id));
    start_file(ui, id, OP_SCAN);
}

static void settings(GtkButton *unused, Ui *ui)
{
    (void) unused;
    g_autofree char *id = g_strdup(ui->selected_id);
    if (id) dialogs_settings(GTK_WINDOW(ui->window), ui->operations, id);
}

static void choose_executable(GtkButton *unused, Ui *ui)
{
    (void) unused;
    g_autofree char *id = g_strdup(ui->selected_id);
    if (!id) return;
    g_autoptr(Game) game = game_load(id, NULL);
    if (!game) return;
    g_autofree char *prefix = game_prefix_path(game);
    g_autofree char *path = dialogs_choose(GTK_WINDOW(ui->window), "Choose game executable", prefix, FALSE);
    if (!path) return;
    g_autoptr(GError) problem = NULL;
    if (!operations_choose_executable(ui->operations, id, path, &problem)) error(ui, problem->message);
}

static void remove_game(GtkButton *unused, Ui *ui)
{
    (void) unused;
    g_autofree char *id = g_strdup(ui->selected_id);
    if (!id) return;
    g_autoptr(Game) game = game_load(id, NULL);
    if (!game) return;
    g_autofree char *title = g_strdup_printf("Remove %s?", game->name);
    if (!dialogs_confirm(GTK_WINDOW(ui->window), title,
        "Managed game files, its prefix, local prefix backups and saves inside them will be deleted. "
        "Linked files outside Paladin's game directory will be kept.", "Remove game")) return;
    /* Revalidate the captured ID after the dialog, not the current selection. */
    start_file(ui, id, OP_REMOVE);
}

static void reset_prefix(GtkButton *unused, Ui *ui)
{
    (void) unused;
    g_autofree char *id = g_strdup(ui->selected_id);
    if (!id) return;
    g_autoptr(Game) game = game_load(id, NULL);
    if (!game) return;
    g_autofree char *title = g_strdup_printf("Back up and reset %s's prefix?", game->name);
    if (!dialogs_confirm(GTK_WINDOW(ui->window), title,
        "The current prefix and settings will be kept in this game's backups folder. "
        "You can then change WoW64 mode and reinstall from the original media. "
        "Portable game files stay in place. Saves in the old prefix stay in its backup.", "Back up / reset")) return;
    start_file(ui, id, OP_RESET);
}

static void open_path(Ui *ui, const char *path)
{
    if (!path || !*path || !g_file_test(path, G_FILE_TEST_IS_DIR)) {
        error(ui, "This folder does not exist yet. Run or install the game first."); return;
    }
    g_autofree char *uri = g_filename_to_uri(path, NULL, NULL);
    g_autoptr(GError) problem = NULL;
    if (uri && !gtk_show_uri_on_window(GTK_WINDOW(ui->window), uri, GDK_CURRENT_TIME, &problem))
        error(ui, problem->message);
}

static void open_files(GtkButton *unused, Ui *ui)
{
    (void) unused;
    g_autoptr(Game) game = ui->selected_id ? game_load(ui->selected_id, NULL) : NULL;
    if (!game) return;
    g_autofree char *folder = game->root && *game->root ? g_strdup(game->root) :
        g_path_get_dirname(game->executable && *game->executable ? game->executable :
                           game->installer && *game->installer ? game->installer : g_get_home_dir());
    open_path(ui, folder);
}

static void open_prefix(GtkButton *unused, Ui *ui)
{
    (void) unused;
    g_autoptr(Game) game = ui->selected_id ? game_load(ui->selected_id, NULL) : NULL;
    if (!game) return;
    g_autofree char *path = game_prefix_path(game);
    open_path(ui, path);
}

static void open_backup(GtkButton *unused, Ui *ui)
{
    (void) unused;
    g_autoptr(Game) game = ui->selected_id ? game_load(ui->selected_id, NULL) : NULL;
    if (game) open_path(ui, game->last_backup);
}

static void log_worker(GTask *task, gpointer object, gpointer task_data, GCancellable *cancel)
{
    (void) object; (void) cancel;
    const char *id = task_data;
    g_autoptr(GError) problem = NULL;
    g_autoptr(Game) game = game_load(id, &problem);
    if (!game) { g_task_return_error(task, g_steal_pointer(&problem)); return; }
    g_autoptr(GString) text = g_string_new("");
    for (guint i = 0; i < 2; i++) {
        g_autofree char *path = game_log_path(game, i == 1);
        g_autofree char *tail = storage_read_tail(path, 256 * 1024, &problem);
        if (!tail && g_error_matches(problem, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
            g_clear_error(&problem);
            g_autofree char *name = g_path_get_basename(path);
            g_autofree char *legacy = g_build_filename(g_get_user_cache_dir(), "paladin-launcher", "logs", name, NULL);
            tail = storage_read_tail(legacy, 256 * 1024, &problem);
        }
        g_string_append_printf(text, "%s:\n%s\n\n", i ? "Errors" : "Standard output",
                               tail ? tail : problem->message);
        g_clear_error(&problem);
    }
    g_task_return_pointer(task, g_string_free(g_steal_pointer(&text), FALSE), g_free);
}

static void logs_ready(GObject *object, GAsyncResult *result, gpointer user_data)
{
    Ui *ui = user_data;
    g_autoptr(GError) problem = NULL;
    g_autofree char *text = g_task_propagate_pointer(G_TASK(result), &problem);
    if (ui->window && text) {
        g_autoptr(GtkWidget) dialog = g_object_ref_sink(gtk_dialog_new_with_buttons("Game logs", GTK_WINDOW(ui->window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Close", GTK_RESPONSE_CLOSE, NULL));
        gtk_window_set_default_size(GTK_WINDOW(dialog), 760, 440);
        GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
        gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scroll, TRUE, TRUE, 8);
        GtkWidget *view = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
        gtk_container_add(GTK_CONTAINER(scroll), view);
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), text, -1);
        gtk_widget_show_all(dialog);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    } else if (ui->window && problem) error(ui, problem->message);
    g_application_release(G_APPLICATION(object));
}

static void show_logs(GtkButton *unused, Ui *ui)
{
    (void) unused;
    if (!ui->selected_id) return;
    g_application_hold(G_APPLICATION(ui->app));
    GTask *task = g_task_new(ui->app, NULL, logs_ready, ui);
    g_task_set_task_data(task, g_strdup(ui->selected_id), g_free);
    g_task_run_in_thread(task, log_worker);
    g_object_unref(task);
}

static void runtime_information(GtkButton *unused, Ui *ui)
{
    (void) unused;
    g_autofree char *report = runner_preflight_report();
    dialogs_message(GTK_WINDOW(ui->window), GTK_MESSAGE_INFO, report);
}

static void show_candidates(Ui *ui, const char *id)
{
    if (!ui->window) return;
    g_autoptr(Game) game = game_load(id, NULL);
    g_autoptr(GPtrArray) choices = operations_candidates(ui->operations, id);
    if (!game || !choices) return;
    g_autofree char *title = g_strdup_printf("Choose the launcher for %s", game->name);
    g_autoptr(GtkWidget) dialog = g_object_ref_sink(gtk_dialog_new_with_buttons(title, GTK_WINDOW(ui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Later", GTK_RESPONSE_CANCEL,
        "Browse…", GTK_RESPONSE_APPLY, "Use selected", GTK_RESPONSE_ACCEPT, NULL));
    gtk_window_set_default_size(GTK_WINDOW(dialog), 640, 180);
    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(area), 16);
    GtkWidget *label = gtk_label_new(choices->len ?
        "Select the program that starts the game. Paths are relative to its Windows C: drive." :
        "No game executable was found. Browse to its installed location or run the installer again.");
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(area), label, FALSE, FALSE, 8);
    GtkWidget *combo = gtk_combo_box_text_new();
    g_autofree char *prefix = game_prefix_path(game);
    g_autofree char *drive = g_build_filename(prefix, "drive_c", NULL);
    g_autoptr(GFile) root = g_file_new_for_path(drive);
    for (guint i = 0; i < choices->len; i++) {
        const char *path = g_ptr_array_index(choices, i);
        g_autoptr(GFile) file = g_file_new_for_path(path);
        g_autofree char *relative = g_file_get_relative_path(root, file);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), relative ? relative : path);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), choices->len ? 0 : -1);
    gtk_box_pack_start(GTK_BOX(area), combo, FALSE, FALSE, 8);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT, choices->len != 0);
    gtk_widget_show_all(dialog);
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    g_autofree char *path = NULL;
    if (response == GTK_RESPONSE_ACCEPT) {
        int index = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
        if (index >= 0) path = g_strdup(g_ptr_array_index(choices, index));
    }
    gtk_widget_destroy(dialog);
    if (response == GTK_RESPONSE_APPLY && ui->window)
        path = dialogs_choose(GTK_WINDOW(ui->window), title, drive, FALSE);
    if (path) {
        g_autoptr(GError) problem = NULL;
        if (!operations_choose_executable(ui->operations, id, path, &problem)) error(ui, problem->message);
    }
}

static gboolean resume_discovery(gpointer user_data)
{
    UiJob *job = user_data;
    g_autoptr(Game) game = game_load(job->id, NULL);
    if (job->ui->window && game && game->discover_pending && !game->removing &&
        !operations_busy(job->ui->operations, job->id)) start_file(job->ui, job->id, OP_SCAN);
    g_application_release(G_APPLICATION(job->ui->app));
    g_free(job->id); g_free(job);
    return G_SOURCE_REMOVE;
}

static void add_action(GtkWidget *box, Ui *ui, const char *label, GCallback callback, gboolean sensitive)
{
    GtkWidget *item = button(label, NULL);
    gtk_widget_set_sensitive(item, sensitive);
    gtk_box_pack_start(GTK_BOX(box), item, FALSE, FALSE, 0);
    g_signal_connect(item, "clicked", callback, ui);
}

static void show_selected(Ui *ui)
{
    if (!ui->detail) return;
    clear_box(ui->detail);
    g_autoptr(Game) game = ui->selected_id ? game_load(ui->selected_id, NULL) : NULL;
    if (!game) {
        gtk_box_pack_start(GTK_BOX(ui->detail), gtk_image_new_from_icon_name("applications-games", GTK_ICON_SIZE_DIALOG), FALSE, FALSE, 12);
        GtkWidget *label = gtk_label_new("Your Windows games\n\nAdd an existing EXE or install from local files and discs.");
        gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
        gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
        gtk_box_pack_start(GTK_BOX(ui->detail), label, FALSE, FALSE, 12);
        gtk_widget_show_all(ui->detail);
        return;
    }
    gboolean busy = operations_busy(ui->operations, game->id);
    gboolean editable = !busy && !game->removing;
    GtkWidget *title = gtk_label_new(game->name);
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "game-title");
    gtk_box_pack_start(GTK_BOX(ui->detail), title, FALSE, FALSE, 4);
    const char *state = operations_status(ui->operations, game->id);
    if (!state) state = game->removing ? "Removal incomplete — retry to finish cleanup" :
        game->discover_pending ? "Installation needs launcher selection" :
        game->ready ? "Ready to play" : game->managed_files ? "Import incomplete — retry copying" : "Ready to install";
    GtkWidget *status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(ui->detail), status, FALSE, FALSE, 12);
    if (busy) {
        GtkWidget *spinner = gtk_spinner_new(); gtk_spinner_start(GTK_SPINNER(spinner));
        gtk_box_pack_start(GTK_BOX(status), spinner, FALSE, FALSE, 0);
    }
    GtkWidget *label = gtk_label_new(state);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_box_pack_start(GTK_BOX(status), label, TRUE, TRUE, 0);
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(ui->detail), actions, FALSE, FALSE, 0);
    GtkWidget *play = button("Play", "media-playback-start-symbolic");
    gtk_style_context_add_class(gtk_widget_get_style_context(play), "suggested-action");
    gtk_widget_set_sensitive(play, editable && game->ready);
    gtk_box_pack_start(GTK_BOX(actions), play, FALSE, FALSE, 0);
    g_signal_connect(play, "clicked", G_CALLBACK(run_selected), ui);
    add_action(actions, ui, "Stop / cancel", G_CALLBACK(stop_selected), busy);
    add_action(actions, ui, "Settings", G_CALLBACK(settings), editable);
    if (game->installer && *game->installer) add_action(ui->detail, ui, "Run installer", G_CALLBACK(run_installer), editable);
    if (game->managed_files && !game->ready && !game->removing)
        add_action(ui->detail, ui, "Retry copying game folder", G_CALLBACK(retry_import), editable);
    GtkWidget *tools = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(ui->detail), tools, FALSE, FALSE, 12);
    add_action(tools, ui, "Find installed game", G_CALLBACK(discover), editable && game_prefix_exists(game));
    add_action(tools, ui, "Choose EXE", G_CALLBACK(choose_executable), editable);
    add_action(ui->detail, ui, "Run another EXE in this game…", G_CALLBACK(run_utility), editable);
    gtk_box_pack_start(GTK_BOX(ui->detail), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 14);
    GtkWidget *files = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(ui->detail), files, FALSE, FALSE, 0);
    add_action(files, ui, "Game files", G_CALLBACK(open_files), TRUE);
    add_action(files, ui, "Prefix", G_CALLBACK(open_prefix), TRUE);
    add_action(files, ui, "Logs", G_CALLBACK(show_logs), TRUE);
    add_action(ui->detail, ui, "Back up / reset prefix", G_CALLBACK(reset_prefix), editable && game_prefix_exists(game));
    if (game->last_backup && *game->last_backup)
        add_action(ui->detail, ui, "Open last prefix backup", G_CALLBACK(open_backup), TRUE);
    add_action(ui->detail, ui, game->removing ? "Retry removal" : "Remove game", G_CALLBACK(remove_game), !busy);
    if (game->last_version && *game->last_version) {
        g_autofree char *version = g_strdup_printf("Last successful runner: %s", game->last_version);
        GtkWidget *info = gtk_label_new(version);
        gtk_label_set_line_wrap(GTK_LABEL(info), TRUE);
        gtk_label_set_xalign(GTK_LABEL(info), 0);
        gtk_box_pack_start(GTK_BOX(ui->detail), info, FALSE, FALSE, 12);
    }
    gtk_widget_show_all(ui->detail);
    if (operations_busy(ui->operations, game->id) && game->discover_pending)
        g_hash_table_add(ui->discovery_attempted, g_strdup(game->id));
    if (editable && game->discover_pending && !g_hash_table_contains(ui->discovery_attempted, game->id)) {
        g_hash_table_add(ui->discovery_attempted, g_strdup(game->id));
        UiJob *job = g_new0(UiJob, 1); job->ui = ui; job->id = g_strdup(game->id);
        g_application_hold(G_APPLICATION(ui->app));
        g_idle_add(resume_discovery, job);
    }
}

static void selection_changed(GtkListBox *unused, GtkListBoxRow *row, Ui *ui)
{
    (void) unused;
    if (ui->refreshing) return;
    const char *id = row ? g_object_get_data(G_OBJECT(row), "game-id") : NULL;
    g_free(ui->selected_id); ui->selected_id = g_strdup(id);
    remember_selection(ui);
    show_selected(ui);
}

static gboolean filter_row(GtkListBoxRow *row, gpointer user_data)
{
    Ui *ui = user_data;
    const char *name = g_object_get_data(G_OBJECT(row), "game-name");
    g_autofree char *query = g_utf8_casefold(gtk_entry_get_text(GTK_ENTRY(ui->search)), -1);
    return !*query || (name && strstr(name, query));
}

static void search_changed(GtkSearchEntry *unused, Ui *ui)
{
    (void) unused;
    gtk_list_box_invalidate_filter(GTK_LIST_BOX(ui->list));
}

static void refresh(Ui *ui)
{
    if (!ui->window) return;
    ui->refreshing = TRUE;
    clear_box(ui->list);
    g_autoptr(GPtrArray) games = game_load_all();
    GtkListBoxRow *select = NULL, *first = NULL;
    for (guint i = 0; i < games->len; i++) {
        Game *game = g_ptr_array_index(games, i);
        GtkWidget *row = gtk_list_box_row_new();
        g_object_set_data_full(G_OBJECT(row), "game-id", g_strdup(game->id), g_free);
        g_object_set_data_full(G_OBJECT(row), "game-name", g_utf8_casefold(game->name, -1), g_free);
        GtkWidget *label = gtk_label_new(game->name);
        gtk_label_set_xalign(GTK_LABEL(label), 0);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_widget_set_margin_start(label, 14); gtk_widget_set_margin_end(label, 14);
        gtk_widget_set_margin_top(label, 14); gtk_widget_set_margin_bottom(label, 14);
        gtk_container_add(GTK_CONTAINER(row), label);
        gtk_container_add(GTK_CONTAINER(ui->list), row);
        if (!first) first = GTK_LIST_BOX_ROW(row);
        if (g_strcmp0(game->id, ui->selected_id) == 0) select = GTK_LIST_BOX_ROW(row);
    }
    gtk_widget_show_all(ui->list);
    if (!select) select = first;
    gtk_list_box_select_row(GTK_LIST_BOX(ui->list), select);
    ui->refreshing = FALSE;
    selection_changed(GTK_LIST_BOX(ui->list), select, ui);
}

static void add_game(Ui *ui, gboolean installer)
{
    g_autofree char *path = dialogs_choose(GTK_WINDOW(ui->window), installer ?
        "Choose setup.exe from local files or a mounted disc" : "Choose an existing game's EXE", NULL, FALSE);
    if (!path) return;
    g_autofree char *base = g_path_get_basename(path);
    g_autofree char *directory = g_path_get_dirname(path);
    g_autoptr(GtkWidget) dialog = g_object_ref_sink(gtk_dialog_new_with_buttons(installer ? "Install game" : "Add game",
        GTK_WINDOW(ui->window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancel", GTK_RESPONSE_CANCEL, installer ? "Create game" : "Add game", GTK_RESPONSE_ACCEPT, NULL));
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(box), 16);
    GtkWidget *name = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(name), base);
    gtk_entry_set_placeholder_text(GTK_ENTRY(name), "Game title");
    gtk_box_pack_start(GTK_BOX(box), name, FALSE, FALSE, 8);
    GtkWidget *root = NULL, *copy = NULL;
    if (!installer) {
        GtkWidget *hint = gtk_label_new("Select the whole game folder, including assets above a nested EXE.");
        gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
        gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 8);
        root = gtk_file_chooser_button_new("Game root folder", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(root), directory);
        gtk_box_pack_start(GTK_BOX(box), root, FALSE, FALSE, 8);
        copy = gtk_check_button_new_with_label("Copy this game folder into Paladin's managed storage");
        gtk_box_pack_start(GTK_BOX(box), copy, FALSE, FALSE, 8);
    } else {
        GtkWidget *hint = gtk_label_new("Create the entry, adjust settings if needed, then click Run installer.\nThe installed game and any patches will share this prefix.");
        gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
        gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 8);
    }
    gtk_widget_show_all(dialog);
    while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        g_autoptr(Game) game = game_new(gtk_entry_get_text(GTK_ENTRY(name)));
        if (installer) game->installer = g_strdup(path);
        else {
            game->executable = g_strdup(path);
            g_free(game->root); game->root = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(root));
            if (!game->root) game->root = g_strdup(directory);
            g_free(game->working_dir); game->working_dir = g_strdup(game->root);
            game->managed_files = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(copy));
            game->ready = !game->managed_files;
        }
        g_autoptr(GError) problem = NULL;
        if (!runner_validate(game, &problem) || !game_save(game, &problem)) {
            dialogs_message(GTK_WINDOW(dialog), GTK_MESSAGE_ERROR, problem->message); continue;
        }
        if (!game_write_shortcuts(game, &problem))
            dialogs_message(GTK_WINDOW(dialog), GTK_MESSAGE_ERROR, problem->message);
        g_free(ui->selected_id); ui->selected_id = g_strdup(game->id);
        gtk_widget_destroy(dialog);
        refresh(ui);
        if (game->managed_files) start_file(ui, game->id, OP_IMPORT);
        return;
    }
    gtk_widget_destroy(dialog);
}

static void add_exe(GtkButton *unused, Ui *ui) { (void) unused; add_game(ui, FALSE); }
static void add_installer(GtkButton *unused, Ui *ui) { (void) unused; add_game(ui, TRUE); }

static void window_destroy(GtkWidget *unused, Ui *ui)
{
    (void) unused;
    ui->window = ui->list = ui->detail = ui->search = NULL;
    g_hash_table_remove_all(ui->discovery_attempted);
}

static void ui_free(Ui *ui)
{
    g_signal_handlers_disconnect_by_data(ui->operations, ui);
    g_object_unref(ui->operations);
    g_hash_table_unref(ui->discovery_attempted);
    g_free(ui->selected_id);
    g_free(ui);
}

static Ui *get_ui(GtkApplication *application)
{
    Ui *ui = g_object_get_data(G_OBJECT(application), "paladin-ui");
    if (!ui) {
        ui = g_new0(Ui, 1); ui->app = application;
        ui->operations = g_object_ref(ui_operations(application));
        ui->discovery_attempted = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        g_autofree char *path = selection_file();
        g_autoptr(GKeyFile) key = g_key_file_new();
        if (g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, NULL))
            ui->selected_id = g_key_file_get_string(key, "Library", "Selected", NULL);
        g_signal_connect(ui->operations, "changed", G_CALLBACK(operation_changed), ui);
        g_signal_connect(ui->operations, "completed", G_CALLBACK(operation_completed), ui);
        g_object_set_data_full(G_OBJECT(application), "paladin-ui", ui, (GDestroyNotify) ui_free);
    }
    return ui;
}

void ui_prepare(GtkApplication *application)
{
    get_ui(application);
}

void ui_show_failure(GtkApplication *application, const char *id, const char *message)
{
    Ui *ui = get_ui(application);
    if (!ui->window && id) { g_free(ui->selected_id); ui->selected_id = g_strdup(id); }
    ui_activate(application);
    dialogs_message(GTK_WINDOW(ui->window), GTK_MESSAGE_ERROR, message);
}

void ui_activate(GtkApplication *application)
{
    Ui *ui = get_ui(application);
    if (ui->window) { gtk_window_present(GTK_WINDOW(ui->window)); return; }
    ui->window = xapp_gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_application_add_window(application, GTK_WINDOW(ui->window));
    xapp_gtk_window_set_icon_name(XAPP_GTK_WINDOW(ui->window), "applications-games");
    gtk_window_set_title(GTK_WINDOW(ui->window), "Paladin Launcher");
    gtk_window_set_default_size(GTK_WINDOW(ui->window), 940, 620);
    g_signal_connect(ui->window, "destroy", G_CALLBACK(window_destroy), ui);
    if (!g_object_get_data(G_OBJECT(application), "paladin-css")) {
        GtkCssProvider *css = gtk_css_provider_new();
        gtk_css_provider_load_from_data(css, ".paladin-detail { padding: 24px; } .game-title { font-size: 24px; font-weight: bold; }", -1, NULL);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_set_data_full(G_OBJECT(application), "paladin-css", css, g_object_unref);
    }
    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "Paladin Launcher");
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header), "Your local Windows games");
    gtk_window_set_titlebar(GTK_WINDOW(ui->window), header);
    GtkWidget *add = button("Add EXE", "list-add-symbolic");
    GtkWidget *install = button("Install", "media-optical-symbolic");
    GtkWidget *runtime = button("Runtime information", "dialog-information-symbolic");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), add);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), install);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), runtime);
    g_signal_connect(add, "clicked", G_CALLBACK(add_exe), ui);
    g_signal_connect(install, "clicked", G_CALLBACK(add_installer), ui);
    g_signal_connect(runtime, "clicked", G_CALLBACK(runtime_information), ui);
    GtkWidget *layout = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(ui->window), layout);
    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_size_request(sidebar, 250, -1);
    gtk_box_pack_start(GTK_BOX(layout), sidebar, FALSE, FALSE, 0);
    ui->search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ui->search), "Search games");
    gtk_box_pack_start(GTK_BOX(sidebar), ui->search, FALSE, FALSE, 8);
    GtkWidget *list_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(sidebar), list_scroll, TRUE, TRUE, 0);
    ui->list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ui->list), GTK_SELECTION_SINGLE);
    gtk_list_box_set_filter_func(GTK_LIST_BOX(ui->list), filter_row, ui, NULL);
    gtk_container_add(GTK_CONTAINER(list_scroll), ui->list);
    g_signal_connect(ui->list, "row-selected", G_CALLBACK(selection_changed), ui);
    g_signal_connect(ui->search, "search-changed", G_CALLBACK(search_changed), ui);
    GtkWidget *detail_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(detail_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(layout), detail_scroll, TRUE, TRUE, 0);
    ui->detail = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(ui->detail), "paladin-detail");
    gtk_container_add(GTK_CONTAINER(detail_scroll), ui->detail);
    refresh(ui);
    gtk_widget_show_all(ui->window);
}
