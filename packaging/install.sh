#!/usr/bin/env bash
set -euo pipefail

# VNSnowKey unified multi-distro installer (Arch Linux, Debian/Ubuntu, Fedora/openSUSE).

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST_ARG=""
DRY_RUN=0
AUTO_YES=0
DO_SETUP=1

usage() {
    cat <<'EOF'
VNSnowKey Installer

Usage:
  ./install.sh [options]

Options:
  --distro <arch|deb|fedora|opensuse>  Force package family instead of auto-detecting
  --no-setup                           Only install package; skip uinput/group/systemd setup
  --yes, -y                            Pass non-interactive confirmation flags where supported
  --dry-run                            Print detected distro, package file, and commands only
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
        --no-setup)
            DO_SETUP=0
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
        echo "Error: unable to detect supported distro (Arch / Debian / Ubuntu / Fedora / openSUSE)." >&2
        exit 1
    fi
}

find_package() {
    local family="$1"
    local pattern=""
    local subdir=""
    case "${family}" in
        arch)
            subdir="archlinux"
            pattern="vnsnowkey-*.pkg.tar.zst"
            ;;
        deb)
            subdir="debian-ubuntu"
            pattern="vnsnowkey_*.deb"
            ;;
        fedora|opensuse)
            subdir="fedora-opensuse"
            pattern="vnsnowkey-*.rpm"
            ;;
    esac

    local candidate=""
    for dir in "${SCRIPT_DIR}/${subdir}" "${SCRIPT_DIR}" "${SCRIPT_DIR}/../dist/${subdir}"; do
        if [ -d "${dir}" ]; then
            candidate=$(find "${dir}" -maxdepth 1 -type f -name "${pattern}" | sort -V | tail -n 1 || true)
            if [ -n "${candidate}" ]; then
                echo "${candidate}"
                return 0
            fi
        fi
    done

    echo "Error: package matching '${pattern}' not found near '${SCRIPT_DIR}'." >&2
    exit 1
}

FAMILY="$(detect_distro)"
PKG_FILE="$(find_package "${FAMILY}")"

echo "==> Detected distro family : ${FAMILY}"
echo "==> Package file           : ${PKG_FILE}"

case "${FAMILY}" in
    arch)
        if [ "${AUTO_YES}" -eq 1 ]; then
            run_root pacman -U --noconfirm "${PKG_FILE}"
        else
            run_root pacman -U "${PKG_FILE}"
        fi
        ;;
    deb)
        run_root apt-get update -qq
        if [ "${AUTO_YES}" -eq 1 ]; then
            run_root env DEBIAN_FRONTEND=noninteractive apt-get install -y --reinstall "${PKG_FILE}"
        else
            run_root apt-get install --reinstall "${PKG_FILE}"
        fi
        ;;
    fedora)
        DNF_VERB="install"
        if command -v rpm >/dev/null 2>&1 && rpm -q vnsnowkey >/dev/null 2>&1; then
            INSTALLED_EVR="$(rpm -q --qf '%{VERSION}-%{RELEASE}' vnsnowkey 2>/dev/null || true)"
            PACKAGE_EVR="$(rpm -qp --qf '%{VERSION}-%{RELEASE}' "${PKG_FILE}" 2>/dev/null || true)"
            if [ -n "${INSTALLED_EVR}" ] && [ "${INSTALLED_EVR}" = "${PACKAGE_EVR}" ]; then
                DNF_VERB="reinstall"
            else
                DNF_VERB="upgrade"
            fi
        fi
        if [ "${AUTO_YES}" -eq 1 ]; then
            run_root dnf "${DNF_VERB}" -y "${PKG_FILE}"
        else
            run_root dnf "${DNF_VERB}" "${PKG_FILE}"
        fi
        FEDORA_EXTRAS=()
        if command -v rpm >/dev/null 2>&1; then
            for _pkg in libX11 libXfixes qt6-qtwayland; do
                if ! rpm -q "${_pkg}" >/dev/null 2>&1; then
                    FEDORA_EXTRAS+=("${_pkg}")
                fi
            done
            if command -v gnome-shell >/dev/null 2>&1 && ! rpm -q gnome-shell-extension-appindicator >/dev/null 2>&1; then
                FEDORA_EXTRAS+=("gnome-shell-extension-appindicator")
            fi
        fi
        if [ "${#FEDORA_EXTRAS[@]}" -gt 0 ]; then
            if [ "${AUTO_YES}" -eq 1 ]; then
                run_root dnf install -y "${FEDORA_EXTRAS[@]}" || true
            else
                run_root dnf install "${FEDORA_EXTRAS[@]}" || true
            fi
        fi
        ;;
    opensuse)
        ZYPPER_FORCE=()
        if command -v rpm >/dev/null 2>&1 && rpm -q vnsnowkey >/dev/null 2>&1; then
            ZYPPER_FORCE=(--force)
        fi
        if [ "${AUTO_YES}" -eq 1 ]; then
            run_root zypper --non-interactive install --allow-unsigned-rpm "${ZYPPER_FORCE[@]}" -y "${PKG_FILE}"
        else
            run_root zypper install --allow-unsigned-rpm "${ZYPPER_FORCE[@]}" "${PKG_FILE}"
        fi
        ;;
