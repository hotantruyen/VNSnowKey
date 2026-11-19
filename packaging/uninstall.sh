#!/usr/bin/env bash
set -euo pipefail

# VNSnowKey unified multi-distro clean uninstaller
# Supports Arch Linux (Arch/CachyOS/EndeavourOS/Manjaro), Debian/Ubuntu,
# Fedora, openSUSE, and manual `make install` installations.

DIST_ARG=""
DRY_RUN=0
AUTO_YES=0
KEEP_CONFIG=0
REMOVE_INPUT_GROUP=0

usage() {
    cat <<'EOF'
VNSnowKey Uninstaller

Usage:
  ./uninstall.sh [options]

Options:
  --distro <arch|deb|fedora|opensuse>  Force package family instead of auto-detecting
  --keep-config                        Keep ~/.config/vnsnowkey/ (settings, macros, profiles)
  --remove-input-group                 Also remove current user from the 'input' group
  --yes, -y                            Pass non-interactive confirmation flags where supported
  --dry-run                            Print detected distro and actions without modifying system
  --help, -h                           Show this help message
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --distro)
            [ $# -ge 2 ] || { echo "Error: --distro requires a value" >&2; exit 1; }
            DIST_ARG="$2"
            shift 2
            ;;
        --keep-config)
            KEEP_CONFIG=1
            shift
            ;;
        --remove-input-group)
            REMOVE_INPUT_GROUP=1
            shift
            ;;
        --yes|-y)
            AUTO_YES=1
            shift
            ;;
        --dry-run)
            DRY_RUN=1
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

run_root() {
    if [ "${DRY_RUN}" -eq 1 ]; then
        echo "[dry-run] root: $*"
        return 0
    fi
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    elif command -v sudo >/dev/null 2>&1; then
        sudo "$@"
    else
        echo "Error: root privileges or 'sudo' are required to run: $*" >&2
        exit 1
    fi
}

run_root_quiet() {
    if [ "${DRY_RUN}" -eq 1 ]; then
        echo "[dry-run] root: $*"
        return 0
    fi
    run_root "$@" >/dev/null 2>&1 || true
}

rm_root_path() {
    local target="$1"
    if [ -e "${target}" ] || [ -L "${target}" ]; then
        if [ -d "${target}" ] && [ ! -L "${target}" ]; then
            run_root rm -rf "${target}"
        else
            run_root rm -f "${target}"
        fi
    fi
}

rm_user_path() {
    local target="$1"
    if [ -e "${target}" ] || [ -L "${target}" ]; then
        if [ "${DRY_RUN}" -eq 1 ]; then
            echo "[dry-run] remove: ${target}"
            return 0
        fi
        if [ -d "${target}" ] && [ ! -L "${target}" ]; then
            rm -rf "${target}"
        else
            rm -f "${target}"
        fi
    fi
}

detect_distro() {
    if [ -n "${DIST_ARG}" ]; then
        case "${DIST_ARG}" in
            arch|archlinux|manjaro|cachyos|endeavouros) echo "arch" ;;
            deb|debian|ubuntu|debian-ubuntu|mint|pop)     echo "deb" ;;
            fedora|rhel|centos|rocky|alma)              echo "fedora" ;;
            opensuse|suse|tumbleweed|leap)              echo "opensuse" ;;
            fedora-opensuse|rpm)
                if command -v zypper >/dev/null 2>&1; then
                    echo "opensuse"
                else
                    echo "fedora"
                fi
                ;;
            *)
                echo "Error: unsupported --distro '${DIST_ARG}'" >&2
                exit 1
                ;;
        esac
        return 0
    fi

    local os_id=""
    local os_like=""
    if [ -r /etc/os-release ]; then
        # shellcheck disable=SC1091
        . /etc/os-release
        os_id="${ID:-}"
        os_like="${ID_LIKE:-}"
    fi

    case " ${os_id} ${os_like} " in
        *" arch "*|*" cachyos "*|*" endeavouros "*|*" manjaro "*)
            echo "arch"
            return 0
            ;;
        *" debian "*|*" ubuntu "*|*" linuxmint "*|*" pop "*)
            echo "deb"
            return 0
            ;;
        *" opensuse"*|*" suse "*)
            echo "opensuse"
            return 0
            ;;
        *" fedora "*|*" rhel "*|*" centos "*|*" rocky "*|*" alma "*)
            echo "fedora"
            return 0
            ;;
    esac

    if command -v pacman >/dev/null 2>&1; then
        echo "arch"
    elif command -v apt-get >/dev/null 2>&1; then
        echo "deb"
    elif command -v zypper >/dev/null 2>&1; then
        echo "opensuse"
    elif command -v dnf >/dev/null 2>&1; then
        echo "fedora"
    else
        echo "source"
    fi
}

