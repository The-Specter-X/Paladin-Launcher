#pragma once
#include <gtk/gtk.h>
#include "operations.h"

void dialogs_message(GtkWindow *parent, GtkMessageType type, const char *text);
char *dialogs_choose(GtkWindow *parent, const char *title, const char *directory, gboolean folder);
gboolean dialogs_confirm(GtkWindow *parent, const char *title, const char *detail, const char *action);
void dialogs_settings(GtkWindow *parent, Operations *operations, const char *id);
