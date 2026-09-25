#include "ui.h"
#include "game.h"
#include "runner.h"

#include <libxapp/xapp-gtk-window.h>
#include <glib/gstdio.h>

typedef struct {
    GtkApplication *app;
    GtkWidget *window, *list, *detail;
    GPtrArray *games;
    Game *selected; /* Borrowed from games. */
    GHashTable *running; /* Borrowed from the application. */
} Ui;

typedef struct { Ui *ui; char *id; gboolean installer; } RunContext;
typedef struct { Ui *ui; char *id; char *source; char *target; char *basename; } ImportContext;

static void refresh(Ui *ui, const char *id);
static void show_selected(Ui *ui);
static void begin_copy(Ui *ui, Game *game);

static void show_error(Ui *ui, const char *message)
{
    if (!ui->window) { g_warning("%s", message); return; }
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(ui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
        GTK_BUTTONS_CLOSE, "%s", message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static GtkWidget *button(const char *label, const char *icon)
{
    GtkWidget *widget = gtk_button_new_with_label(label);
    if (icon) {
        GtkWidget *image = gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON);
        gtk_button_set_image(GTK_BUTTON(widget), image);
        gtk_button_set_always_show_image(GTK_BUTTON(widget), TRUE);
    }
    return widget;
}

static void clear_box(GtkWidget *box)
{
    GList *children = gtk_container_get_children(GTK_CONTAINER(box));
    for (GList *p = children; p; p = p->next) gtk_widget_destroy(GTK_WIDGET(p->data));
    g_list_free(children);
}

static void show_folder(GtkButton *unused, Ui *ui)
{
    (void) unused;
    if (!ui->selected || !ui->window) return;
    g_autofree char *path = game_data_path(ui->selected);
    g_autofree char *uri = g_filename_to_uri(path, NULL, NULL);
    g_autoptr(GError) error = NULL;
    if (uri && !gtk_show_uri_on_window(GTK_WINDOW(ui->window), uri,
                                        GDK_CURRENT_TIME, &error)) show_error(ui, error->message);
}

static void show_logs(GtkButton *unused, Ui *ui)
{
    (void) unused;
    if (!ui->selected) return;
    g_autofree char *out = game_log_path(ui->selected, FALSE);
    g_autofree char *err = game_log_path(ui->selected, TRUE);
    g_autofree char *text = NULL, *errors = NULL;
    g_file_get_contents(out, &text, NULL, NULL);
    g_file_get_contents(err, &errors, NULL, NULL);
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Game logs", GTK_WINDOW(ui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Close", GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 720, 400);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scroll,
                       TRUE, TRUE, 8);
    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    g_autofree char *combined = g_strdup_printf("Standard output:\n%s\n\nErrors:\n%s",
                                                text ? text : "(empty)", errors ? errors : "(empty)");
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), combined, -1);
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static char *choose_executable(Ui *ui, const char *title, const char *directory)
{
    GtkWidget *chooser = gtk_file_chooser_dialog_new(title, GTK_WINDOW(ui->window),
        GTK_FILE_CHOOSER_ACTION_OPEN, "Cancel", GTK_RESPONSE_CANCEL,
        "Select", GTK_RESPONSE_ACCEPT, NULL);
    if (directory) gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser), directory);
    GtkFileFilter *exe = gtk_file_filter_new();
    gtk_file_filter_set_name(exe, "Windows executables (*.exe)");
    gtk_file_filter_add_pattern(exe, "*.exe");
    gtk_file_filter_add_pattern(exe, "*.EXE");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), exe);
    GtkFileFilter *all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, "All files");
    gtk_file_filter_add_pattern(all, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), all);
    char *path = NULL;
    if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT)
        path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
    gtk_widget_destroy(chooser);
    return path;
}