TARGET_USER="${SUDO_USER:-${USER:-$(id -un 2>/dev/null || true)}}"
TARGET_UID=""
TARGET_HOME=""
if [ -n "${TARGET_USER}" ] && [ "${TARGET_USER}" != "root" ] && id "${TARGET_USER}" >/dev/null 2>&1; then
    TARGET_UID="$(id -u "${TARGET_USER}" 2>/dev/null || true)"
    TARGET_HOME="$(getent passwd "${TARGET_USER}" 2>/dev/null | cut -d: -f6 || true)"
    if [ -z "${TARGET_HOME}" ] && [ "$(id -u)" -ne 0 ]; then
        TARGET_HOME="${HOME:-}"
    fi
elif [ "$(id -u)" -ne 0 ]; then
    TARGET_HOME="${HOME:-}"
fi

if [ -n "${TARGET_HOME}" ]; then
    if [ "$(id -u)" -eq 0 ] && [ "${TARGET_USER}" != "root" ]; then
        TARGET_CONFIG_HOME="${TARGET_HOME}/.config"
        TARGET_DATA_HOME="${TARGET_HOME}/.local/share"
        TARGET_CACHE_HOME="${TARGET_HOME}/.cache"
    else
        TARGET_CONFIG_HOME="${XDG_CONFIG_HOME:-${TARGET_HOME}/.config}"
        TARGET_DATA_HOME="${XDG_DATA_HOME:-${TARGET_HOME}/.local/share}"
        TARGET_CACHE_HOME="${XDG_CACHE_HOME:-${TARGET_HOME}/.cache}"
    fi
else
    TARGET_CONFIG_HOME=""
    TARGET_DATA_HOME=""
    TARGET_CACHE_HOME=""
fi

run_as_target_user() {
    if [ "${DRY_RUN}" -eq 1 ]; then
        echo "[dry-run] user: $*"
        return 0
    fi
    if [ "$(id -u)" -ne 0 ]; then
        "$@"
    elif [ -n "${TARGET_USER}" ] && [ "${TARGET_USER}" != "root" ] && [ -n "${TARGET_UID}" ] && [ -S "/run/user/${TARGET_UID}/bus" ]; then
        runuser -u "${TARGET_USER}" -- env \
            HOME="${TARGET_HOME}" \
            XDG_RUNTIME_DIR="/run/user/${TARGET_UID}" \
            DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/${TARGET_UID}/bus" \
            "$@"
    else
        return 1
    fi
}

remove_gsettings_array_item() {
    local schema="$1"
    local key="$2"
    local needle="$3"
    command -v gsettings >/dev/null 2>&1 || return 0
    if [ "${DRY_RUN}" -eq 1 ]; then
        echo "[dry-run] user: gsettings remove '${needle}' from ${schema} ${key} (if present)"
        return 0
    fi

    local cur=""
    cur="$(run_as_target_user gsettings get "${schema}" "${key}" 2>/dev/null || true)"
    case "${cur}" in
        *"'${needle}'"*) ;;
        *) return 0 ;;
    esac

    local inner="${cur#[}"
    inner="${inner%]}"
    local -a kept=()
    local IFS=','
    local item=""
    for item in ${inner}; do
        item="${item#"${item%%[![:space:]]*}"}"
        item="${item%"${item##*[![:space:]]}"}"
        if [ -n "${item}" ] && [ "${item}" != "'${needle}'" ] && [ "${item}" != "\"${needle}\"" ]; then
            kept+=("${item}")
        fi
    done

    local updated="@as []"
    if [ "${#kept[@]}" -gt 0 ]; then
        local joined=""
        for item in "${kept[@]}"; do
            if [ -n "${joined}" ]; then
                joined="${joined}, ${item}"
            else
                joined="${item}"
            fi
        done
        updated="[${joined}]"
    fi
    run_as_target_user gsettings set "${schema}" "${key}" "${updated}" >/dev/null 2>&1 || true
}

run_user_quiet() {
    if [ "${DRY_RUN}" -eq 1 ]; then
        echo "[dry-run] user: $*"
        return 0
    fi
    run_as_target_user "$@" >/dev/null 2>&1 || true
}

