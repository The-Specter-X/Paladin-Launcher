#include "dialogs.h"
#include "runner.h"

void dialogs_message(GtkWindow *parent, GtkMessageType type, const char *text)
{
    g_autoptr(GtkWidget) dialog = g_object_ref_sink(gtk_message_dialog_new(parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, type, GTK_BUTTONS_CLOSE, "%s", text));
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

gboolean dialogs_confirm(GtkWindow *parent, const char *title, const char *detail, const char *action)
{
    g_autoptr(GtkWidget) dialog = g_object_ref_sink(gtk_message_dialog_new(parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING,
        GTK_BUTTONS_NONE, "%s", title));
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", detail);
    gtk_dialog_add_buttons(GTK_DIALOG(dialog), "Cancel", GTK_RESPONSE_CANCEL, action, GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return response == GTK_RESPONSE_ACCEPT;
}

char *dialogs_choose(GtkWindow *parent, const char *title, const char *directory, gboolean folder)
{
    g_autoptr(GtkWidget) chooser = g_object_ref_sink(gtk_file_chooser_dialog_new(title, parent,
        folder ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER : GTK_FILE_CHOOSER_ACTION_OPEN,
        "Cancel", GTK_RESPONSE_CANCEL, "Select", GTK_RESPONSE_ACCEPT, NULL));
    gtk_window_set_destroy_with_parent(GTK_WINDOW(chooser), TRUE);
    gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(chooser), TRUE);
    if (directory && *directory) gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser), directory);
    if (!folder) {
        GtkFileFilter *exe = gtk_file_filter_new();
        gtk_file_filter_set_name(exe, "Windows executables (*.exe)");
        gtk_file_filter_add_pattern(exe, "*.[eE][xX][eE]");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), exe);
        GtkFileFilter *all = gtk_file_filter_new();
        gtk_file_filter_set_name(all, "All files");
        gtk_file_filter_add_pattern(all, "*");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), all);
    }
    char *path = NULL;
    if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT)
        path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
    gtk_widget_destroy(chooser);
    return path;
}

static GtkWidget *entry(GtkWidget *box, const char *label, const char *text)
{
    GtkWidget *caption = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(caption), 0);
    gtk_box_pack_start(GTK_BOX(box), caption, FALSE, FALSE, 0);
    GtkWidget *widget = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(widget), text ? text : "");
    gtk_box_pack_start(GTK_BOX(box), widget, FALSE, FALSE, 0);
    return widget;
}

static GtkWidget *check(GtkWidget *box, const char *label, gboolean active)
{
    GtkWidget *widget = gtk_check_button_new_with_label(label);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), active);
    gtk_box_pack_start(GTK_BOX(box), widget, FALSE, FALSE, 0);
    return widget;
}

typedef struct { GtkWidget *wayland, *wined3d, *fsr; } Compatibility;
static void compatibility_changed(GtkToggleButton *unused, Compatibility *widgets)
{
    (void) unused;
    gboolean supported = !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widgets->wayland)) &&
        !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widgets->wined3d));
    if (!supported) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets->fsr), FALSE);
    gtk_widget_set_sensitive(widgets->fsr, supported);
}

static void select_runner(GtkButton *button, GtkEntry *entry_widget)
{
    const char *path = g_object_get_data(G_OBJECT(button), "runner-path");
    if (path) gtk_entry_set_text(entry_widget, path);
}

static void browse_runner(GtkButton *button, GtkEntry *entry_widget)
{
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
    g_autofree char *path = dialogs_choose(parent, "Select installed Proton directory", NULL, TRUE);
    if (path) gtk_entry_set_text(entry_widget, path);
}

