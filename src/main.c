#include "game.h"
#include "runner.h"
#include "ui.h"

#include <gtk/gtk.h>

static void cli_finished(GObject *object, GAsyncResult *result, gpointer user_data)
{
    GtkApplication *application = user_data;
    g_autoptr(GError) error = NULL;
    if (!g_subprocess_wait_finish(G_SUBPROCESS(object), result, &error))
        g_printerr("Paladin: %s\n", error->message);
    else if (!g_subprocess_get_successful(G_SUBPROCESS(object)))
        g_printerr("Paladin: game exited with status %d\n",
                   g_subprocess_get_exit_status(G_SUBPROCESS(object)));
    g_application_release(G_APPLICATION(application));
}

static int command_line(GApplication *application, GApplicationCommandLine *command,
                        gpointer unused)
{
    (void) unused;
    gint argc = 0;
    g_auto(GStrv) argv = g_application_command_line_get_arguments(command, &argc);
    if (argc == 1) {
        g_application_activate(application);
        return 0;
    }
    if (argc != 3 || !g_str_equal(argv[1], "--play")) {
        g_application_command_line_printerr(command, "Usage: paladin-launcher [--play GAME-ID]\n");
        return 2;
    }
    g_autoptr(GError) error = NULL;
    g_autoptr(Game) game = game_load(argv[2], &error);
    if (!game || !game->ready || !game->executable || !*game->executable) {
        g_application_command_line_printerr(command, "Paladin: %s\n",
            error ? error->message : "Game is not ready");
        return 1;
    }
    g_autoptr(GSubprocess) process = runner_start(game, game->executable, FALSE, &error);
    if (!process) {
        g_application_command_line_printerr(command, "Paladin: %s\n", error->message);
        return 1;
    }
    g_application_hold(application);
    g_subprocess_wait_async(process, NULL, cli_finished, application);
    return 0;
}

int main(int argc, char **argv)
{
    g_autoptr(GtkApplication) app = gtk_application_new(
        "io.github.thespecterx.PaladinLauncher", G_APPLICATION_HANDLES_COMMAND_LINE);
    g_signal_connect(app, "activate", G_CALLBACK(ui_activate), NULL);
    g_signal_connect(app, "command-line", G_CALLBACK(command_line), NULL);
    return g_application_run(G_APPLICATION(app), argc, argv);
}