static void pick_launch_file(GtkButton *unused, Ui *ui)
{
    (void) unused;
    if (!ui->selected || g_hash_table_contains(ui->running, ui->selected->id)) return;
    g_autofree char *prefix = game_prefix_path(ui->selected);
    g_autofree char *path = choose_executable(ui, "Choose game executable", prefix);
    if (!path) return;
    g_free(ui->selected->executable);
    ui->selected->executable = g_strdup(path);
    ui->selected->ready = TRUE;
    g_autoptr(GError) error = NULL;
    if (!game_save(ui->selected, &error) ||
        !game_write_shortcuts(ui->selected, &error)) show_error(ui, error->message);
    g_autofree char *id = g_strdup(ui->selected->id);
    refresh(ui, id);
}

static void finish_install(Ui *ui, Game *game)
{
    if (!ui->window) return;
    g_autoptr(GPtrArray) choices = game_find_executables(game);
    char *path = NULL;
    if (choices->len) {
        GtkWidget *dialog = gtk_dialog_new_with_buttons("Choose the game", GTK_WINDOW(ui->window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            "Later", GTK_RESPONSE_CANCEL, "Browse…", GTK_RESPONSE_APPLY,
            "Use selected", GTK_RESPONSE_ACCEPT, NULL);
        GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        GtkWidget *label = gtk_label_new("Installation finished. Which program starts the game?");
        gtk_box_pack_start(GTK_BOX(area), label, FALSE, FALSE, 10);
        GtkWidget *combo = gtk_combo_box_text_new();
        for (guint i = 0; i < MIN(choices->len, 50); i++) {
            const char *item = g_ptr_array_index(choices, i);
            g_autofree char *base = g_path_get_basename(item);
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), base);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
        gtk_box_pack_start(GTK_BOX(area), combo, FALSE, FALSE, 10);
        gtk_widget_show_all(dialog);
        int answer = gtk_dialog_run(GTK_DIALOG(dialog));
        if (answer == GTK_RESPONSE_ACCEPT) {
            int index = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
            if (index >= 0) path = g_strdup(g_ptr_array_index(choices, index));
        } else if (answer == GTK_RESPONSE_APPLY) {
            g_autofree char *prefix = game_prefix_path(game);
            path = choose_executable(ui, "Choose game executable", prefix);
        }
        gtk_widget_destroy(dialog);
    } else {
        g_autofree char *prefix = game_prefix_path(game);
        path = choose_executable(ui, "Find the installed game's executable", prefix);
    }
    if (!path) return;
    g_free(game->executable);
    game->executable = path;
    game->ready = TRUE;
    g_autoptr(GError) error = NULL;
    if (!game_save(game, &error) || !game_write_shortcuts(game, &error))
        show_error(ui, error->message);
}

static void run_finished(GObject *object, GAsyncResult *result, gpointer user_data)
{
    RunContext *context = user_data;
    Ui *ui = context->ui;
    g_autoptr(GError) error = NULL;
    gboolean completed = g_subprocess_wait_finish(G_SUBPROCESS(object), result, &error);
    g_hash_table_remove(ui->running, context->id);
    if (context->installer) {
        g_autoptr(Game) game = game_load(context->id, NULL);
        if (game && completed) finish_install(ui, game);
    }
    if (ui->window && error) show_error(ui, error->message);
    if (ui->window) refresh(ui, context->id);
    g_application_release(G_APPLICATION(ui->app));
    g_free(context->id);
    g_free(context);
}

static void start_game(Ui *ui, Game *game, const char *path, gboolean installer)
{
    if (g_hash_table_contains(ui->running, game->id)) {
        show_error(ui, "This game is already running or installing.");
        return;
    }
    g_autoptr(GError) error = NULL;
    g_autoptr(GSubprocess) process = runner_start(game, path, installer, &error);
    if (!process) { show_error(ui, error->message); return; }
    g_hash_table_add(ui->running, g_strdup(game->id));
    RunContext *context = g_new0(RunContext, 1);
    context->ui = ui;
    context->id = g_strdup(game->id);
    context->installer = installer;
    g_application_hold(G_APPLICATION(ui->app));
    g_subprocess_wait_async(process, NULL, run_finished, context);
    refresh(ui, game->id);
}

