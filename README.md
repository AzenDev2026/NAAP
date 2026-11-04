# NAAP — New Architecture App Nap

A Linux implementation of macOS-style App Nap for KDE Plasma 6 / KWin.
Applications whose windows are all hidden (minimized or on another
virtual desktop) are frozen via cgroup v2 after a 5-second grace
period, and thawed as soon as one of their windows becomes visible
again.

The project lives in the [`naap/`](naap/) directory — see
[naap/README.md](naap/README.md) for the full documentation, build
instructions and limitations.
