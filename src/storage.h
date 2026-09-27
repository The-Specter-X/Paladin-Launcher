#pragma once
#include <gio/gio.h>

/* Resolves existing ancestors as well as a not-yet-created final path. */
char *storage_resolve(const char *path, GError **error);
gboolean storage_copy(const char *source, const char *target,
                      GCancellable *cancel, GError **error);
gboolean storage_remove(const char *path, GCancellable *cancel, GError **error);
char *storage_read_tail(const char *path, gsize limit, GError **error);