static void play(GtkButton *unused, Ui *ui)
{
    (void) unused;
    if (ui->selected && ui->selected->ready)
        start_game(ui, ui->selected, ui->selected->executable, FALSE);
}

static void rerun_installer(GtkButton *unused, Ui *ui)
{
    (void) unused;
    if (ui->selected && ui->selected->installer && *ui->selected->installer)
        start_game(ui, ui->selected, ui->selected->installer, TRUE);
}

static void retry_copy(GtkButton *unused, Ui *ui)
{
    (void) unused;
    if (ui->selected && ui->selected->managed_files && !ui->selected->ready)
        begin_copy(ui, ui->selected);
}

static void remove_game(GtkButton *unused, Ui *ui)
{
    (void) unused;
    if (!ui->selected || g_hash_table_contains(ui->running, ui->selected->id)) {
        show_error(ui, "Close the game before removing it."); return;
    }
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(ui->window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE, "Remove %s?", ui->selected->name);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
        "The managed files and Wine prefix, including saves inside the prefix, will be deleted. "
        "Files outside Paladin's game directory will be kept.");
    gtk_dialog_add_buttons(GTK_DIALOG(dialog), "Cancel", GTK_RESPONSE_CANCEL,
                           "Remove game", GTK_RESPONSE_ACCEPT, NULL);
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response != GTK_RESPONSE_ACCEPT) return;
    g_autoptr(GError) error = NULL;
    if (!game_remove(ui->selected, &error)) show_error(ui, error->message);
    refresh(ui, NULL);
}

static GtkWidget *settings_check(GtkWidget *box, const char *label, gboolean active)
{
    GtkWidget *check = gtk_check_button_new_with_label(label);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), active);
    gtk_box_pack_start(GTK_BOX(box), check, FALSE, FALSE, 3);
    return check;
}

static void settings(GtkButton *unused, Ui *ui)
{
    (void) unused;
    Game *game = ui->selected;
    if (!game || g_hash_table_contains(ui->running, game->id)) return;
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Game settings", GTK_WINDOW(ui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel", GTK_RESPONSE_CANCEL,
        "Save", GTK_RESPONSE_ACCEPT, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 550, 560);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scroll,
                       TRUE, TRUE, 0);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
    gtk_widget_set_margin_start(box, 20); gtk_widget_set_margin_end(box, 20);
    gtk_widget_set_margin_top(box, 16); gtk_widget_set_margin_bottom(box, 16);
    gtk_container_add(GTK_CONTAINER(scroll), box);

    gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Game title"), FALSE, FALSE, 0);
    GtkWidget *name = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(name), game->name);
    gtk_box_pack_start(GTK_BOX(box), name, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Proton runner (GE-Proton or an absolute version path)"), FALSE, FALSE, 0);
    GtkWidget *runner = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(runner), game->runner);
    gtk_box_pack_start(GTK_BOX(box), runner, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Game arguments"), FALSE, FALSE, 0);
    GtkWidget *arguments = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(arguments), game->arguments);
    gtk_box_pack_start(GTK_BOX(box), arguments, FALSE, FALSE, 0);
    GtkWidget *wayland = settings_check(box, "Native Wine Wayland (disable for XWayland)", game->wayland);
    GtkWidget *wow64 = settings_check(box, "New WoW64 mode", game->wow64);
    GtkWidget *fsr = settings_check(box, "GE-Proton fullscreen FSR", game->fsr);
    GtkWidget *wined3d = settings_check(box, "Use WineD3D instead of DXVK (older GPUs)", game->wined3d);
    GtkWidget *desktop = settings_check(box, "Create a desktop shortcut", game->desktop_shortcut);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Environment variables (one NAME=value per line)"), FALSE, FALSE, 0);
    GtkWidget *env_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(env_scroll, -1, 90);
    GtkWidget *env = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(env), TRUE);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(env)), game->environment, -1);
    gtk_container_add(GTK_CONTAINER(env_scroll), env);
    gtk_box_pack_start(GTK_BOX(box), env_scroll, FALSE, FALSE, 0);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *new_name = gtk_entry_get_text(GTK_ENTRY(name));
        if (*new_name) { g_free(game->name); game->name = g_strdup(new_name); }
        g_free(game->runner); game->runner = g_strdup(gtk_entry_get_text(GTK_ENTRY(runner)));
        g_free(game->arguments); game->arguments = g_strdup(gtk_entry_get_text(GTK_ENTRY(arguments)));
        GtkTextIter start, end;
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(env));
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        g_free(game->environment);
        game->environment = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        game->wayland = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wayland));
        game->wow64 = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wow64));
        game->fsr = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fsr));
        game->wined3d = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wined3d));
        game->desktop_shortcut = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(desktop));
        g_autoptr(GError) error = NULL;
        if (!game_save(game, &error) || !game_write_shortcuts(game, &error))
            show_error(ui, error->message);
        g_autofree char *id = g_strdup(game->id);
        gtk_widget_destroy(dialog);
        refresh(ui, id);
        return;
    }
    gtk_widget_destroy(dialog);
}

