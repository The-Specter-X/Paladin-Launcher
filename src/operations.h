#pragma once
#include "game.h"

#define PALADIN_TYPE_OPERATIONS (operations_get_type())
G_DECLARE_FINAL_TYPE(Operations, operations, PALADIN, OPERATIONS, GObject)

typedef enum {
    OP_NONE, OP_PLAY, OP_INSTALL, OP_UTILITY, OP_IMPORT, OP_SCAN, OP_REMOVE, OP_RESET
} OperationKind;

/* Signals: changed(id), completed(id, kind, error_message, cancelled). All on the main thread. */
Operations *operations_new(GApplication *application);
gboolean operations_busy(Operations *self, const char *id);
const char *operations_status(Operations *self, const char *id);
gboolean operations_run(Operations *self, const char *id, const char *path,
                         OperationKind kind, GError **error);
gboolean operations_start(Operations *self, const char *id, OperationKind kind, GError **error);
void operations_stop(Operations *self, const char *id);
GPtrArray *operations_candidates(Operations *self, const char *id); /* owned reference */
gboolean operations_save_settings(Operations *self, const Game *edited, GError **error);
gboolean operations_choose_executable(Operations *self, const char *id,
                                       const char *path, GError **error);
