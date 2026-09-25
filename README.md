# Paladin Launcher

Paladin installs and launches Windows games on a Wayland desktop. It uses GTK 3
and Linux Mint's XApp window integration. GE-Proton runs through `umu-run`;
Steam and a separate system Wine installation are not required.

## Current features

- Add an existing Windows game by selecting its `.exe`. Optionally copy its
  entire folder into Paladin's managed storage in a background task.
- Install from a mounted CD/DVD, ISO, or downloaded `setup.exe`. Paladin starts
  the installer in a new prefix, scans for likely game executables, and asks
  you to confirm the launcher. The installer and game use the same prefix.
- A per-game prefix, runner selection, game arguments, environment variables,
  native Wayland or XWayland, new WoW64, FSR, NVIDIA NVAPI, and a WineD3D fallback.
  GE-Proton supplies DXVK and VKD3D-Proton; they are shared with the runner and
  configured independently for each game's prefix. Proton handles its normal
  synchronization defaults, with per-game environment overrides when needed.
- Direct application-menu shortcuts and optional desktop shortcuts, each
  targeting `paladin-launcher --play GAME-ID`.
- View output/error logs, browse managed files, and remove a game and prefix.
  Files you linked from outside Paladin's data directory are never deleted.

## Build

Install GTK 3, XApp, GLib development headers, Meson, Ninja and pkg-config.
For example on Debian or Ubuntu:

```sh
sudo apt install build-essential meson ninja-build pkg-config libgtk-3-dev libxapp-dev
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
sudo meson install -C build
```

Install [`umu-launcher`](https://github.com/Open-Wine-Components/umu-launcher)
as `umu-run` in the user's `PATH`. Its first run downloads the shared GE-Proton
runner and Steam Linux Runtime (network access is required). Paladin leaves
GPU drivers and platform packages to the distro. A GPU without sufficiently
capable Vulkan support may need the per-game WineD3D setting. Test your distro's
Proton and 32-bit graphics dependencies on its actual target hardware.

## Files

| What | Location |
| --- | --- |
| Game metadata | `$XDG_CONFIG_HOME/paladin-launcher/games/*.ini` |
| Prefixes and copied game folders | `$XDG_DATA_HOME/paladin-launcher/games/GAME-ID/` |
| Menu shortcuts | `$XDG_DATA_HOME/applications/paladin-game-GAME-ID.desktop` |
| Launcher logs | `$XDG_CACHE_HOME/paladin-launcher/logs/` |
| Shared Proton and runtime | umu's own XDG data/cache paths |

Missing XDG environment variables use their conventional GLib defaults.
Paladin does not delete shared umu downloads when removing a game.

## Limitations and behavior

- Native Wine Wayland is the default; XWayland remains available in a Wayland
  session for games that need it. No X11 login session is needed.
- New WoW64 is enabled for new entries. Switching runner architecture after
  installing a game may require making a fresh prefix and reinstalling it.
- The game-specific prefix separates configuration; it is **not** a security
  sandbox. Run executables only from sources you trust.
- A game can save data outside its prefix or use an external game directory.
  Review saves before removal; Paladin never deletes external game files.
- Installer auto-detection is best effort. `Choose EXE` can set or correct the
  launcher. Games with disc copy protection may not run even after installation.
- If you close the window during installation, the installer continues. Reopen
  the app to choose its game executable. A failed copy can be retried from the
  incomplete entry.

Paladin is licensed under GPL-3.0-or-later; see `LICENSE`.
