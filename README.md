# Paladin Launcher

A C / GTK 3 / XApp launcher for Windows games on a Wayland desktop, targeting
Debian 14 and LMDE 8. GE-Proton runs through [umu-launcher](https://github.com/Open-Wine-Components/umu-launcher).
Steam and a separate system Wine installation are not required.

## Local games only

Paladin installs games from **local EXEs, game folders and mounted CD/DVD media**.
Mount an ISO with your desktop's disk tools first. There is no storefront, game
catalogue or game download manager.

- **Existing game:** select the EXE, then its whole game folder. For an EXE inside
  `bin/`, choose the parent containing the assets too. Link the original folder
  or copy it into managed storage. Copying happens in the background; overlap
  and symbolic links are reported explicitly. Linked games may use symlinks.
- **Installer:** select local `setup.exe`, create the entry, optionally adjust
  Settings, then Run installer. Choose the installed launcher afterward. The
  installer, game, patches and component installers all use the same prefix.
- **Run another EXE in this game:** apply a local patch or component installer
  without replacing the game's normal launcher or creating another prefix.

GE-Proton and umu's Linux runtime are separate from game media. umu may obtain
or update these shared compatibility components over the network. **Prepare
and test the runtime before disconnecting.** Paladin does not bundle runtimes
or promise a first launch without network access. Runtime information reports
missing umu, display availability and the Vulkan loader; it is not a GPU
compatibility test. Distribution packaging and driver integration are deferred.

## Per-game settings and controls

- Native Wine Wayland and new WoW64 default on. Disable native Wayland for a
  game's XWayland fallback. The desktop can remain Wayland-only; XWayland must
  be available in that session for the fallback. Disabling WoW64 can require
  the runner's traditional 32-bit dependencies.
- `GE-Proton` follows umu's latest release selection. Paladin records the
  prefix's runner version after successful execution. **Keep last successful**
  and **Previous successful** select those installed runner directories; Browse
  accepts another locally installed compatible Proton. No extra runner copies
  are made. A missing retained directory must be restored or replaced.
- DXVK and VKD3D-Proton come with GE-Proton. WineD3D offers an OpenGL fallback
  for older games. NVAPI defaults to automatic, with disabled/forced overrides.
  Fullscreen FSR requires XWayland and Vulkan; incompatible controls are disabled.
- Optional umu game ID enables identified-game compatibility fixes. Leaving it
  empty uses `umu-default`. Arguments are parsed as words, without executing a
  shell. Custom environment uses one `NAME=value` per line. Variables controlled
  by Paladin are rejected to prevent conflicting configuration.
- Proton's synchronization defaults are used; no obsolete Esync/Fsync toggles.
- Different games can run concurrently. Each game's operations are serialized;
  settings, removal and launcher changes are blocked while that game is busy.
  Stop sends SIGTERM to umu, which handles its process tree. A game remains busy
  until the process exits. Copying, scanning and removal are cancellable.
- Search the library; the selected game is remembered. Game files and Prefix
  open separately. Menu shortcuts and optional desktop shortcuts launch directly
  through `paladin-launcher --play GAME-ID`. Runtime failures appear in the UI,
  including launches started from shortcuts.

## Recovery and data

Closing the window lets active operations finish. Pending installer discovery
survives reopening, and Choose EXE is always available for manual correction.
Discovery covers publisher folders on C:, displays relative paths and filters
common helper programs. Working directory and arguments can be adjusted for
launchers that need them. Executable discovery is best effort, not an installer
registry or shortcut parser.

Interrupted copies leave a retryable entry. Interrupted removals leave an
explicit removal-pending entry until data, logs and shortcuts are cleaned up.
Linked files outside the managed game directory are kept. Saves may also live
outside the prefix; review a game's save locations before removal.

WoW64 mode is locked once a prefix exists. **Back up / reset prefix** moves the
old prefix and a metadata snapshot into the game's backups folder before
recreation. Reinstall prefix-based games from the original media after changing
mode. Portable files remain in place. Open last prefix backup exposes old saves.
These backups are local recovery copies, not protection against disk failure;
removing the game also deletes them. To restore manually, close Paladin and the
game, preserve any newer prefix, restore the backup `prefix` to the game's data
directory and its `game.ini` to the game's metadata path, then reopen Paladin.

| Data | Location |
| --- | --- |
| Game metadata | `$XDG_CONFIG_HOME/paladin-launcher/games/GAME-ID.ini` |
| Remembered selection | `$XDG_CONFIG_HOME/paladin-launcher/ui.ini` |
| Prefix, copied files and backups | `$XDG_DATA_HOME/paladin-launcher/games/GAME-ID/` |
| Menu shortcuts | `$XDG_DATA_HOME/applications/paladin-game-GAME-ID.desktop` |
| Launcher logs and Proton diagnostics | `$XDG_STATE_HOME/paladin-launcher/logs/` |
| Shared runner/runtime | umu's XDG data/cache locations |

Unset XDG variables use GLib's standard defaults. Old cache logs remain readable
and are cleaned up on removal. The log viewer loads a bounded UTF-8-safe tail in
a background task. Launch stdout/stderr files contain the most recent run;
upstream diagnostics can consume additional disk space. Shared runtimes are not
removed when deleting a game. Prefix isolation is **not a security sandbox**.

## Build and test

Requires GTK >= 3.24, GLib/GIO >= 2.76, XApp, GModule, Meson and a C11 compiler.
For example, on a suitable Debian/Ubuntu development system:

```sh
sudo apt install build-essential meson ninja-build pkg-config libgtk-3-dev libxapp-dev
meson setup build -Dwerror=true
meson compile -C build
meson test -C build --suite core --print-errorlogs
# Run the real GTK dialog tests inside a Wayland session:
GDK_BACKEND=wayland dbus-run-session -- meson test -C build --suite gui --print-errorlogs
sudo meson install -C build
```

Install `umu-run` in PATH separately. CI runs normal and AddressSanitizer/UBSan
builds plus GTK dialog regressions on a headless Weston compositor. Tests use a
fake umu to check lifecycle and environment handling without downloading games
or runners. See [development notes](docs/development.md) for architecture,
coverage and the real-hardware release checklist. Disc DRM and individual game
compatibility still depend on Proton and the game.

GPL-3.0-or-later; see `LICENSE`.
