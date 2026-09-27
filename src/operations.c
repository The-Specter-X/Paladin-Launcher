#include "operations.h"
#include "runner.h"
#include <signal.h>

typedef struct {
    Operations *owner;
    Game *game;
    OperationKind kind;
    GSubprocess *process;
    GCancellable *cancel;
    gboolean stopping;
} Job;

struct _Operations {
    GObject parent_instance;
    GApplication *application; /* Application owns us; active jobs hold its run loop. */
    GHashTable *jobs;
    GHashTable *candidates;
};
G_DEFINE_TYPE(Operations, operations, G_TYPE_OBJECT)
static guint changed_signal, completed_signal;

static void job_free(Job *job)
{
    game_free(job->game);
    g_clear_object(&job->process);
    g_clear_object(&job->cancel);
    g_free(job);
}

static void operations_finalize(GObject *object)
{
    Operations *self = PALADIN_OPERATIONS(object);
    g_hash_table_unref(self->jobs);
    g_hash_table_unref(self->candidates);
    G_OBJECT_CLASS(operations_parent_class)->finalize(object);
}

static void operations_class_init(OperationsClass *klass)
{
    G_OBJECT_CLASS(klass)->finalize = operations_finalize;
    changed_signal = g_signal_new("changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
        0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
    completed_signal = g_signal_new("completed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
        0, NULL, NULL, NULL, G_TYPE_NONE, 4, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING, G_TYPE_BOOLEAN);
}

static void operations_init(Operations *self)
{
    self->jobs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify) job_free);
    self->candidates = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_ptr_array_unref);
}

Operations *operations_new(GApplication *application)
{
    Operations *self = g_object_new(PALADIN_TYPE_OPERATIONS, NULL);
    self->application = application;
    return self;
}

gboolean operations_busy(Operations *self, const char *id)
{
    return id && g_hash_table_contains(self->jobs, id);
}

const char *operations_status(Operations *self, const char *id)
{
    Job *job = id ? g_hash_table_lookup(self->jobs, id) : NULL;
    if (!job) return NULL;
    if (job->stopping) return job->process ? "Stopping…" : "Cancelling…";
    switch (job->kind) {
    case OP_PLAY: return "Starting / running game…";
    case OP_INSTALL: return "Running installer…";
    case OP_UTILITY: return "Running another EXE…";
    case OP_IMPORT: return "Copying game files…";
    case OP_SCAN: return "Finding installed games…";
    case OP_REMOVE: return "Removing game files…";
    case OP_RESET: return "Backing up and resetting prefix…";
    default: return NULL;
    }
}

static Game *load_idle(Operations *self, const char *id, gboolean allow_removing, GError **error)
{
    if (operations_busy(self, id)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_BUSY,
                            "This game has an operation in progress. Stop it or wait for it to finish.");
        return NULL;
    }
    Game *game = game_load(id, error);
    if (game && game->removing && !allow_removing) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_BUSY,
                            "Removal is incomplete. Finish removing this entry first.");
        game_free(game);
        return NULL;
    }
    return game;
}

static Job *begin(Operations *self, Game *game, OperationKind kind)
{
    Job *job = g_new0(Job, 1);
    job->owner = g_object_ref(self);
    job->game = game;
    job->kind = kind;
    job->cancel = g_cancellable_new();
    g_hash_table_insert(self->jobs, g_strdup(game->id), job);
    if (self->application) g_application_hold(self->application);
    return job;
}

static void finish(Job *job, const char *message)
{
    Operations *self = job->owner;
    g_autofree char *id = g_strdup(job->game->id);
    g_autofree char *text = g_strdup(message);
    OperationKind kind = job->kind;
    gboolean stopped = job->stopping;
    g_hash_table_remove(self->jobs, id);
    g_signal_emit(self, changed_signal, 0, id);
    g_signal_emit(self, completed_signal, 0, id, kind, stopped ? NULL : text, stopped);
    if (self->application) g_application_release(self->application);
    g_object_unref(self);
}

static void run_finished(GObject *object, GAsyncResult *result, gpointer user_data)
{
    Job *job = user_data;
    g_autoptr(GError) error = NULL;
    g_autofree char *message = NULL;
    if (!g_subprocess_wait_finish(G_SUBPROCESS(object), result, &error))
        message = g_strdup(error->message);
    else message = runner_result(G_SUBPROCESS(object));
    if (!message && !job->stopping) {
        runner_record_success(job->game);
        if (!game_save(job->game, &error)) message = g_strdup(error->message);
    }
    finish(job, message);
}

gboolean operations_run(Operations *self, const char *id, const char *path,
                         OperationKind kind, GError **error)
{
    g_return_val_if_fail(kind == OP_PLAY || kind == OP_INSTALL || kind == OP_UTILITY, FALSE);
    g_autoptr(Game) game = load_idle(self, id, FALSE, error);
    if (!game) return FALSE;
    if (kind == OP_PLAY && !game->ready) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Finish setting up this game before playing");
        return FALSE;
    }
    const char *exe = path ? path : kind == OP_INSTALL ? game->installer : game->executable;
    gboolean previously_pending = game->discover_pending;
    if (kind == OP_INSTALL) {
        game->discover_pending = TRUE;
        if (!game_save(game, error)) return FALSE;
    }
    g_autoptr(GSubprocess) process = runner_start(game, exe, kind != OP_PLAY, error);
    if (!process) {
        if (kind == OP_INSTALL) {
            game->discover_pending = previously_pending;
            game_save(game, NULL);
        }
        return FALSE;
    }
    Job *job = begin(self, g_steal_pointer(&game), kind);
    job->process = g_steal_pointer(&process);
    g_subprocess_wait_async(job->process, NULL, run_finished, job);
    g_signal_emit(self, changed_signal, 0, id);
    return TRUE;
}

