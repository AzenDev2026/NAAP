// Unit test for CgroupFreezer helper logic
#include "freezer.hpp"
#include <iostream>
#include <unistd.h>

static int failures = 0;

static void check(bool ok, const std::string& what) {
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << what << "\n";
    if (!ok) ++failures;
}

int main() {
    // 1. selfCgroup must match /proc/self/cgroup "0::" line
    std::string self = CgroupFreezer::selfCgroup();
    std::cout << "selfCgroup() = " << self << "\n";
    check(!self.empty() && self.rfind("/sys/fs/cgroup/", 0) == 0,
          "selfCgroup looks like a cgroup v2 path");

    // 2. cgroupOf(getpid()) should equal selfCgroup()
    check(CgroupFreezer::cgroupOf(::getpid()) == self,
          "cgroupOf(getpid()) == selfCgroup()");

    // 3. cgroupOf on bad pids
    check(CgroupFreezer::cgroupOf(0).empty(), "cgroupOf(0) empty");
    check(CgroupFreezer::cgroupOf(-1).empty(), "cgroupOf(-1) empty");
    check(CgroupFreezer::cgroupOf(999999999).empty(), "cgroupOf(bogus) empty");

    // 4. isSafeToFreeze cases
    check(!CgroupFreezer::isSafeToFreeze(""), "empty path unsafe");
    check(!CgroupFreezer::isSafeToFreeze(self),
          "own cgroup is NOT safe to freeze");
    check(!CgroupFreezer::isSafeToFreeze(
              "/sys/fs/cgroup/user.slice/user-1000.slice/user@1000.service"),
          "session root unsafe");
    check(!CgroupFreezer::isSafeToFreeze(
              "/sys/fs/cgroup/user.slice/user-1000.slice/"
              "user@1000.service/app.slice"),
          "app.slice itself unsafe");
    check(!CgroupFreezer::isSafeToFreeze(
              "/sys/fs/cgroup/user.slice/user-1000.slice/"
              "user@1000.service/session.slice"),
          "session.slice itself unsafe");
    check(!CgroupFreezer::isSafeToFreeze(
              "/sys/fs/cgroup/user.slice/user-1000.slice/"
              "user@1000.service/app.slice/plasmashell.service"),
          "plasmashell blocked");
    check(!CgroupFreezer::isSafeToFreeze(
              "/sys/fs/cgroup/user.slice/user-1000.slice/"
              "user@1000.service/app.slice/kwin_wayland.service"),
          "kwin_wayland blocked");
    check(!CgroupFreezer::isSafeToFreeze(
              "/sys/fs/cgroup/user.slice/user-1000.slice/"
              "user@1000.service/app.slice/dbus.service"),
          "dbus blocked");
    check(!CgroupFreezer::isSafeToFreeze(
              "/sys/fs/cgroup/user.slice/user-1000.slice/"
              "user@1000.service/app.slice/naap.service"),
          "naap.service blocked");
    check(!CgroupFreezer::isSafeToFreeze(
              "/sys/fs/cgroup/app.slice/whatever.scope"),
          "path outside user@.service unsafe");
    check(CgroupFreezer::isSafeToFreeze(
              "/sys/fs/cgroup/user.slice/user-1000.slice/"
              "user@1000.service/app.slice/app-kate@1234.service"),
          "normal app unit safe");
    check(CgroupFreezer::isSafeToFreeze(
              "/sys/fs/cgroup/user.slice/user-1000.slice/"
              "user@1000.service/app.slice/app-org.kde.dolphin@"
              "8759c69f930f491c8ec7d6fb6f8b72a3.service"),
          "dolphin app unit safe");
    check(CgroupFreezer::isSafeToFreeze(
              "/sys/fs/cgroup/user.slice/user-1000.slice/"
              "user@1000.service/app.slice/"
              "app-flatpak-com.tencent.WeChat-3868162010.scope"),
          "flatpak scope safe");
    check(CgroupFreezer::isSafeToFreeze(
              "/sys/fs/cgroup/user.slice/user-1000.slice/"
              "user@1000.service/session.slice/konsole.scope"),
          "session.slice scope safe");
    check(!CgroupFreezer::isSafeToFreeze(
              "/sys/fs/cgroup/user.slice/user-1000.slice/"
              "user@1000.service/app.slice/xdg-desktop-portal-gtk.service"),
          "xdg-desktop-portal blocked");
    check(!CgroupFreezer::isSafeToFreeze(
              "/sys/fs/cgroup/user.slice/user-1000.slice/"
              "user@1000.service/app.slice/pipewire.service"),
          "pipewire blocked");

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED")
              << " (" << failures << " failures)\n";
    return failures == 0 ? 0 : 1;
}
