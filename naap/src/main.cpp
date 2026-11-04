#include <systemd/sd-bus.h>
#include <chrono>
#include <unordered_map>
#include <iostream>
#include <string>
#include <csignal>
#include <cerrno>
#include <cstring>
#include <sys/types.h>
#include <unistd.h>
#include "freezer.hpp"

struct WindowState {
    std::chrono::steady_clock::time_point hide_start{};
    bool pending_freeze = false;
    bool frozen = false;
    std::string cgroup_path;
};

static std::unordered_map<pid_t, WindowState> g_states;
static volatile sig_atomic_t g_stop_requested = 0;
constexpr auto FREEZE_DELAY = std::chrono::seconds(5);

static void on_signal(int /*sig*/) {
    g_stop_requested = 1;
}

static bool processAlive(pid_t pid) {
    if (pid <= 0) return false;
    errno = 0;
    if (::kill(pid, 0) == 0) return true;
    return errno == EPERM; // 进程存在但无权限
}

// 解冻所有当前记录的冻结状态；退出前调用，防止应用被永久冻住
static void thawAllTracked() {
    for (auto& entry : g_states) {
        WindowState& st = entry.second;
        if (st.frozen) {
            if (CgroupFreezer::thaw(st.cgroup_path)) {
                std::cerr << "[naap] thawed pid " << entry.first
                          << " (" << st.cgroup_path << ")\n";
            } else {
                std::cerr << "[naap] WARNING: failed to thaw pid "
                          << entry.first << " (" << st.cgroup_path << ")\n";
            }
            st.frozen = false;
        }
        st.pending_freeze = false;
    }
}

// D-Bus 方法：org.freeze.Manager.WindowStateChanged(pid, action)
// action ∈ {"show", "hide", "close"}
// KWin 脚本已按 pid 聚合：只要该应用还有任意一个可见窗口就发 "show"，
// 全部窗口都不可见才发 "hide"，最后一个窗口关闭时发 "close"。
static int on_window_state(sd_bus_message* m, void* /*userdata*/,
                           sd_bus_error* /*ret_error*/) {
    int32_t pid = 0;
    const char* action = nullptr;
    int r = sd_bus_message_read(m, "is", &pid, &action);
    if (r < 0) return r;
    if (pid <= 0) return sd_bus_reply_method_return(m, nullptr);

    std::string act = action ? action : "";
    auto now = std::chrono::steady_clock::now();
    auto& st = g_states[pid];

    if (act == "hide") {
        if (!st.pending_freeze && !st.frozen) {
            st.hide_start = now;
            st.pending_freeze = true;
        }
    } else if (act == "show") {
        st.pending_freeze = false;
        if (st.frozen) {
            if (CgroupFreezer::thaw(st.cgroup_path))
                std::cerr << "[naap] thawed pid " << pid << "\n";
            st.frozen = false;
        }
    } else if (act == "close") {
        st.pending_freeze = false;
        if (st.frozen) {
            if (CgroupFreezer::thaw(st.cgroup_path))
                std::cerr << "[naap] thawed pid " << pid << " (closed)\n";
        }
        g_states.erase(pid);
    }
    return sd_bus_reply_method_return(m, nullptr);
}

static const sd_bus_vtable manager_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("WindowStateChanged", "is", "", on_window_state,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

int main() {
    // 收到 SIGTERM / SIGINT 时解冻所有应用再退出
    struct sigaction sa {};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);

    sd_bus* bus = nullptr;
    int r = sd_bus_open_user(&bus);
    if (r < 0) {
        std::cerr << "[naap] failed to open user bus: "
                  << std::strerror(-r) << "\n";
        return 1;
    }

    sd_bus_slot* slot = nullptr;
    r = sd_bus_add_object_vtable(bus, &slot,
        "/org/freeze/Manager",
        "org.freeze.Manager",
        manager_vtable,
        nullptr);
    if (r < 0) {
        std::cerr << "[naap] failed to add vtable: "
                  << std::strerror(-r) << "\n";
        sd_bus_unref(bus);
        return 1;
    }

    r = sd_bus_request_name(bus, "org.freeze.Manager", 0);
    if (r < 0) {
        std::cerr << "[naap] failed to request bus name: "
                  << std::strerror(-r) << "\n";
        sd_bus_slot_unref(slot);
        sd_bus_unref(bus);
        return 1;
    }

    // 崩溃恢复：上一实例若在冻结状态下退出，这里统一解冻
    int thawed = CgroupFreezer::thawAllUserCgroups();
    if (thawed > 0) {
        std::cerr << "[naap] recovered " << thawed
                  << " frozen cgroup(s) from a previous run\n";
    }
    std::cerr << "[naap] freeze-manager started\n";

    // 主循环
    for (;;) {
        r = sd_bus_process(bus, nullptr);
        if (r < 0) {
            std::cerr << "[naap] sd_bus_process error: "
                      << std::strerror(-r) << "\n";
            break;
        }
        if (g_stop_requested) {
            std::cerr << "[naap] stopping, thawing everything...\n";
            break;
        }
        if (r == 0) {
            sd_bus_wait(bus, 100000); // 100ms
        }

        auto now = std::chrono::steady_clock::now();
        for (auto it = g_states.begin(); it != g_states.end(); ) {
            pid_t pid = it->first;
            WindowState& st = it->second;

            if (st.pending_freeze && !st.frozen) {
                if (now - st.hide_start >= FREEZE_DELAY) {
                    st.pending_freeze = false;
                    if (!processAlive(pid)) {
                        it = g_states.erase(it);
                        continue;
                    }
                    const std::string path = CgroupFreezer::cgroupOf(pid);
                    if (path.empty()) {
                        std::cerr << "[naap] pid " << pid
                                  << ": cannot resolve cgroup, skipped\n";
                    } else if (!CgroupFreezer::isSafeToFreeze(path)) {
                        std::cerr << "[naap] pid " << pid
                                  << ": cgroup " << path
                                  << " is shared/infrastructure, skipped\n";
                    } else if (CgroupFreezer::freeze(path)) {
                        st.frozen = true;
                        st.cgroup_path = path;
                        std::cerr << "[naap] froze pid " << pid
                                  << " (" << path << ")\n";
                    } else {
                        std::cerr << "[naap] pid " << pid
                                  << ": freeze failed (" << path << ")\n";
                    }
                }
            }
            ++it;
        }
    }

    thawAllTracked();
    sd_bus_slot_unref(slot);
    sd_bus_unref(bus);
    return 0;
}
