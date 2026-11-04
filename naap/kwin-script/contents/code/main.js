// NAAP Freeze Manager Bridge — KWin script
// Observes window visibility and reports the AGGREGATED state per pid
// over D-Bus: "show" as soon as any window of the app is visible,
// "hide" only when ALL windows of the app are hidden,
// "close" when the app's last window closes.

const SERVICE = "org.freeze.Manager";
const PATH = "/org/freeze/Manager";
const INTERFACE = "org.freeze.Manager";

// All normal windows this script instance is tracking.
const trackedWindows = new Set();

function isWindowVisible(window) {
    // 最小化直接视为不可见
    if (window.minimized) return false;

    // 在所有桌面上的窗口始终可见
    if (window.onAllDesktops) return true;

    // 判断窗口所在桌面是否包含当前桌面
    const current = workspace.currentDesktop;
    const desktops = window.desktops;
    if (!desktops || desktops.length === 0) return true;
    for (let i = 0; i < desktops.length; ++i) {
        if (desktops[i] === current) return true;
    }
    return false;
}

function callManager(pid, action) {
    callDBus(SERVICE, PATH, INTERFACE, "WindowStateChanged", pid, action);
}

// Recompute visibility for every tracked window of this pid and send
// the aggregated state to freeze-manager.
function reportPid(pid) {
    if (!pid || pid <= 0) return;
    let visibleCount = 0;
    for (const w of trackedWindows) {
        if (w.pid === pid && isWindowVisible(w)) {
            visibleCount++;
        }
    }
    callManager(pid, visibleCount > 0 ? "show" : "hide");
}

function attachWindow(w) {
    if (!w.normalWindow) return;

    const pid = w.pid;
    if (!pid || pid <= 0) return;

    trackedWindows.add(w);

    w.minimizedChanged.connect(() => reportPid(pid));
    w.desktopsChanged.connect(() => reportPid(pid));

    // 窗口关闭：从跟踪集合移除后重新计算；若该 pid 已无任何窗口则发 "close"
    w.closed.connect(() => {
        trackedWindows.delete(w);
        let stillExists = false;
        for (const x of trackedWindows) {
            if (x.pid === pid) {
                stillExists = true;
                break;
            }
        }
        if (stillExists) {
            reportPid(pid);
        } else {
            callManager(pid, "close");
        }
    });

    // 初始上报一次，确保服务端状态与现状一致
    reportPid(pid);
}

// 监听已存在的窗口
workspace.windowList().forEach(attachWindow);

// 监听新出现的窗口
workspace.windowAdded.connect(attachWindow);

// 切换虚拟桌面时，所有留在原桌面的窗口都变为不可见
// （desktopsChanged 只在窗口被移动时触发，切换桌面不会触发它）
if (workspace.currentDesktopChanged) {
    workspace.currentDesktopChanged.connect(() => {
        const seen = new Set();
        for (const w of trackedWindows) {
            const p = w.pid;
            if (p && p > 0 && !seen.has(p)) {
                seen.add(p);
                reportPid(p);
            }
        }
    });
}