static void show_selected(Ui *ui)
{
    if (!ui->detail) return;
    clear_box(ui->detail);
    if (!ui->selected) {
        GtkWidget *icon = gtk_image_new_from_icon_name("applications-games", GTK_ICON_SIZE_DIALOG);
        gtk_box_pack_start(GTK_BOX(ui->detail), icon, FALSE, FALSE, 6);
        GtkWidget *label = gtk_label_new("Your Windows games will appear here.\n\nAdd an EXE or install from a disc to begin.");
        gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
        gtk_box_pack_start(GTK_BOX(ui->detail), label, FALSE, FALSE, 6);
        gtk_widget_show_all(ui->detail);
        return;
    }
    Game *game = ui->selected;
    GtkWidget *title = gtk_label_new(game->name);
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "game-title");
    gtk_box_pack_start(GTK_BOX(ui->detail), title, FALSE, FALSE, 4);
    const char *state = g_hash_table_contains(ui->running, game->id) ? "Running…" :
        (game->ready ? "Ready to play" : (game->installer && *game->installer ?
        "Select the game executable to finish setup" : "Import incomplete"));
    GtkWidget *subtitle = gtk_label_new(state);
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0);
    gtk_box_pack_start(GTK_BOX(ui->detail), subtitle, FALSE, FALSE, 12);
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(ui->detail), actions, FALSE, FALSE, 0);
    GtkWidget *play_button = button("Play", "media-playback-start-symbolic");
    gtk_style_context_add_class(gtk_widget_get_style_context(play_button), "suggested-action");
    gtk_widget_set_sensitive(play_button, game->ready &&
        !g_hash_table_contains(ui->running, game->id));
    gtk_box_pack_start(GTK_BOX(actions), play_button, FALSE, FALSE, 0);
    g_signal_connect(play_button, "clicked", G_CALLBACK(play), ui);
    GtkWidget *pick = button("Choose EXE", "document-open-symbolic");
    gtk_box_pack_start(GTK_BOX(actions), pick, FALSE, FALSE, 0);
    g_signal_connect(pick, "clicked", G_CALLBACK(pick_launch_file), ui);

    if (game->installer && *game->installer) {
        GtkWidget *again = button("Run installer", NULL);
        gtk_widget_set_sensitive(again, !g_hash_table_contains(ui->running, game->id));
        gtk_box_pack_start(GTK_BOX(ui->detail), again, FALSE, FALSE, 10);
        g_signal_connect(again, "clicked", G_CALLBACK(rerun_installer), ui);
    }
    if (game->managed_files && !game->ready) {
        GtkWidget *again = button("Retry copying game files", NULL);
        gtk_widget_set_sensitive(again, !g_hash_table_contains(ui->running, game->id));
        gtk_box_pack_start(GTK_BOX(ui->detail), again, FALSE, FALSE, 10);
        g_signal_connect(again, "clicked", G_CALLBACK(retry_copy), ui);
    }
    GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(ui->detail), separator, FALSE, FALSE, 12);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(ui->detail), row, FALSE, FALSE, 2);
    struct { const char *label; GCallback callback; } options[] = {
        {"Settings", G_CALLBACK(settings)}, {"Files", G_CALLBACK(show_folder)},
        {"Logs", G_CALLBACK(show_logs)}, {"Remove", G_CALLBACK(remove_game)}
    };
    for (guint i = 0; i < G_N_ELEMENTS(options); i++) {
        GtkWidget *item = button(options[i].label, NULL);
        gtk_box_pack_start(GTK_BOX(row), item, FALSE, FALSE, 0);
        g_signal_connect(item, "clicked", options[i].callback, ui);
    }
    if (!game->managed_files && game->ready) {
        GtkWidget *hint = gtk_label_new("Linked game files remain in their original folder when removed.");
        gtk_label_set_xalign(GTK_LABEL(hint), 0);
        gtk_box_pack_start(GTK_BOX(ui->detail), hint, FALSE, FALSE, 15);
    }
    gtk_widget_show_all(ui->detail);
}

