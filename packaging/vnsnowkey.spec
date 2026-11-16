%global debug_package %{nil}

Name:           vnsnowkey
Version:        1.0.0
Release:        1
Summary:        Vietnamese Input Method for Linux Wayland and X11

License:        GPL-3.0-or-later
URL:            https://github.com/hotantruyen/VNSnowKey
Source0:        vnsnowkey-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  pkgconfig
BuildRequires:  pkgconfig(libevdev)
BuildRequires:  pkgconfig(xkbcommon)
BuildRequires:  pkgconfig(Qt6Core)
BuildRequires:  pkgconfig(Qt6DBus)
BuildRequires:  pkgconfig(Qt6Gui)
BuildRequires:  pkgconfig(Qt6Widgets)
BuildRequires:  pkgconfig(Qt6Svg)
BuildRequires:  pkgconfig(libsystemd)
BuildRequires:  pkgconfig(wayland-client)
BuildRequires:  pkgconfig(wayland-scanner)
BuildRequires:  /usr/bin/dbus-run-session

Requires:       udev
Requires:       xkeyboard-config
Requires:       kmod
Requires:       libX11
Requires:       libXfixes
Recommends:     qt6-qtwayland
Recommends:     (gnome-shell-extension-appindicator if gnome-shell)

%description
VNSnowKey is a Vietnamese Input Method (BambooEngine core, evdev + uinput +
XKB 8-level overlay) for Linux supporting KDE Plasma, GNOME, Hyprland,
Sway, and X11 sessions.

%prep
%autosetup

%build
%make_build PREFIX=/usr HAVE_KF6_SNI=0 all

%check
QT_QPA_PLATFORM=offscreen make HAVE_KF6_SNI=0 test

%install
make DESTDIR=%{buildroot} PREFIX=/usr install
rm -rf %{buildroot}/usr/share/licenses/vnsnowkey

%post
rm -f /etc/xdg/autostart/org.vnsnowkey.Autostart.desktop
rm -f /etc/udev/rules.d/99-vnsnowkey-uinput.rules /usr/lib/udev/rules.d/99-vnsnowkey-uinput.rules
if [ -e /proc/modules ] && command -v modprobe >/dev/null 2>&1; then
    modprobe uinput >/dev/null 2>&1 || true
fi
udevadm control --reload-rules >/dev/null 2>&1 || true
udevadm trigger --subsystem-match=misc --action=change >/dev/null 2>&1 || true
udevadm trigger --subsystem-match=input --action=change >/dev/null 2>&1 || true
if [ -n "${SUDO_USER:-}" ] && [ "${SUDO_USER}" != "root" ] && id "${SUDO_USER}" >/dev/null 2>&1; then
    if ! id -nG "${SUDO_USER}" | tr ' ' '\n' | grep -qx "input"; then
        usermod -aG input "${SUDO_USER}" >/dev/null 2>&1 || true
    fi
    SUDO_UID="$(id -u "${SUDO_USER}" 2>/dev/null || true)"
    if [ -n "${SUDO_UID}" ] && [ -S "/run/user/${SUDO_UID}/bus" ]; then
        for _uuid in vnsnowkey-focus@vnsnowkey.org appindicatorsupport@rgcjonas.gmail.com; do
            if command -v gnome-extensions >/dev/null 2>&1; then
                runuser -u "${SUDO_USER}" -- env \
                    XDG_RUNTIME_DIR="/run/user/${SUDO_UID}" \
                    DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/${SUDO_UID}/bus" \
                    gnome-extensions enable "${_uuid}" >/dev/null 2>&1 || true
            fi
            if command -v gsettings >/dev/null 2>&1; then
                _cur_exts="$(runuser -u "${SUDO_USER}" -- env \
                    XDG_RUNTIME_DIR="/run/user/${SUDO_UID}" \
                    DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/${SUDO_UID}/bus" \
                    gsettings get org.gnome.shell enabled-extensions 2>/dev/null || true)"
                _new_exts=""
                case "${_cur_exts}" in
                    *"'${_uuid}'"*) ;;
                    "@as []"|"[]") _new_exts="['${_uuid}']" ;;
                    *"['"*"]"*) _new_exts="${_cur_exts%]}, '${_uuid}']" ;;
                esac
                if [ -n "${_new_exts}" ]; then
                    runuser -u "${SUDO_USER}" -- env \
                        XDG_RUNTIME_DIR="/run/user/${SUDO_UID}" \
                        DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/${SUDO_UID}/bus" \
                        gsettings set org.gnome.shell enabled-extensions "${_new_exts}" >/dev/null 2>&1 || true
                fi
                runuser -u "${SUDO_USER}" -- env \
                    XDG_RUNTIME_DIR="/run/user/${SUDO_UID}" \
                    DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/${SUDO_UID}/bus" \
                    gsettings set org.gnome.shell disable-user-extensions false >/dev/null 2>&1 || true
            fi
        done
    fi
fi
echo "==> VNSnowKey has been installed."
echo "==> 1. Ensure 'uinput' is loaded and add your user to 'input':"
echo "       sudo modprobe uinput && sudo usermod -aG input \$USER"
echo "==> 2. Log out and back in, then enable user services:"
echo "       systemctl --user daemon-reload && systemctl --user enable --now vnsnowkeyd.service vnsnowkey-gui.service"
echo "==> 3. Desktop-specific extras:"
echo "       - GNOME Wayland: gnome-extensions enable vnsnowkey-focus@vnsnowkey.org appindicatorsupport@rgcjonas.gmail.com"
echo "       - KDE Plasma:    System Settings -> Keyboard -> Virtual Keyboard -> VNSnowKey"

%files
%license LICENSE CREDITS
%{_bindir}/vnsnowkeyd
%{_bindir}/vnsnowkey
%{_bindir}/vnsnowkey-gui
%{_bindir}/vnsnowkey-im
/usr/lib/udev/rules.d/70-vnsnowkey-uinput.rules
/usr/lib/modules-load.d/vnsnowkey-uinput.conf
/usr/lib/systemd/user/vnsnowkeyd.service
/usr/lib/systemd/user/vnsnowkey-gui.service
%{_datadir}/applications/org.vnsnowkey.App.desktop
%{_datadir}/applications/org.vnsnowkey.InputMethod.desktop
%{_datadir}/vnsnowkey/
%{_datadir}/gnome-shell/extensions/vnsnowkey-focus@vnsnowkey.org/
%{_datadir}/icons/hicolor/scalable/apps/vnsnowkey.svg
%{_datadir}/icons/hicolor/256x256/apps/vnsnowkey.png
%{_datadir}/icons/hicolor/scalable/status/vnsnowkey-vi-symbolic.svg
%{_datadir}/icons/hicolor/scalable/status/vnsnowkey-en-symbolic.svg

%changelog
* Mon Nov 16 2026 VNSnowKey Contributors <vnsnowkey@localhost> - 1.0.0-1
- Rebranded project to VNSnowKey (GPL-3.0-or-later) with full multi-distro Wayland/X11, GNOME Shell 51/52, GameMode, and post-click buffer protection support.