esac

if [ "${DO_SETUP}" -eq 0 ]; then
    echo "==> Package installed (--no-setup specified)."
    exit 0
fi

echo "==> Configuring uinput, input group, and user services..."

if [ -e /proc/modules ] && command -v modprobe >/dev/null 2>&1; then
    run_root modprobe uinput || true
fi

if command -v udevadm >/dev/null 2>&1; then
    run_root udevadm control --reload-rules || true
    run_root udevadm trigger --subsystem-match=misc --action=change || true
    run_root udevadm trigger --subsystem-match=input --action=change || true
fi

TARGET_USER="${SUDO_USER:-${USER:-}}"
TARGET_UID=""
if [ -n "${TARGET_USER}" ] && [ "${TARGET_USER}" != "root" ] && id "${TARGET_USER}" >/dev/null 2>&1; then
    TARGET_UID="$(id -u "${TARGET_USER}" 2>/dev/null || true)"
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
            XDG_RUNTIME_DIR="/run/user/${TARGET_UID}" \
            DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/${TARGET_UID}/bus" \
            "$@"
    else
        return 1
    fi
}

enable_gnome_extension() {
    local uuid="$1"
    if command -v gnome-extensions >/dev/null 2>&1; then
        run_as_target_user gnome-extensions enable "${uuid}" >/dev/null 2>&1 || true
    fi
    if command -v gsettings >/dev/null 2>&1; then
        local cur_exts=""
        local new_exts=""
        cur_exts="$(run_as_target_user gsettings get org.gnome.shell enabled-extensions 2>/dev/null || true)"
        case "${cur_exts}" in
            *"'${uuid}'"*) ;;
            "@as []"|"[]"|"") new_exts="['${uuid}']" ;;
            *"['"*"]"*) new_exts="${cur_exts%]}, '${uuid}']" ;;
        esac
        if [ -n "${new_exts}" ]; then
            run_as_target_user gsettings set org.gnome.shell enabled-extensions "${new_exts}" >/dev/null 2>&1 || true
        fi
        run_as_target_user gsettings set org.gnome.shell disable-user-extensions false >/dev/null 2>&1 || true
    fi
}

NEED_RELOGIN=0
if [ -n "${TARGET_USER}" ] && [ "${TARGET_USER}" != "root" ] && id "${TARGET_USER}" >/dev/null 2>&1; then
    if ! id -nG "${TARGET_USER}" | tr ' ' '\n' | grep -qx "input"; then
        run_root usermod -aG input "${TARGET_USER}"
        NEED_RELOGIN=1
    fi
fi