FAMILY="$(detect_distro)"
echo "==> Detected distro family : ${FAMILY}"
[ -n "${TARGET_USER}" ] && echo "==> Target user            : ${TARGET_USER}"

echo "==> 1. Stopping VNSnowKey services and processes..."
if command -v vnsnowkey >/dev/null 2>&1; then
    run_user_quiet vnsnowkey ctl panic
fi

if command -v systemctl >/dev/null 2>&1; then
    if [ "${DRY_RUN}" -eq 1 ] || run_as_target_user systemctl --user show-environment >/dev/null 2>&1; then
        run_user_quiet systemctl --user disable --now vnsnowkey-gui.service vnsnowkeyd.service
        run_user_quiet systemctl --user stop vnsnowkey-gui.service vnsnowkeyd.service vnsnowkey-im.service
    fi
fi

if [ "${DRY_RUN}" -eq 1 ]; then
    echo "[dry-run] pkill exact processes: vnsnowkey-gui vnsnowkeyd vnsnowkey-im"
elif command -v pkill >/dev/null 2>&1; then
    if [ -n "${TARGET_USER}" ] && [ "${TARGET_USER}" != "root" ]; then
        pkill -u "${TARGET_USER}" -x vnsnowkey-gui >/dev/null 2>&1 || true
        pkill -u "${TARGET_USER}" -x vnsnowkeyd >/dev/null 2>&1 || true
        pkill -u "${TARGET_USER}" -x vnsnowkey-im >/dev/null 2>&1 || true
    else
        pkill -x vnsnowkey-gui >/dev/null 2>&1 || true
        pkill -x vnsnowkeyd >/dev/null 2>&1 || true
        pkill -x vnsnowkey-im >/dev/null 2>&1 || true
    fi
fi

echo "==> 2. Removing desktop integrations, XKB overlay, and user files..."
if command -v gnome-extensions >/dev/null 2>&1; then
    run_user_quiet gnome-extensions disable "vnsnowkey-focus@vnsnowkey.org"
fi
remove_gsettings_array_item "org.gnome.shell" "enabled-extensions" "vnsnowkey-focus@vnsnowkey.org"
remove_gsettings_array_item "org.gnome.desktop.input-sources" "xkb-options" "vnsnowkey:reload"

if command -v qdbus6 >/dev/null 2>&1; then
    run_user_quiet qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript vnsnowkey-focus
elif command -v qdbus >/dev/null 2>&1; then
    run_user_quiet qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript vnsnowkey-focus
fi

if [ -n "${TARGET_CONFIG_HOME}" ]; then
    # Reset KDE Plasma virtual keyboard selector only if it points to VNSnowKey
    if [ -f "${TARGET_CONFIG_HOME}/kwinrc" ] && grep -q "org.vnsnowkey.InputMethod.desktop" "${TARGET_CONFIG_HOME}/kwinrc" 2>/dev/null; then
        if command -v kwriteconfig6 >/dev/null 2>&1; then
            run_as_target_user kwriteconfig6 --file "${TARGET_CONFIG_HOME}/kwinrc" --group Wayland --key InputMethod --delete >/dev/null 2>&1 || true
        else
            echo "  [i] Note: '${TARGET_CONFIG_HOME}/kwinrc' still references org.vnsnowkey.InputMethod.desktop;"
            echo "      set System Settings -> Keyboard -> Virtual Keyboard back to None/Maliit if on KDE Plasma."
        fi
    fi

    # Remove XKB 8-level overlay files ONLY if generated by VNSnowKey
    XKB_DIR="${TARGET_CONFIG_HOME}/xkb"
    if [ -f "${XKB_DIR}/symbols/us" ] && grep -q "VNSNOWKEY_XKB_OVERLAY" "${XKB_DIR}/symbols/us" 2>/dev/null; then
        rm_user_path "${XKB_DIR}/symbols/us"
    fi
    if [ -f "${XKB_DIR}/types/level5" ] && grep -q "VNSNOWKEY_XKB_TYPES" "${XKB_DIR}/types/level5" 2>/dev/null; then
        rm_user_path "${XKB_DIR}/types/level5"
    fi
    if [ "${DRY_RUN}" -eq 0 ]; then
        rmdir "${XKB_DIR}/symbols" "${XKB_DIR}/types" "${XKB_DIR}" 2>/dev/null || true
    fi

    # Remove legacy XDG autostart and user-level systemd overrides/wants symlinks
    rm_user_path "${TARGET_CONFIG_HOME}/autostart/org.vnsnowkey.Autostart.desktop"
    for svc in vnsnowkeyd.service vnsnowkey-gui.service vnsnowkey-im.service; do
        rm_user_path "${TARGET_CONFIG_HOME}/systemd/user/${svc}"
        rm_user_path "${TARGET_CONFIG_HOME}/systemd/user/default.target.wants/${svc}"
        rm_user_path "${TARGET_CONFIG_HOME}/systemd/user/graphical-session.target.wants/${svc}"
    done

    if [ "${KEEP_CONFIG}" -eq 0 ]; then
        rm_user_path "${TARGET_CONFIG_HOME}/vnsnowkey"
    else
        echo "  [i] Keeping ${TARGET_CONFIG_HOME}/vnsnowkey (--keep-config)."
    fi
