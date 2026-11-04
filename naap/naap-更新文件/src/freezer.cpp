#include "freezer.hpp"
#include <fstream>
#include <string>
#include <dirent.h>
#include <sys/stat.h>

namespace {

constexpr const char* CGROUP_ROOT = "/sys/fs/cgroup";

// 这些组件被冻结会导致桌面/会话直接卡死，必须跳过
bool isInfrastructure(const std::string& path) {
    static const char* blocked[] = {
        "kwin_wayland", "kwin_x11", "plasmashell",
        "dbus", "systemd", "pipewire", "wireplumber",
        "xdg-desktop-portal", "fcitx", "ibus", "dconf",
        "naap",
    };
    for (const char* s : blocked) {
        if (path.find(s) != std::string::npos) return true;
    }
    return false;
}

bool isDir(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// 递归扫描目录，解冻所有 frozen 的 cgroup
int thawRecursive(const std::string& dir, int depth) {
    if (depth > 4) return 0;
    int count = 0;
    DIR* d = ::opendir(dir.c_str());
    if (!d) return 0;
    struct dirent* ent;
    while ((ent = ::readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        std::string full = dir + "/" + ent->d_name;
        if (!isDir(full)) continue;
        if (CgroupFreezer::isFrozen(full)) {
            if (CgroupFreezer::thaw(full)) ++count;
        }
        count += thawRecursive(full, depth + 1);
    }
    ::closedir(d);
    return count;
}

} // namespace

std::string CgroupFreezer::cgroupOf(pid_t pid) {
    if (pid <= 0) return "";
    std::ifstream f("/proc/" + std::to_string(pid) + "/cgroup");
    if (!f) return "";
    std::string line;
    while (std::getline(f, line)) {
        // cgroup v2 统一层级，行格式为 "0::/path/to/cgroup"
        if (line.rfind("0::", 0) == 0) {
            std::string rel = line.substr(3);
            if (rel.empty() || rel == "/") return "";
            return std::string(CGROUP_ROOT) + rel;
        }
    }
    return ""; // cgroup v1 / 混合层级：不支持
}

std::string CgroupFreezer::selfCgroup() {
    std::ifstream f("/proc/self/cgroup");
    if (!f) return "";
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("0::", 0) == 0)
            return std::string(CGROUP_ROOT) + line.substr(3);
    }
    return "";
}

bool CgroupFreezer::freeze(const std::string& cgroup_path) {
    if (cgroup_path.empty()) return false;
    std::ofstream f(cgroup_path + "/cgroup.freeze");
    if (!f) return false;
    f << "1";
    return f.good();
}

bool CgroupFreezer::thaw(const std::string& cgroup_path) {
    if (cgroup_path.empty()) return false;
    std::ofstream f(cgroup_path + "/cgroup.freeze");
    if (!f) return false;
    f << "0";
    return f.good();
}

bool CgroupFreezer::isFrozen(const std::string& cgroup_path) {
    std::ifstream f(cgroup_path + "/cgroup.events");
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("frozen ", 0) == 0)
            return line == "frozen 1";
    }
    return false;
}

bool CgroupFreezer::isSafeToFreeze(const std::string& cgroup_path) {
    if (cgroup_path.empty()) return false;

    const std::string self = selfCgroup();
    if (!self.empty() && cgroup_path == self) return false; // 不能冻结自己
    if (isInfrastructure(cgroup_path)) return false;

    // 必须位于本用户的会话 cgroup（user@<uid>.service）之内
    const size_t pos = cgroup_path.find("/user@");
    if (pos == std::string::npos) return false;

    // 只能是 user@….service 下面 app.slice / session.slice 里的
    // 独立单元（.scope / .service / 更深层子 cgroup），
    // 不能是整个会话或整个切片
    const std::string rest = cgroup_path.substr(pos + 1);
    const bool hasUnitBelow =
        rest.find(".service/") != std::string::npos ||
        rest.find(".slice/") != std::string::npos;
    if (!hasUnitBelow) return false;

    const size_t slash = cgroup_path.rfind('/');
    const std::string leaf =
        slash == std::string::npos ? cgroup_path
                                   : cgroup_path.substr(slash + 1);
    if (leaf == "app.slice" || leaf == "session.slice" ||
        leaf.rfind("user@", 0) == 0 || leaf.rfind("user-", 0) == 0) {
        return false;
    }
    return true;
}

int CgroupFreezer::thawAllUserCgroups() {
    const std::string self = selfCgroup();
    if (self.empty()) return 0;

    // self 形如 /sys/fs/cgroup/user.slice/user-1000.slice/
    //               user@1000.service/app.slice/naap.service
    // 扫描根为 user@1000.service 这一层
    const size_t pos = self.find("/user@");
    if (pos == std::string::npos) return 0;
    const size_t end = self.find('/', pos + 1);
    const std::string root = (end == std::string::npos)
                                 ? self
                                 : self.substr(0, end);
    return thawRecursive(root, 0);
}