if command -v systemctl >/dev/null 2>&1 && run_as_target_user systemctl --user show-environment >/dev/null 2>&1; then
    run_as_target_user systemctl --user daemon-reload || true
    run_as_target_user systemctl --user enable --now vnsnowkeyd.service vnsnowkey-gui.service || true
    run_as_target_user systemctl --user restart vnsnowkeyd.service vnsnowkey-gui.service || true
fi

if command -v gnome-shell >/dev/null 2>&1; then
    enable_gnome_extension "vnsnowkey-focus@vnsnowkey.org"
    if [ "${FAMILY}" = "fedora" ] || [ -d "/usr/share/gnome-shell/extensions/appindicatorsupport@rgcjonas.gmail.com" ]; then
        enable_gnome_extension "appindicatorsupport@rgcjonas.gmail.com"
    fi
    case "${XDG_CURRENT_DESKTOP:-}" in
        *GNOME*|*gnome*)
            if [ "${DRY_RUN}" -eq 0 ] && command -v gnome-extensions >/dev/null 2>&1; then
                for _ext in vnsnowkey-focus@vnsnowkey.org appindicatorsupport@rgcjonas.gmail.com; do
                    if [ -d "/usr/share/gnome-shell/extensions/${_ext}" ]; then
                        _ext_state=$(run_as_target_user gnome-extensions info "${_ext}" 2>/dev/null | grep -E "State:" || true)
                        if [ -n "${_ext_state}" ] && echo "${_ext_state}" | grep -qiE "OUT OF DATE|ERROR"; then
                            echo "  [!] Extension ${_ext} reported '${_ext_state}'; check gnome-shell version compatibility."
                        fi
                        if [ -z "${_ext_state}" ] || ! echo "${_ext_state}" | grep -qi "ACTIVE"; then
                            NEED_RELOGIN=1
                        fi
                    fi
                done
            fi
            ;;
    esac
fi

if [ -n "${TARGET_HOME:-}" ] && [ -d "${TARGET_HOME}/.local/share/icons/hicolor" ]; then
    find "${TARGET_HOME}/.local/share/icons/hicolor" -iname '*vnsnowkey*' -delete 2>/dev/null || true
    rm -f "${TARGET_HOME}/.cache/icon-cache.kcache" 2>/dev/null || true
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    run_root gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi

if command -v kbuildsycoca6 >/dev/null 2>&1; then
    run_as_target_user kbuildsycoca6 --noincremental >/dev/null 2>&1 || true
fi

echo ""
echo "==> VNSnowKey installation complete!"
if [ "${NEED_RELOGIN}" -eq 1 ]; then
    echo "  [!] Please log out and log back in once so 'input' group permissions"
    echo "      and newly installed GNOME Shell tray/focus extensions take effect."
fi
if [ -n "${TARGET_USER}" ] && [ "$(id -u)" -eq 0 ] && [ "${TARGET_USER}" != "root" ] && { [ -z "${TARGET_UID}" ] || [ ! -S "/run/user/${TARGET_UID}/bus" ]; }; then
    echo "  [i] Run as '${TARGET_USER}' to start services:"
    echo "      systemctl --user daemon-reload && systemctl --user enable --now vnsnowkeyd.service vnsnowkey-gui.service"
fi
case "${XDG_CURRENT_DESKTOP:-}" in
    *GNOME*|*gnome*)
        echo "  [i] GNOME Wayland tray & focus extensions:"
        echo "      gnome-extensions enable vnsnowkey-focus@vnsnowkey.org"
        echo "      gnome-extensions enable appindicatorsupport@rgcjonas.gmail.com"
        ;;
    *KDE*|*kde*|*Plasma*)
        echo "  [i] KDE Plasma Wayland bridge (if using virtual keyboard integration):"
        echo "      System Settings -> Keyboard -> Virtual Keyboard -> VNSnowKey"
        ;;
esac
echo "  [i] Diagnostics check: vnsnowkey ctl doctor"