static void selection_changed(GtkListBox *list, GtkListBoxRow *row, Ui *ui)
{
    (void) list;
    ui->selected = NULL;
    const char *id = row ? g_object_get_data(G_OBJECT(row), "game-id") : NULL;
    for (guint i = 0; id && ui->games && i < ui->games->len; i++) {
        Game *game = g_ptr_array_index(ui->games, i);
        if (g_str_equal(game->id, id)) { ui->selected = game; break; }
    }
    show_selected(ui);
}

static void refresh(Ui *ui, const char *id)
{
    if (!ui->window) return;
    g_autofree char *selected_id = id ? g_strdup(id) :
        (ui->selected ? g_strdup(ui->selected->id) : NULL);
    ui->selected = NULL;
    clear_box(ui->list);
    g_clear_pointer(&ui->games, g_ptr_array_unref);
    ui->games = game_load_all();
    GtkListBoxRow *to_select = NULL;
    for (guint i = 0; i < ui->games->len; i++) {
        Game *game = g_ptr_array_index(ui->games, i);
        GtkWidget *row = gtk_list_box_row_new();
        g_object_set_data_full(G_OBJECT(row), "game-id", g_strdup(game->id), g_free);
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_widget_set_margin_start(box, 12); gtk_widget_set_margin_end(box, 12);
        gtk_widget_set_margin_top(box, 10); gtk_widget_set_margin_bottom(box, 10);
        gtk_container_add(GTK_CONTAINER(row), box);
        GtkWidget *image = gtk_image_new_from_icon_name("applications-games", GTK_ICON_SIZE_DND);
        gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
        GtkWidget *label = gtk_label_new(game->name);
        gtk_label_set_xalign(GTK_LABEL(label), 0);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);
        gtk_container_add(GTK_CONTAINER(ui->list), row);
        if (selected_id && g_str_equal(selected_id, game->id)) to_select = GTK_LIST_BOX_ROW(row);
    }
    gtk_widget_show_all(ui->list);
    if (to_select) gtk_list_box_select_row(GTK_LIST_BOX(ui->list), to_select);
    else selection_changed(GTK_LIST_BOX(ui->list), NULL, ui);
}

static void import_worker(GTask *task, gpointer source_object, gpointer task_data,
                          GCancellable *cancellable)
{
    (void) source_object;
    ImportContext *job = task_data;
    g_autoptr(GError) error = NULL;
    if (!game_copy_folder(job->source, job->target, cancellable, &error))
        g_task_return_error(task, g_steal_pointer(&error));
    else g_task_return_boolean(task, TRUE);
}

