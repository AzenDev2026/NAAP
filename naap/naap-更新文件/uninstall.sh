#!/usr/bin/env bash
set -euo pipefail

PREFIX="${PREFIX:-$HOME/.local}"

log() { printf '\033[1;34m[naap]\033[0m %s\n' "$*"; }

log "停止并移除 systemd user service ..."
systemctl --user disable --now naap.service 2>/dev/null || true
rm -f "$HOME/.config/systemd/user/naap.service"
systemctl --user daemon-reload || true

log "移除 KWin 脚本 ..."
rm -rf "$HOME/.local/share/kwin/scripts/freeze-manager"

# 关闭 KWin 脚本开关
if command -v kwriteconfig6 >/dev/null 2>&1; then
    kwriteconfig6 --file kwinrc --group Plugins \
        --key freeze-managerEnabled false
elif command -v kwriteconfig5 >/dev/null 2>&1; then
    kwriteconfig5 --file kwinrc --group Plugins \
        --key freeze-managerEnabled false
fi
if command -v qdbus6 >/dev/null 2>&1; then
    qdbus6 org.kde.KWin /KWin reconfigure 2>/dev/null || true
elif command -v qdbus >/dev/null 2>&1; then
    qdbus org.kde.KWin /KWin reconfigure 2>/dev/null || true
fi

log "移除二进制 ..."
rm -f "$PREFIX/bin/freeze-manager"

log "清理临时文件 ..."
rm -f /tmp/freeze-manager.busname

log "卸载完成。构建目录 build/ 未删除，可手动移除。"