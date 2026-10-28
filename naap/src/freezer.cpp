#include "freezer.hpp"
#include <fstream>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <vector>
#include <sstream>

namespace {

// 从 /proc/self/cgroup 读取本进程所在的 cgroup v2 路径
std::string selfCgroupDir() {
    std::ifstream f("/proc/self/cgroup");
    if (!f) return "";
    std::string line;
    while (std::getline(f, line)) {
        // cgroup v2: "0::/path"
        if (line.rfind("0::", 0) == 0) {
            return "/sys/fs/cgroup" + line.substr(3);
        }
    }
    return "";
}

} // namespace

CgroupFreezer::CgroupFreezer(const std::string& name) {
    std::string base = selfCgroupDir();
    if (base.empty()) {
        // 回退：旧行为（可能无权限，仅用于调试）
        cgroup_path_ = "/sys/fs/cgroup/" + name;
    } else {
        cgroup_path_ = base + "/" + name;
    }
    // 尝试创建目录；若已存在忽略 EEXIST
    if (::mkdir(cgroup_path_.c_str(), 0755) != 0 && errno != EEXIST) {
        // 记录失败，后续写入将自然失败
    }
}

bool CgroupFreezer::writeCgroupFile(const std::string& filename,
                                     const std::string& value) {
    std::ofstream f(cgroup_path_ + "/" + filename);
    if (!f) return false;
    f << value;
    return f.good();
}

bool CgroupFreezer::addPid(pid_t pid) {
    std::ofstream f(cgroup_path_ + "/cgroup.procs");
    if (!f) return false;
    f << pid;
    return f.good();
}

bool CgroupFreezer::freeze(pid_t pid) {
    if (!addPid(pid)) return false;
    return writeCgroupFile("cgroup.freeze", "1");
}

bool CgroupFreezer::thaw(pid_t pid) {
    (void)pid;
    return writeCgroupFile("cgroup.freeze", "0");
}

bool CgroupFreezer::isFrozen() const {
    std::ifstream f(cgroup_path_ + "/cgroup.events");
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.find("frozen") != std::string::npos) {
            return line.find("frozen 1") != std::string::npos;
        }
    }
    return false;
}

void CgroupFreezer::collectSessionPids(pid_t root_pid,
                                        std::vector<pid_t>& out) const {
    pid_t sid = ::getsid(root_pid);
    if (sid < 0) {
        out.push_back(root_pid);
        return;
    }
    DIR* d = ::opendir("/proc");
    if (!d) {
        out.push_back(root_pid);
        return;
    }
    struct dirent* ent;
    while ((ent = ::readdir(d)) != nullptr) {
        char* end = nullptr;
        long v = std::strtol(ent->d_name, &end, 10);
        if (end == ent->d_name || *end != '\0') continue;
        pid_t p = static_cast<pid_t>(v);
        if (p <= 0) continue;
        if (::getsid(p) == sid) {
            out.push_back(p);
        }
    }
    ::closedir(d);

    bool has_root = false;
    for (pid_t p : out) if (p == root_pid) { has_root = true; break; }
    if (!has_root) out.push_back(root_pid);
}

bool CgroupFreezer::freezeTree(pid_t root_pid) {
    std::vector<pid_t> pids;
    collectSessionPids(root_pid, pids);
    bool any = false;
    for (pid_t p : pids) {
        if (addPid(p)) any = true;
    }
    if (!any) return false;
    return writeCgroupFile("cgroup.freeze", "1");
}

bool CgroupFreezer::thawTree(pid_t root_pid) {
    (void)root_pid;
    return writeCgroupFile("cgroup.freeze", "0");
}