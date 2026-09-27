# Development and verification

## Module boundaries

- `game`: owned records, backward-compatible metadata, shortcuts, import/reset
  and discovery. No GTK dependency.
- `storage`: canonical paths, copy validation, descriptor-relative recursive
  deletion and bounded log reading.
- `runner`: option validation, controlled child environment, preflight, umu
  process creation, termination results and last-successful runner tracking.
- `operations`: one active job per immutable game ID, main-thread lifecycle
  signals, background file workers, cancellation and fresh-record settings merges.
- `dialogs`: independent owned settings snapshots and local file pickers.
- `ui`: selection by ID, operation presentation, local-media workflows and
  recovery. `main` uses the same controller for shortcuts and the GUI.

Records returned by `game_load` and `game_copy` are owned by the caller. Dialogs
must not hold pointers into refreshed rows. After a nested GTK dialog returns,
mutations go through Operations, which reloads the captured ID and checks busy
state again. Settings updates copy only editable fields into the current record.

Each active job owns its record and holds the application until completion.
Workers do not touch GTK. Cancellation never releases the busy state before the
worker exits. `completed(id, kind, error_message, cancelled)` distinguishes a
user stop from success, so stopping an installer cannot trigger a success flow.
The single GtkApplication/D-Bus instance coordinates normal GUI and CLI clients;
manual metadata edits while jobs are active are unsupported.

Deletion keeps a persisted tombstone until all cleanup succeeds. It never follows
links encountered inside the removal tree. Managed copying rejects nested links
and special files instead of silently losing assets. These mechanisms prevent
routine destructive mistakes; they are not an adversarial sandbox for games
running as the same user. No files outside managed storage are deliberately
removed, except Paladin's own per-game logs and shortcuts.

## Automated coverage

Core tests exercise metadata/shortcuts, separate prefixes, link-safe removal,
copy overlap (including aliases), nested EXE roots, explicit symlink rejection,
cancellation/retry, tombstones and filesystem errors, bounded invalid-UTF-8 logs,
publisher-folder discovery, quoted arguments, NVAPI defaults, child environments,
concurrent games, busy guards, failed/signalled launches, stale settings merges,
option conflicts and backed-up architecture changes.

The GUI test opens actual GTK dialogs on Wayland. While Settings and Remove
are waiting in nested GTK loops, it refreshes the library and changes selection.
It verifies saved operational metadata survives and only the captured game is
removed. It also closes/reopens the window and completes persisted installer
launcher discovery. CI runs both ordinary and ASan/UBSan builds. Leak detection
is disabled in CI because GTK/system libraries retain process-global state;
address and undefined-behaviour checks remain fatal.

## Hardware testing before a release

CI does not certify Proton compatibility. Test a current umu/GE-Proton pair with:

1. Local 32-bit and 64-bit games, DX9/DX11/DX12; installer, portable and patched
   games. Include nested EXEs, publisher install paths and multi-disc media.
2. Native Wine Wayland and XWayland on AMD/Intel/NVIDIA; input capture,
   controllers, audio, fullscreen, high DPI, multiple monitors and NVAPI.
3. New WoW64 and a backed-up reset to the traditional mode where needed.
4. Simultaneous games with different arguments/options; Stop while umu prepares
   a runtime and while a game is running. Ensure another game's process survives.
5. Disconnect networking after preparing the shared runner/runtime; launch local
   media games with latest and retained runner selections. Missing runtimes must
   fail visibly. This is not a game-download workflow.
6. Unmount source media, fill a disk, cancel a large import, deny filesystem
   access and retry removal. Check data/backup retention and menu shortcuts.
7. Run from a desktop shortcut without a library window; verify normal exit,
   launch failure and process termination. Desktop trust prompts are desktop
   environment policy.

The target is Debian 14 / LMDE 8; Ubuntu CI is a development test host, not a
claim that the target distribution's final graphics/runtime stack is validated.
Distribution packaging, artwork/AppStream and driver installation remain out of
scope for this change.
