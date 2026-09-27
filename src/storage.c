#define _GNU_SOURCE
#include "storage.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static gboolean io_error(GError **error, const char *path)
{
    int saved = errno;
    g_set_error(error, G_IO_ERROR, g_io_error_from_errno(saved), "%s: %s",
                path, g_strerror(saved));
    return FALSE;
}

char *storage_resolve(const char *path, GError **error)
{
    if (!path || !g_path_is_absolute(path)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Choose an absolute local path");
        return NULL;
    }
    char *resolved = realpath(path, NULL);
    if (resolved) return resolved;
    if (errno != ENOENT) { io_error(error, path); return NULL; }
    g_autofree char *parent = g_path_get_dirname(path);
    if (g_str_equal(parent, path)) { io_error(error, path); return NULL; }
    g_autofree char *base = g_path_get_basename(path);
    g_autofree char *root = storage_resolve(parent, error);
    return root ? g_canonicalize_filename(base, root) : NULL;
}

static gboolean cancelled(GCancellable *cancel, GError **error)
{
    return cancel && g_cancellable_set_error_if_cancelled(cancel, error);
}

/* Descriptor-relative removal never traverses a directory replaced by a link. */
static gboolean remove_at(int parent, const char *name, GCancellable *cancel,
                           guint depth, GError **error)
{
    if (cancelled(cancel, error)) return FALSE;
    struct stat st;
    if (fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW) != 0)
        return errno == ENOENT || io_error(error, name);
    if (!S_ISDIR(st.st_mode))
        return unlinkat(parent, name, 0) == 0 || errno == ENOENT || io_error(error, name);
    if (depth > 128) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Directory nesting is too deep");
        return FALSE;
    }
    int fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return io_error(error, name);
    DIR *dir = fdopendir(fd);
    if (!dir) { close(fd); return io_error(error, name); }
    gboolean ok = TRUE;
    struct dirent *item;
    errno = 0;
    while ((item = readdir(dir))) {
        if (g_str_equal(item->d_name, ".") || g_str_equal(item->d_name, "..")) continue;
        if (!remove_at(fd, item->d_name, cancel, depth + 1, error)) { ok = FALSE; break; }
        errno = 0;
    }
    if (ok && errno) ok = io_error(error, name);
    closedir(dir);
    if (ok && cancelled(cancel, error)) return FALSE;
    return ok && (unlinkat(parent, name, AT_REMOVEDIR) == 0 || errno == ENOENT || io_error(error, name));
}

gboolean storage_remove(const char *path, GCancellable *cancel, GError **error)
{
    g_autofree char *parent = g_path_get_dirname(path);
    g_autofree char *base = g_path_get_basename(path);
    int fd = open(parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return errno == ENOENT || io_error(error, parent);
    gboolean ok = remove_at(fd, base, cancel, 0, error);
    close(fd);
    return ok;
}

static gboolean copy_tree(GFile *source, GFile *target, GCancellable *cancel,
                          guint depth, GError **error)
{
    if (cancelled(cancel, error)) return FALSE;
    if (depth > 64) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Directory nesting is too deep");
        return FALSE;
    }
    g_autoptr(GFileInfo) info = g_file_query_info(source, G_FILE_ATTRIBUTE_STANDARD_TYPE,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel, error);
    if (!info) return FALSE;
    GFileType type = g_file_info_get_file_type(info);
    if (type != G_FILE_TYPE_DIRECTORY && type != G_FILE_TYPE_REGULAR) {
        g_autofree char *path = g_file_get_path(source);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                    "Cannot copy %s: symbolic links and special files are not copied. "
                    "Use the original folder as a linked game, or replace the link with a local file.", path);
        return FALSE;
    }
    g_autoptr(GError) query_error = NULL;
    g_autoptr(GFileInfo) existing = g_file_query_info(target, G_FILE_ATTRIBUTE_STANDARD_TYPE,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel, &query_error);
    if (!existing && !g_error_matches(query_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
        g_propagate_error(error, g_steal_pointer(&query_error));
        return FALSE;
    }
    if (existing && g_file_info_get_file_type(existing) != type) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                            "The destination contains an unexpected file or symbolic link");
        return FALSE;
    }
    if (type == G_FILE_TYPE_REGULAR)
        return g_file_copy(source, target, G_FILE_COPY_NOFOLLOW_SYMLINKS | G_FILE_COPY_OVERWRITE,
                           cancel, NULL, NULL, error);
    if (!existing && !g_file_make_directory_with_parents(target, cancel, error)) return FALSE;
    g_autoptr(GFileEnumerator) entries = g_file_enumerate_children(source,
        G_FILE_ATTRIBUTE_STANDARD_NAME, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel, error);
    if (!entries) return FALSE;
    g_autoptr(GFileInfo) item = NULL;
    while ((item = g_file_enumerator_next_file(entries, cancel, error))) {
        g_autoptr(GFile) from = g_file_get_child(source, g_file_info_get_name(item));
        g_autoptr(GFile) to = g_file_get_child(target, g_file_info_get_name(item));
        if (!copy_tree(from, to, cancel, depth + 1, error)) return FALSE;
        g_clear_object(&item);
    }
    return !(error && *error);
}

gboolean storage_copy(const char *source, const char *target,
                      GCancellable *cancel, GError **error)
{
    g_autofree char *from_path = storage_resolve(source, error);
    if (!from_path) return FALSE;
    g_autofree char *to_path = storage_resolve(target, error);
    if (!to_path) return FALSE;
    g_autoptr(GFile) from = g_file_new_for_path(from_path);
    g_autoptr(GFile) to = g_file_new_for_path(to_path);
    if (g_file_equal(from, to) || g_file_has_prefix(to, from) || g_file_has_prefix(from, to)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Source and destination folders overlap. Choose a separate game folder.");
        return FALSE;
    }
    return copy_tree(from, to, cancel, 0, error);
}

char *storage_read_tail(const char *path, gsize limit, GError **error)
{
    g_autoptr(GFile) file = g_file_new_for_path(path);
    g_autoptr(GFileInputStream) stream = g_file_read(file, NULL, error);
    if (!stream) return NULL;
    g_autoptr(GFileInfo) info = g_file_input_stream_query_info(stream,
        G_FILE_ATTRIBUTE_STANDARD_SIZE, NULL, error);
    if (!info) return NULL;
    goffset size = g_file_info_get_size(info);
    goffset offset = MAX((goffset) 0, size - (goffset) limit);
    if (!g_seekable_seek(G_SEEKABLE(stream), offset, G_SEEK_SET, NULL, error)) return NULL;
    g_autofree char *buffer = g_malloc(limit + 1);
    gsize read = 0;
    if (!g_input_stream_read_all(G_INPUT_STREAM(stream), buffer, limit, &read, NULL, error)) return NULL;
    /* Embedded NULs and invalid Windows encodings must not truncate GTK text. */
    for (gsize i = 0; i < read; i++) if (!buffer[i]) buffer[i] = ' ';
    buffer[read] = '\0';
    g_autofree char *valid = g_utf8_make_valid(buffer, read);
    return g_strconcat(offset ? "[Showing the end of this log]\n" : "", valid, NULL);
}