fi

if [ -n "${TARGET_DATA_HOME}" ] && [ -d "${TARGET_DATA_HOME}/icons/hicolor" ]; then
    if [ "${DRY_RUN}" -eq 1 ]; then
        echo "[dry-run] find ${TARGET_DATA_HOME}/icons/hicolor -iname '*vnsnowkey*' -delete"
    else
        find "${TARGET_DATA_HOME}/icons/hicolor" -iname '*vnsnowkey*' -delete 2>/dev/null || true
    fi
fi
if [ -n "${TARGET_CACHE_HOME}" ]; then
    rm_user_path "${TARGET_CACHE_HOME}/icon-cache.kcache"
fi
if [ -n "${TARGET_UID}" ]; then
    rm_user_path "/run/user/${TARGET_UID}/vnsnowkey-gui.lock"
fi
rm_user_path "/tmp/vnsnowkey-gui.lock"

echo "==> 3. Removing installed VNSnowKey distribution package (if present)..."
case "${FAMILY}" in
    arch)
        if command -v pacman >/dev/null 2>&1 && pacman -Qi vnsnowkey >/dev/null 2>&1; then
            if [ "${AUTO_YES}" -eq 1 ]; then
                run_root pacman -Rns --noconfirm vnsnowkey
            else
                run_root pacman -Rns vnsnowkey
            fi
        else
            echo "  [i] Package 'vnsnowkey' not registered in pacman."
        fi
        ;;
    deb)
        if command -v dpkg-query >/dev/null 2>&1 && dpkg-query -W -f='${Status}\n' vnsnowkey 2>/dev/null | grep -q "install ok"; then
            if [ "${AUTO_YES}" -eq 1 ]; then
                run_root env DEBIAN_FRONTEND=noninteractive apt-get purge -y vnsnowkey
            else
                run_root apt-get purge vnsnowkey
            fi
        else
            echo "  [i] Package 'vnsnowkey' not registered in dpkg/apt."
        fi
        ;;
    fedora)
        if command -v rpm >/dev/null 2>&1 && rpm -q vnsnowkey >/dev/null 2>&1; then
            if [ "${AUTO_YES}" -eq 1 ]; then
                run_root dnf remove -y vnsnowkey
            else
                run_root dnf remove vnsnowkey
            fi
        else
            echo "  [i] Package 'vnsnowkey' not registered in dnf/rpm."
        fi
        ;;
    opensuse)
        if command -v rpm >/dev/null 2>&1 && rpm -q vnsnowkey >/dev/null 2>&1; then
            if [ "${AUTO_YES}" -eq 1 ]; then
                run_root zypper --non-interactive remove -u vnsnowkey
            else
                run_root zypper remove -u vnsnowkey
            fi
        else
            echo "  [i] Package 'vnsnowkey' not registered in zypper/rpm."
        fi
        ;;
    source)
        echo "  [i] No supported distro package manager detected; proceeding with system file cleanup."
        ;;
esac

