#pragma once
#include <string>
#include <sys/types.h>

// 就地冻结：不再把进程搬进 naap 自己的 cgroup（跨 cgroup 迁移会被内核
// 以 EPERM 拒绝），而是直接写目标应用所在 cgroup 的 cgroup.freeze 文件。
// 用户会话里 user@<uid>.service 之下的 cgroup 文件都属于当前用户，
// 所以这一步不需要任何特权，也不需要 Delegate=yes。
class CgroupFreezer {
public:
    // 读取 /proc/<pid>/cgroup，返回该进程所在 cgroup v2 的完整目录
    // （例如 /sys/fs/cgroup/user.slice/user-1000.slice/user@1000.service/
    //   app.slice/app-kate@xxxx.service）。失败或非 cgroup v2 返回空串。
    static std::string cgroupOf(pid_t pid);

    // 本进程自己的 cgroup 目录
    static std::string selfCgroup();

    // 冻结 / 解冻某个 cgroup（写 cgroup.freeze）
    static bool freeze(const std::string& cgroup_path);
    static bool thaw(const std::string& cgroup_path);

    // 该 cgroup 当前是否处于 frozen 状态
    static bool isFrozen(const std::string& cgroup_path);

    // 安全校验：拒绝冻结桌面基础设施（KWin / plasmashell / dbus 等）、
    // 整个会话切片、或我们自己的 cgroup，防止把会话搞挂。
    static bool isSafeToFreeze(const std::string& cgroup_path);

    // 启动恢复：扫描本用户会话下所有 cgroup，解冻仍处于 frozen 状态的
    // 单元。防止 freeze-manager 崩溃/被强杀后应用永久卡住。
    // 返回解冻的数量。
    static int thawAllUserCgroups();
};