void dialogs_settings(GtkWindow *parent, Operations *operations, const char *id)
{
    g_autoptr(GError) error = NULL;
    /* This record is owned by this dialog, independently of library refreshes. */
    g_autoptr(Game) game = game_load(id, &error);
    if (!game) { dialogs_message(parent, GTK_MESSAGE_ERROR, error->message); return; }
    g_autoptr(GtkWidget) dialog = g_object_ref_sink(gtk_dialog_new_with_buttons("Game settings", parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel", GTK_RESPONSE_CANCEL,
        "Save", GTK_RESPONSE_ACCEPT, NULL));
    gtk_window_set_default_size(GTK_WINDOW(dialog), 620, 640);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scroll, TRUE, TRUE, 0);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 9);
    gtk_container_set_border_width(GTK_CONTAINER(box), 20);
    gtk_container_add(GTK_CONTAINER(scroll), box);
    GtkWidget *name = entry(box, "Game title", game->name);
    GtkWidget *runner = entry(box, "Runner: GE-Proton for latest, or an installed directory", game->runner);
    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);
    const char *labels[] = {"Latest", "Keep last successful", "Previous successful"};
    const char *paths[] = {"GE-Proton", game->last_runner, game->previous_runner};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++) {
        GtkWidget *button = gtk_button_new_with_label(labels[i]);
        gtk_box_pack_start(GTK_BOX(buttons), button, FALSE, FALSE, 0);
        g_object_set_data_full(G_OBJECT(button), "runner-path", g_strdup(paths[i]), g_free);
        gtk_widget_set_sensitive(button, paths[i] && *paths[i]);
        g_signal_connect(button, "clicked", G_CALLBACK(select_runner), runner);
    }
    GtkWidget *browse = gtk_button_new_with_label("Browse…");
    gtk_box_pack_start(GTK_BOX(buttons), browse, FALSE, FALSE, 0);
    g_signal_connect(browse, "clicked", G_CALLBACK(browse_runner), runner);
    g_autofree char *version = g_strdup_printf("Last successful version: %s",
        game->last_version && *game->last_version ? game->last_version : "Not recorded yet");
    GtkWidget *version_label = gtk_label_new(version);
    gtk_label_set_xalign(GTK_LABEL(version_label), 0);
    gtk_box_pack_start(GTK_BOX(box), version_label, FALSE, FALSE, 0);
    GtkWidget *working = entry(box, "Working directory (empty uses the EXE's folder)", game->working_dir);
    GtkWidget *arguments = entry(box, "Game arguments (quotes group words; no shell commands)", game->arguments);
    GtkWidget *umu = entry(box, "Optional umu game ID for compatibility fixes", game->umu_id);
    gtk_entry_set_placeholder_text(GTK_ENTRY(umu), "umu-… (leave empty for an unidentified game)");
    Compatibility compat;
    compat.wayland = check(box, "Native Wine Wayland (disable for XWayland)", game->wayland);
    GtkWidget *wow64 = check(box, "New WoW64 mode", game->wow64);
    gboolean prefix_exists = game_prefix_exists(game);
    gtk_widget_set_sensitive(wow64, !prefix_exists);
    gtk_widget_set_tooltip_text(wow64, "For an existing prefix, use Back up / reset prefix before changing architecture.");
    compat.wined3d = check(box, "WineD3D OpenGL fallback (not for DirectX 12)", game->wined3d);
    compat.fsr = check(box, "Fullscreen FSR (XWayland and Vulkan only)", game->fsr);
    g_signal_connect(compat.wayland, "toggled", G_CALLBACK(compatibility_changed), &compat);
    g_signal_connect(compat.wined3d, "toggled", G_CALLBACK(compatibility_changed), &compat);
    compatibility_changed(NULL, &compat);
    GtkWidget *nvapi = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(nvapi), "NVAPI: automatic runner defaults");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(nvapi), "NVAPI: disabled");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(nvapi), "NVAPI: forced");
    gtk_combo_box_set_active(GTK_COMBO_BOX(nvapi), game->nvapi);
    gtk_box_pack_start(GTK_BOX(box), nvapi, FALSE, FALSE, 0);
    GtkWidget *desktop = check(box, "Create a desktop shortcut", game->desktop_shortcut);
    GtkWidget *label = gtk_label_new("Advanced environment (NAME=value, one per line)");
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    GtkWidget *env_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(env_scroll, -1, 110);
    GtkWidget *env = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(env), TRUE);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(env)), game->environment, -1);
    gtk_container_add(GTK_CONTAINER(env_scroll), env);
    gtk_box_pack_start(GTK_BOX(box), env_scroll, FALSE, FALSE, 0);
    gtk_widget_show_all(dialog);
    while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
#define SET_ENTRY(field, widget) do { g_free(game->field); game->field = g_strdup(gtk_entry_get_text(GTK_ENTRY(widget))); } while (0)
        SET_ENTRY(name, name); SET_ENTRY(runner, runner); SET_ENTRY(arguments, arguments);
        SET_ENTRY(working_dir, working); SET_ENTRY(umu_id, umu);
#undef SET_ENTRY
        GtkTextIter start, end;
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(env));
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        g_free(game->environment);
        game->environment = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        game->wayland = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(compat.wayland));
        game->wow64 = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wow64));
        game->wined3d = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(compat.wined3d));
        game->fsr = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(compat.fsr));
        game->nvapi = gtk_combo_box_get_active(GTK_COMBO_BOX(nvapi));
        game->desktop_shortcut = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(desktop));
        g_clear_error(&error);
        if (operations_save_settings(operations, game, &error)) break;
        dialogs_message(GTK_WINDOW(dialog), GTK_MESSAGE_ERROR, error->message);
    }
    gtk_widget_destroy(dialog);
}
