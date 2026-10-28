# NAAP — New Architecture App Nap  
  
A Linux implementation of macOS-style App Nap for KDE Plasma 6 / KWin.  
When a window is minimized or moved to another virtual desktop, the  
associated application is frozen via cgroup v2 after a 5-second grace  
period, and thawed as soon as the window becomes visible again.  
  
## Architecture  
KWin script (main.js)  
│ window minimize / desktop change / close → callDBus  
▼  
freeze-manager (C++ / sd-bus) ← systemd user service, Delegate=yes  
│ 5 s delay → write cgroup.freeze  
▼  
cgroup v2 (/sys/fs/cgroup/<self>/naap-freezer)  
  
text  
  
- **KWin script** observes window state changes and reports them over D-Bus.  
- **freeze-manager** is a small D-Bus service that debounces events and  
  moves the target process tree into a delegated cgroup, then writes  
  `cgroup.freeze=1`.  
- **systemd user service** provides the `Delegate=yes` cgroup subtree that  
  makes the whole scheme possible for unprivileged users.  
  
## Requirements  
  
- KDE Plasma 6 (KWin 6)  
- cgroup v2 (unified hierarchy; default on modern distributions)  
- systemd ≥ 244 (for `Delegate=yes` on service units)  
- Build dependencies: `cmake`, `pkg-config`, `libsystemd` headers  
  
  - Debian/Ubuntu: `sudo apt install cmake pkg-config libsystemd-dev`  
  - Fedora: `sudo dnf install cmake pkgconfig systemd-devel`  
  - Arch: `sudo pacman -S cmake pkgconf systemd`  
  
## Installation  
  
```bash
git clone https://github.com/<you>/naap.git
cd naap
chmod +x install.sh uninstall.sh
./install.sh
```
The installer will:

Build freeze-manager and install it to ~/.local/bin.

Install the KWin script to ~/.local/share/kwin/scripts/freeze-manager.

Install and start naap.service (a systemd user unit with
Delegate=yes, so the service can write into its own cgroup subtree).

Enable the KWin script and reload the KWin configuration.

If you prefer a custom prefix:

bash
PREFIX=/usr/local ./install.sh
Verifying the installation
bash
systemctl --user status naap.service
journalctl --user -u naap.service -f
Minimize a window, wait ~5 seconds, and you should see a log line
indicating the process was frozen. Restore the window and it will be
thawed immediately.

You can also inspect the cgroup directly:

bash
cat /sys/fs/cgroup/user.slice/user-$(id -u).slice/user@$(id -u).service/\
naap-freezer/cgroup.events
frozen 1 means the cgroup is currently frozen.

Uninstallation
bash
./uninstall.sh
This stops and removes the systemd unit, removes the KWin script, deletes
the binary, and cleans up /tmp/freeze-manager.busname. The build/
directory is left in place; delete it manually if you want a clean tree.

How it works
The KWin script attaches to every normal window and listens for
minimizedChanged, desktopsChanged, and closed signals.

On a state change it calls the D-Bus method
org.freeze.Manager.WindowStateChanged(pid, action) where action is
one of "show", "hide", or "close".

freeze-manager debounces "hide" events with a 5-second timer. If the
window is restored within that window, no freeze happens.

After the delay, freeze-manager enumerates all processes in the same
session as the window's pid (getsid), moves them into the
naap-freezer child cgroup, and writes cgroup.freeze=1.

"show" and "close" events reverse the operation by writing
cgroup.freeze=0 and clearing internal state.

Known limitations
Cross-cgroup permission issue. The target application usually lives
in a different systemd unit under app.slice. Moving its processes into
the naap.service cgroup via cgroup.procs may return EPERM under
cgroup v2, because the "no internal process" rule and delegation
boundaries forbid arbitrary cross-unit migration. When this happens the
code silently gives up for that pid instead of retrying. This is the
biggest engineering obstacle for a fully transparent App Nap and would
require a deeper slice-delegation design (e.g. placing naap.service in
the same slice as the target applications, or launching applications via
systemd-run --scope under NAAP's cgroup).

Process-tree heuristic. Processes are grouped by session id
(getsid). Daemonized children that call setsid() will be missed.

First-event race at startup. The KWin script resolves the bus
destination by reading /tmp/freeze-manager.busname, which is written
by freeze-manager after it acquires its unique bus name. If KWin
starts before naap.service, the very first window event may be lost.
Subsequent events will pick up the name correctly. Logging out and back
in once after installation is recommended.

Wayland vs X11. Tested on Plasma 6 Wayland. X11 sessions should work
in principle but have not been validated.

Only cgroup v2. cgroup v1 (legacy / hybrid) is not supported.

Contributing
Issues and pull requests are welcome. Please include:

Your distribution and version

plasmashell --version and kwin_wayland --version

Output of systemctl --user status naap.service

Relevant journal lines (journalctl --user -u naap.service -b)


text

---