static void import_finished(GObject *source, GAsyncResult *result, gpointer user_data)
{
    (void) source;
    ImportContext *job = user_data;
    Ui *ui = job->ui;
    g_autoptr(GError) error = NULL;
    gboolean success = g_task_propagate_boolean(G_TASK(result), &error);
    g_autoptr(Game) game = game_load(job->id, NULL);
    if (success && game) {
        g_free(game->executable);
        game->executable = g_build_filename(job->target, job->basename, NULL);
        game->ready = TRUE;
        if (!game_save(game, &error) || !game_write_shortcuts(game, &error)) success = FALSE;
    }
    if (ui->window && !success) show_error(ui, error ? error->message : "Import could not be completed");
    g_hash_table_remove(ui->running, job->id);
    if (ui->window) refresh(ui, job->id);
    g_application_release(G_APPLICATION(ui->app));
    g_free(job->id); g_free(job->source); g_free(job->target);
    g_free(job->basename); g_free(job);
}

static void begin_copy(Ui *ui, Game *game)
{
    if (g_hash_table_contains(ui->running, game->id)) return;
    g_autofree char *source_dir = g_path_get_dirname(game->executable);
    g_autofree char *data = game_data_path(game);
    ImportContext *job = g_new0(ImportContext, 1);
    job->ui = ui; job->id = g_strdup(game->id);
    job->source = g_strdup(source_dir);
    job->target = g_build_filename(data, "files", NULL);
    job->basename = g_path_get_basename(game->executable);
    g_hash_table_add(ui->running, g_strdup(game->id));
    g_application_hold(G_APPLICATION(ui->app));
    GTask *task = g_task_new(NULL, NULL, import_finished, job);
    g_task_set_task_data(task, job, NULL);
    g_task_run_in_thread(task, import_worker);
    g_object_unref(task);
    refresh(ui, job->id);
}

static void add_executable(GtkButton *unused, Ui *ui)
{
    (void) unused;
    g_autofree char *path = choose_executable(ui, "Choose a portable game's EXE", NULL);
    if (!path) return;
    g_autofree char *base = g_path_get_basename(path);
    char *dot = g_strrstr(base, ".");
    if (dot) *dot = '\0';
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Add game", GTK_WINDOW(ui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel", GTK_RESPONSE_CANCEL,
        "Add game", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(area), 16);
    GtkWidget *name = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(name), base);
    gtk_entry_set_placeholder_text(GTK_ENTRY(name), "Game name");
    gtk_box_pack_start(GTK_BOX(area), name, FALSE, FALSE, 8);
    GtkWidget *copy = gtk_check_button_new_with_label("Copy the entire game folder into Paladin's managed files");
    gtk_box_pack_start(GTK_BOX(area), copy, FALSE, FALSE, 8);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT &&
        *gtk_entry_get_text(GTK_ENTRY(name))) {
        g_autoptr(Game) game = game_new(gtk_entry_get_text(GTK_ENTRY(name)));
        game->executable = g_strdup(path);
        game->managed_files = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(copy));
        game->ready = !game->managed_files;
        g_autoptr(GError) error = NULL;
        if (!game_save(game, &error) ||
            (!game->managed_files && !game_write_shortcuts(game, &error)))
            show_error(ui, error->message);
        else {
            g_autofree char *id = g_strdup(game->id);
            gtk_widget_destroy(dialog);
            if (game->managed_files) begin_copy(ui, game);
            else refresh(ui, id);
            return;
        }
    }
    gtk_widget_destroy(dialog);
}

static void install_game(GtkButton *unused, Ui *ui)
{
    (void) unused;
    g_autofree char *path = choose_executable(ui, "Choose setup.exe from a disc or folder", NULL);
    if (!path) return;
    g_autofree char *base = g_path_get_basename(path);
    g_autoptr(Game) game = game_new(base);
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Install game", GTK_WINDOW(ui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel", GTK_RESPONSE_CANCEL,
        "Install", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(area), 16);
    GtkWidget *label = gtk_label_new("Name of the game on this disc:");
    gtk_box_pack_start(GTK_BOX(area), label, FALSE, FALSE, 8);
    GtkWidget *entry = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(entry), base);
    gtk_box_pack_start(GTK_BOX(area), entry, FALSE, FALSE, 8);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT &&
        *gtk_entry_get_text(GTK_ENTRY(entry))) {
        g_free(game->name); game->name = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
        game->installer = g_strdup(path);
        g_autoptr(GError) error = NULL;
        if (!game_save(game, &error)) show_error(ui, error->message);
        else {
            g_autofree char *id = g_strdup(game->id);
            gtk_widget_destroy(dialog);
            start_game(ui, game, game->installer, TRUE);
            refresh(ui, id);
            return;
        }
    }
    gtk_widget_destroy(dialog);
}

