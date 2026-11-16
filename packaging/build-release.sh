#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="${ROOT_DIR}/dist"

cd "${ROOT_DIR}"

rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}/archlinux" "${DIST_DIR}/debian-ubuntu" "${DIST_DIR}/fedora-opensuse"

echo "==> [archlinux] Building Arch Linux package with makepkg..."
rm -f "${ROOT_DIR}"/vnsnowkey-*.pkg.tar.zst
makepkg -f --noconfirm
pkg_ver=$(awk -F= '/^pkgver=/{print $2}' "${ROOT_DIR}/PKGBUILD")
arch_pkg=$(find "${ROOT_DIR}" -maxdepth 1 -name 'vnsnowkey-*.pkg.tar.zst' | head -n 1)
pkg_arch=$(uname -m)
cp -vf "${arch_pkg}" "${DIST_DIR}/archlinux/vnsnowkey-${pkg_ver}-${pkg_arch}.pkg.tar.zst"
rm -rf "${ROOT_DIR}/pkg" "${ROOT_DIR}"/vnsnowkey-*.pkg.tar.zst

"${ROOT_DIR}/packaging/build-deb.sh"
"${ROOT_DIR}/packaging/build-rpm.sh"

cp -vf "${ROOT_DIR}/docs/INSTALL.md" "${DIST_DIR}/INSTALL.md"
cp -vf "${ROOT_DIR}/packaging/install.sh" "${DIST_DIR}/install.sh"
chmod +x "${DIST_DIR}/install.sh"

(
    cd "${DIST_DIR}"
    sha256sum install.sh INSTALL.md */vnsnowkey* > SHA256SUMS
)

echo "==> Done! Release artifacts:"
find "${DIST_DIR}" -maxdepth 2 -type f | sort
