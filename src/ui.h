#pragma once
#include <gtk/gtk.h>
#include "operations.h"

Operations *ui_operations(GtkApplication *application); /* borrowed */
void ui_prepare(GtkApplication *application);
void ui_activate(GtkApplication *application);
void ui_show_failure(GtkApplication *application, const char *id, const char *message);