static void window_destroy(GtkWidget *widget, Ui *ui)
{
    (void) widget;
    ui->window = NULL; ui->list = NULL; ui->detail = NULL;
    ui->selected = NULL;
    g_clear_pointer(&ui->games, g_ptr_array_unref);
}

static void ui_free(Ui *ui)
{
    g_clear_pointer(&ui->games, g_ptr_array_unref);
    g_free(ui);
}

GHashTable *ui_running_games(GtkApplication *application)
{
    GHashTable *running = g_object_get_data(G_OBJECT(application), "paladin-running");
    if (!running) {
        running = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        g_object_set_data_full(G_OBJECT(application), "paladin-running", running,
                               (GDestroyNotify) g_hash_table_unref);
    }
    return running;
}

void ui_refresh_game(GtkApplication *application, const char *id)
{
    Ui *ui = g_object_get_data(G_OBJECT(application), "paladin-ui");
    if (ui && ui->window) refresh(ui, id);
}

void ui_activate(GtkApplication *application)
{
    Ui *ui = g_object_get_data(G_OBJECT(application), "paladin-ui");
    if (!ui) {
        ui = g_new0(Ui, 1);
        ui->app = application;
        ui->running = ui_running_games(application);
        g_object_set_data_full(G_OBJECT(application), "paladin-ui", ui, (GDestroyNotify) ui_free);
    }
    if (ui->window) { gtk_window_present(GTK_WINDOW(ui->window)); return; }
    ui->window = xapp_gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_application_add_window(application, GTK_WINDOW(ui->window));
    xapp_gtk_window_set_icon_name(XAPP_GTK_WINDOW(ui->window), "applications-games");
    gtk_window_set_title(GTK_WINDOW(ui->window), "Paladin Launcher");
    gtk_window_set_default_size(GTK_WINDOW(ui->window), 860, 550);
    gtk_window_set_position(GTK_WINDOW(ui->window), GTK_WIN_POS_CENTER);
    g_signal_connect(ui->window, "destroy", G_CALLBACK(window_destroy), ui);

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        ".paladin-detail { padding: 28px; }"
        ".game-title { font-size: 24px; font-weight: bold; }"
        ".paladin-sidebar { border-right: 1px solid alpha(@theme_fg_color, 0.12); }",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "Paladin Launcher");
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header), "Windows games with GE-Proton");
    gtk_window_set_titlebar(GTK_WINDOW(ui->window), header);
    GtkWidget *add = button("Add EXE", "list-add-symbolic");
    GtkWidget *install = button("Install", "media-optical-symbolic");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), add);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), install);
    g_signal_connect(add, "clicked", G_CALLBACK(add_executable), ui);
    g_signal_connect(install, "clicked", G_CALLBACK(install_game), ui);

    GtkWidget *layout = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(ui->window), layout);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scroll, 270, -1);
    gtk_style_context_add_class(gtk_widget_get_style_context(scroll), "paladin-sidebar");
    gtk_box_pack_start(GTK_BOX(layout), scroll, FALSE, FALSE, 0);
    ui->list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ui->list), GTK_SELECTION_SINGLE);
    gtk_container_add(GTK_CONTAINER(scroll), ui->list);
    g_signal_connect(ui->list, "row-selected", G_CALLBACK(selection_changed), ui);
    ui->detail = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(ui->detail), "paladin-detail");
    gtk_box_pack_start(GTK_BOX(layout), ui->detail, TRUE, TRUE, 0);
    refresh(ui, NULL);
    gtk_widget_show_all(ui->window);
}