static void file_worker(GTask *task, gpointer object, gpointer task_data, GCancellable *cancel)
{
    (void) object;
    Job *job = task_data;
    g_autoptr(GError) error = NULL;
    gboolean ok = FALSE;
    switch (job->kind) {
    case OP_IMPORT: ok = game_import(job->game, cancel, &error); break;
    case OP_REMOVE: ok = game_remove_full(job->game, cancel, &error); break;
    case OP_RESET: ok = game_reset_prefix(job->game, cancel, &error); break;
    case OP_SCAN: {
        GPtrArray *items = game_find_executables_full(job->game, cancel, &error);
        if (items) g_task_return_pointer(task, items, (GDestroyNotify) g_ptr_array_unref);
        else g_task_return_error(task, g_steal_pointer(&error));
        return;
    }
    default: g_assert_not_reached();
    }
    if (ok) g_task_return_boolean(task, TRUE);
    else g_task_return_error(task, g_steal_pointer(&error));
}

static void file_finished(GObject *object, GAsyncResult *result, gpointer user_data)
{
    Operations *self = PALADIN_OPERATIONS(object);
    Job *job = user_data;
    g_autoptr(GError) error = NULL;
    if (job->kind == OP_SCAN) {
        GPtrArray *items = g_task_propagate_pointer(G_TASK(result), &error);
        if (items) g_hash_table_replace(self->candidates, g_strdup(job->game->id), items);
    } else g_task_propagate_boolean(G_TASK(result), &error);
    finish(job, error ? error->message : NULL);
}

gboolean operations_start(Operations *self, const char *id, OperationKind kind, GError **error)
{
    g_return_val_if_fail(kind >= OP_IMPORT && kind <= OP_RESET, FALSE);
    g_autoptr(Game) game = load_idle(self, id, kind == OP_REMOVE, error);
    if (!game) return FALSE;
    if (kind == OP_REMOVE) {
        game->removing = TRUE;
        game->ready = FALSE;
        if (!game_save(game, error)) return FALSE;
    }
    if (kind == OP_SCAN || kind == OP_REMOVE) g_hash_table_remove(self->candidates, id);
    Job *job = begin(self, g_steal_pointer(&game), kind);
    GTask *task = g_task_new(self, job->cancel, file_finished, job);
    /* Do not report cancellation until the worker has actually stopped mutating files. */
    g_task_set_return_on_cancel(task, FALSE);
    g_task_set_task_data(task, job, NULL);
    g_task_run_in_thread(task, file_worker);
    g_object_unref(task);
    g_signal_emit(self, changed_signal, 0, id);
    return TRUE;
}

void operations_stop(Operations *self, const char *id)
{
    Job *job = g_hash_table_lookup(self->jobs, id);
    if (!job || job->stopping) return;
    job->stopping = TRUE;
    if (job->process) g_subprocess_send_signal(job->process, SIGTERM);
    else g_cancellable_cancel(job->cancel);
    g_signal_emit(self, changed_signal, 0, id);
}

GPtrArray *operations_candidates(Operations *self, const char *id)
{
    GPtrArray *items = g_hash_table_lookup(self->candidates, id);
    return items ? g_ptr_array_ref(items) : NULL;
}

gboolean operations_save_settings(Operations *self, const Game *edited, GError **error)
{
    g_autoptr(Game) game = load_idle(self, edited->id, FALSE, error);
    if (!game || !runner_validate(edited, error)) return FALSE;
    /* Only editable fields are copied. Completion/recovery metadata remains current. */
#define COPY_FIELD(field) do { g_free(game->field); game->field = g_strdup(edited->field); } while (0)
    COPY_FIELD(name); COPY_FIELD(runner); COPY_FIELD(arguments); COPY_FIELD(environment);
    COPY_FIELD(working_dir); COPY_FIELD(umu_id);
#undef COPY_FIELD
    game->wayland = edited->wayland;
    game->wow64 = edited->wow64;
    game->fsr = edited->fsr;
    game->nvapi = edited->nvapi;
    game->wined3d = edited->wined3d;
    game->desktop_shortcut = edited->desktop_shortcut;
    gboolean ok = game_save(game, error) && game_write_shortcuts(game, error);
    g_signal_emit(self, changed_signal, 0, game->id);
    return ok;
}

gboolean operations_choose_executable(Operations *self, const char *id,
                                       const char *path, GError **error)
{
    g_autoptr(Game) game = load_idle(self, id, FALSE, error);
    if (!game) return FALSE;
    if (!path || !g_path_is_absolute(path) || !g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Choose an existing local executable");
        return FALSE;
    }
    g_free(game->executable); game->executable = g_strdup(path);
    g_free(game->working_dir); game->working_dir = g_path_get_dirname(path);
    if (!game->root || !*game->root) { g_free(game->root); game->root = g_path_get_dirname(path); }
    game->ready = TRUE;
    game->discover_pending = FALSE;
    gboolean ok = game_save(game, error) && game_write_shortcuts(game, error);
    g_signal_emit(self, changed_signal, 0, id);
    return ok;
}
