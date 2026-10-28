#!/usr/bin/env bash
set -euo pipefail

# NAAP (New Architecture App Nap) installer
# 支持 KDE Plasma 6 + KWin，cgroup v2

PREFIX="${PREFIX:-$HOME/.local}"
BUILD_DIR="${BUILD_DIR:-build}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

log()  { printf '\033[1;34m[naap]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[naap]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[naap]\033[0m %s\n' "$*" >&2; exit 1; }

# ---------- 前置检查 ----------
log "检查前置条件..."

if ! command -v cmake >/dev/null 2>&1; then
    die "未找到 cmake，请先安装（如 sudo apt install cmake 或 sudo pacman -S cmake）"
fi
if ! command -v pkg-config >/dev/null 2>&1; then
    die "未找到 pkg-config"
fi
if ! pkg-config --exists libsystemd; then
    die "未找到 libsystemd 开发包（如 libsystemd-dev / systemd-devel）"
fi

# cgroup v2 检查
if [ ! -f /sys/fs/cgroup/cgroup.controllers ]; then
    die "未检测到 cgroup v2。NAAP 需要 cgroup v2（unified hierarchy）。"
fi

# KWin / Plasma 检查（尽力而为）
if ! command -v kwin_wayland >/dev/null 2>&1 && ! command -v kwin_x11 >/dev/null 2>&1; then
    warn "未找到 kwin 可执行文件，仍继续安装。若你不是 KDE Plasma 用户，脚本将不会生效。"
fi

# ---------- 编译 ----------
log "编译 freeze-manager ..."
cmake -S "$SCRIPT_DIR" -B "$SCRIPT_DIR/$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build "$SCRIPT_DIR/$BUILD_DIR" -j"$(nproc)"

log "安装二进制到 $PREFIX/bin ..."
cmake --install "$SCRIPT_DIR/$BUILD_DIR"

# ---------- 安装 KWin 脚本 ----------
KWIN_SCRIPT_DIR="$HOME/.local/share/kwin/scripts/freeze-manager"
log "安装 KWin 脚本到 $KWIN_SCRIPT_DIR ..."
mkdir -p "$KWIN_SCRIPT_DIR"
cp -r "$SCRIPT_DIR/kwin-script/." "$KWIN_SCRIPT_DIR/"

# ---------- 安装 systemd user service ----------
SYSTEMD_USER_DIR="$HOME/.config/systemd/user"
mkdir -p "$SYSTEMD_USER_DIR"
log "安装 systemd user service ..."
sed "s|@CMAKE_INSTALL_PREFIX@|$PREFIX|g" \
    "$SCRIPT_DIR/systemd/naap.service.in" \
    > "$SYSTEMD_USER_DIR/naap.service"

systemctl --user daemon-reload
systemctl --user enable --now naap.service || {
    warn "启用 naap.service 失败，请检查 systemctl --user status naap.service"
}

# ---------- 启用 KWin 脚本 ----------
log "启用 KWin 脚本 ..."
if command -v kwriteconfig6 >/dev/null 2>&1; then
    kwriteconfig6 --file kwinrc --group Plugins \
        --key freeze-managerEnabled true
elif command -v kwriteconfig5 >/dev/null 2>&1; then
    kwriteconfig5 --file kwinrc --group Plugins \
        --key freeze-managerEnabled true
else
    warn "未找到 kwriteconfig，请手动在 系统设置 → KWin 脚本 中启用 'Freeze Manager Bridge'"
fi

# 重新加载 KWin 配置
if command -v qdbus6 >/dev/null 2>&1; then
    qdbus6 org.kde.KWin /KWin reconfigure 2>/dev/null || true
elif command -v qdbus >/dev/null 2>&1; then
    qdbus org.kde.KWin /KWin reconfigure 2>/dev/null || true
fi

log "安装完成。"
log "查看服务状态： systemctl --user status naap.service"
log "查看日志：     journalctl --user -u naap.service -f"
log "如需卸载：     $SCRIPT_DIR/uninstall.sh"