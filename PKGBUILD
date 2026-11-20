# Maintainer: VNSnowKey Contributors <vnsnowkey@archlinux.local>
pkgname=vnsnowkey
pkgver=1.0.4
pkgrel=1
pkgdesc="Vietnamese Input Method (BambooEngine core, evdev + uinput) for Linux Wayland and X11 (KDE, GNOME, Hyprland, Sway)"
arch=('x86_64' 'aarch64')
url="https://github.com/hotantruyen/VNSnowKey"
license=('GPL-3.0-or-later')
depends=(
  'gcc-libs'
  'glibc'
  'libevdev'
  'libxkbcommon'
  'qt6-base'
  'qt6-svg'
  'kstatusnotifieritem'
  'systemd-libs'
  'wayland'
)
optdepends=(
  'xorg-xkbcomp: apply XKB 8-level overlay on X11 sessions'
  'xorg-setxkbmap: query and reload active X11 XKB layout'
)
makedepends=(
  'gcc'
  'make'
  'pkgconf'
)
checkdepends=(
  'dbus'
)
install=vnsnowkey.install
source=()
sha256sums=()

prepare() {
  rm -rf "$srcdir/vnsnowkey-build"
  mkdir -p "$srcdir/vnsnowkey-build/src"
  cp -rf "$startdir/Makefile" \
         "$startdir/LICENSE" \
         "$startdir/CREDITS" \
         "$startdir/tests" \
         "$startdir/assets" \
         "$startdir/packaging" \
         "$srcdir/vnsnowkey-build/"
  for mod in engine config input output focus daemon cli gui im; do
    cp -rf "$startdir/src/$mod" "$srcdir/vnsnowkey-build/src/"
  done
}

build() {
  cd "$srcdir/vnsnowkey-build"
  make -j"$(nproc)" all
}

check() {
  cd "$srcdir/vnsnowkey-build"
  ./build/test_bamboo_engine
  ./build/test_uinput_harness
  ./build/test_e2e_daemon
  QT_QPA_PLATFORM=offscreen ./build/test_app_integration
  ./build/test_gui_single_instance
  QT_QPA_PLATFORM=offscreen ./build/vnsnowkey-gui --test-icons
}

package() {
  cd "$srcdir/vnsnowkey-build"
  make DESTDIR="$pkgdir" PREFIX=/usr install
  rm -rf "$srcdir/vnsnowkey-build"
}
