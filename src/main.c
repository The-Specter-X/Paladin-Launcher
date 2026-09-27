#include "ui.h"

static int command_line(GApplication *application, GApplicationCommandLine *command, gpointer unused)
{
    (void) unused;
    gint argc = 0;
    g_auto(GStrv) argv = g_application_command_line_get_arguments(command, &argc);
    if (argc == 1) { g_application_activate(application); return 0; }
    if (argc == 2 && g_str_equal(argv[1], "--help")) {
        g_application_command_line_print(command, "Usage: paladin-launcher [--play GAME-ID]\n");
        return 0;
    }
    if (argc != 3 || !g_str_equal(argv[1], "--play")) {
        g_application_command_line_printerr(command, "Usage: paladin-launcher [--play GAME-ID]\n");
        return 2;
    }
    g_autoptr(GError) error = NULL;
    GtkApplication *app = GTK_APPLICATION(application);
    if (!operations_run(ui_operations(app), argv[2], NULL, OP_PLAY, &error)) {
        g_application_command_line_printerr(command, "Paladin: %s\n", error->message);
        ui_show_failure(app, argv[2], error->message);
        return 1;
    }
    return 0;
}

static void startup(GApplication *application, gpointer unused)
{
    (void) unused;
    ui_prepare(GTK_APPLICATION(application));
}

int main(int argc, char **argv)
{
    g_autoptr(GtkApplication) app = gtk_application_new("io.github.thespecterx.PaladinLauncher",
                                                       G_APPLICATION_HANDLES_COMMAND_LINE);
    g_signal_connect(app, "startup", G_CALLBACK(startup), NULL);
    g_signal_connect(app, "activate", G_CALLBACK(ui_activate), NULL);
    g_signal_connect(app, "command-line", G_CALLBACK(command_line), NULL);
    return g_application_run(G_APPLICATION(app), argc, argv);
}
