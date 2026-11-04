# NAAP — New Architecture App Nap

A Linux implementation of macOS-style App Nap for KDE Plasma 6 / KWin.

When all windows of an application are minimized, moved to another
virtual desktop, or left behind on another desktop, the application is
frozen via **cgroup v2** after a 5-second grace period, and thawed as
soon as one of its windows becomes visible again. While hidden, the
app is frozen in short cycles with brief "breathing" windows, so
desktop components that talk to it (like the global menu) stay
responsive instead of blocking.

## How it works

```
KWin script (main.js)
│  window minimized / desktop changed / closed  →  D-Bus call
▼
freeze-manager (C++ / sd-bus)  ←  systemd user service
│  debounce 5 s  →  write cgroup.freeze = 1  directly on the
│                   application's OWN cgroup (freeze in place)
▼
cgroup v2 (the app's scope/service under user@<uid>.service)
```

- **KWin script** observes window state changes (minimize, desktop
  change, virtual desktop switch, open, close). It aggregates the
  visibility of *all* windows of each pid: `"show"` is sent as soon as
  any window of the app is visible, `"hide"` only when every window of
  the app is hidden, and `"close"` when the app's last window closes.
- **freeze-manager** is a small D-Bus service that debounces `"hide"`
  events with a 5-second timer. After the delay it looks up the
  application's cgroup from `/proc/<pid>/cgroup` and writes
  `cgroup.freeze = 1` **in place** — the process tree is never moved,
  so there is no cross-cgroup `EPERM` problem. `"show"` and `"close"`
  write `cgroup.freeze = 0`.
- **Breathing cycles**: a hard freeze would make the app unable to
  answer any IPC, and callers (the global menu applet, plasmashell,
  …) would block on it and stutter. Instead, frozen apps are thawed
  for 50 ms every 250 ms. Queued D-Bus calls complete within ~250 ms
  while the app still runs only ~20% of the time. The tradeoff is
  tunable via `FREEZE_PHASE` / `THAW_PHASE` in `src/main.cpp`.
- The cgroup files under `user@<uid>.service` belong to your user, so
  no root privileges and no `Delegate=yes` are needed.
- **Crash safety**: freeze-manager thaws everything on a clean stop
  (SIGTERM handler) and scans for and thaws any still-frozen cgroups
  when it starts, so a crash can never leave an app frozen forever.

## Requirements

- KDE Plasma 6 (KWin 6)
- cgroup v2 (unified hierarchy; default on modern distributions)
- systemd ≥ 244 (only for the user service)
- Build dependencies: `cmake`, `pkg-config`, `libsystemd` headers

  - Debian/Ubuntu: `sudo apt install cmake pkg-config libsystemd-dev`
  - Fedora: `sudo dnf install cmake pkgconfig systemd-devel`
  - Arch: `sudo pacman -S cmake pkgconf systemd`

## Installation

```bash
git clone https://github.com/AzenDev2026/NAAP.git
cd NAAP/naap
chmod +x install.sh uninstall.sh
./install.sh
```

The installer will:

1. Build `freeze-manager` and install it to `~/.local/bin`.
2. Install the KWin script to `~/.local/share/kwin/scripts/freeze-manager`.
3. Install, enable and start the `naap.service` systemd user unit.
4. Enable the KWin script and reload the KWin configuration.

To use a custom prefix:

```bash
PREFIX=/usr/local ./install.sh
```

## Verifying the installation

```bash
systemctl --user status naap.service
journalctl --user -u naap.service -f
```

Minimize a window, wait ~5 seconds, and you should see a log line
indicating the process was frozen. Restore the window and it is thawed
immediately.

## Uninstallation

```bash
./uninstall.sh
```

This stops and removes the systemd unit, removes the KWin script and
deletes the binary. The `build/` directory is left in place; delete it
manually if you want a clean tree.

## Safety and limitations

- **Freeze in place**: the app is frozen in the cgroup it already lives
  in. Apps launched from the start menu / KRunner normally have their
  own dedicated unit, so only that app freezes.
- **Shared cgroups**: apps started from a terminal share the terminal's
  scope, so minimizing the terminal is the correct way to nap them. A
  second app spawned from that same terminal shares the freeze too.
- **Infrastructure blocklist**: `kwin_wayland`, `plasmashell`, `dbus`,
  pipewire, input methods, portals, and similar session-critical units
  are never frozen, even if a window reports their pid.
- **Only cgroup v2**. cgroup v1 (legacy / hybrid) is not supported.
- **Wayland vs X11**: tested on Plasma 6 Wayland. X11 sessions should
  work in principle but have not been validated.
- The freeze is visible to the kernel (the app stops being scheduled),
  but systemd is not aware of it; the unit's state shown by
  `systemctl --user status` is unaffected.
- While an app is napped, IPC to it (D-Bus calls from the global menu
  or other widgets) completes on the next breathing window — at the
  default settings that is at most ~250 ms, usually much less.

## Contributing

Issues and pull requests are welcome. Please include:

- Your distribution and version
- `plasmashell --version` and `kwin_wayland --version`
- Output of `systemctl --user status naap.service`
- Relevant journal lines (`journalctl --user -u naap.service -b`)
