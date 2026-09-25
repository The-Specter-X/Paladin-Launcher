#pragma once
#include <gtk/gtk.h>

void ui_activate(GtkApplication *application);
GHashTable *ui_running_games(GtkApplication *application);
void ui_refresh_game(GtkApplication *application, const char *id);