echo "==> 4. Cleaning any remaining system/source-installed files..."
for prefix in /usr /usr/local; do
    rm_root_path "${prefix}/bin/vnsnowkey"
    rm_root_path "${prefix}/bin/vnsnowkeyd"
    rm_root_path "${prefix}/bin/vnsnowkey-gui"
    rm_root_path "${prefix}/bin/vnsnowkey-im"

    rm_root_path "${prefix}/lib/udev/rules.d/70-vnsnowkey-uinput.rules"
    rm_root_path "${prefix}/lib/udev/rules.d/99-vnsnowkey-uinput.rules"
    rm_root_path "${prefix}/lib/modules-load.d/vnsnowkey-uinput.conf"

    rm_root_path "${prefix}/lib/systemd/user/vnsnowkeyd.service"
    rm_root_path "${prefix}/lib/systemd/user/vnsnowkey-gui.service"
    rm_root_path "${prefix}/lib/systemd/user/vnsnowkey-im.service"

    rm_root_path "${prefix}/share/applications/org.vnsnowkey.App.desktop"
    rm_root_path "${prefix}/share/applications/org.vnsnowkey.InputMethod.desktop"
    rm_root_path "${prefix}/share/vnsnowkey"
    rm_root_path "${prefix}/share/gnome-shell/extensions/vnsnowkey-focus@vnsnowkey.org"
    rm_root_path "${prefix}/share/licenses/vnsnowkey"

    rm_root_path "${prefix}/share/icons/hicolor/scalable/apps/vnsnowkey.svg"
    rm_root_path "${prefix}/share/icons/hicolor/256x256/apps/vnsnowkey.png"
    rm_root_path "${prefix}/share/icons/hicolor/scalable/status/vnsnowkey-vi-symbolic.svg"
    rm_root_path "${prefix}/share/icons/hicolor/scalable/status/vnsnowkey-en-symbolic.svg"
done

rm_root_path "/etc/udev/rules.d/70-vnsnowkey-uinput.rules"
rm_root_path "/etc/udev/rules.d/99-vnsnowkey-uinput.rules"
rm_root_path "/etc/modules-load.d/vnsnowkey-uinput.conf"
rm_root_path "/etc/xdg/autostart/org.vnsnowkey.Autostart.desktop"
rm_root_path "/etc/systemd/user/vnsnowkeyd.service"
rm_root_path "/etc/systemd/user/vnsnowkey-gui.service"

echo "==> 5. Refreshing systemd, udev, XKB, icon and desktop caches..."
if command -v systemctl >/dev/null 2>&1; then
    if [ "${DRY_RUN}" -eq 1 ] || run_as_target_user systemctl --user show-environment >/dev/null 2>&1; then
        run_user_quiet systemctl --user daemon-reload
        run_user_quiet systemctl --user reset-failed vnsnowkeyd.service vnsnowkey-gui.service
    fi
fi

if command -v udevadm >/dev/null 2>&1; then
    run_root_quiet udevadm control --reload-rules
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1 && [ -d /usr/share/icons/hicolor ]; then
    run_root_quiet gtk-update-icon-cache -f -t /usr/share/icons/hicolor
fi

if command -v update-desktop-database >/dev/null 2>&1 && [ -d /usr/share/applications ]; then
    run_root_quiet update-desktop-database /usr/share/applications
fi

if command -v kbuildsycoca6 >/dev/null 2>&1; then
    run_user_quiet kbuildsycoca6 --noincremental
fi

# Reload compositor XKB so removed ~/.config/xkb overlay takes effect cleanly
if command -v hyprctl >/dev/null 2>&1 && [ -n "${HYPRLAND_INSTANCE_SIGNATURE:-}" ]; then
    run_user_quiet hyprctl keyword input:kb_layout us
elif command -v swaymsg >/dev/null 2>&1 && [ -n "${SWAYSOCK:-}" ]; then
    run_user_quiet swaymsg 'input type:keyboard xkb_layout us'
elif command -v qdbus6 >/dev/null 2>&1; then
    run_user_quiet qdbus6 org.kde.KWin /KWin reconfigure
elif command -v qdbus >/dev/null 2>&1; then
    run_user_quiet qdbus org.kde.KWin /KWin reconfigure
fi

if [ "${REMOVE_INPUT_GROUP}" -eq 1 ] && [ -n "${TARGET_USER}" ] && [ "${TARGET_USER}" != "root" ] && id "${TARGET_USER}" >/dev/null 2>&1; then
    if id -nG "${TARGET_USER}" | tr ' ' '\n' | grep -qx "input"; then
        run_root gpasswd -d "${TARGET_USER}" input || true
    fi
fi

echo ""
echo "==> VNSnowKey has been cleanly uninstalled!"
if [ "${REMOVE_INPUT_GROUP}" -eq 0 ] && [ -n "${TARGET_USER}" ] && [ "${TARGET_USER}" != "root" ] && id "${TARGET_USER}" >/dev/null 2>&1; then
    if id -nG "${TARGET_USER}" | tr ' ' '\n' | grep -qx "input"; then
        echo "  [i] User '${TARGET_USER}' remains in the 'input' group (left untouched because"
        echo "      other tools/controllers may use it). To remove it as well, run:"
        echo "      ./uninstall.sh --remove-input-group   (or: sudo gpasswd -d ${TARGET_USER} input)"
    fi
fi
