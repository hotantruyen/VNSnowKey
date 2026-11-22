#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT_DIR}/dist"
mkdir -p "${OUT_DIR}"

RUNTIME=""
if command -v podman >/dev/null 2>&1; then
    RUNTIME="podman"
elif command -v docker >/dev/null 2>&1; then
    RUNTIME="docker"
else
    echo "Error: neither 'podman' nor 'docker' is installed." >&2
    echo "On Arch Linux, install rootless podman first: sudo pacman -S --needed podman" >&2
    exit 1
fi

TARGETS=("$@")
if [ ${#TARGETS[@]} -eq 0 ]; then
    TARGETS=("debian-ubuntu")
fi

build_one() {
    local target="$1"
    local image=""
    case "${target}" in
        debian-ubuntu|deb|universal)
            # Oldest supported baseline (glibc 2.34, Qt 6.4): installs cleanly on
            # Debian 12/13 and Ubuntu 24.04/25.04+.
            image="docker.io/library/debian:bookworm"
            target="debian-ubuntu"
            ;;
        ubuntu-24.04|noble)
            image="docker.io/library/ubuntu:24.04"
            target="ubuntu-24.04"
            ;;
        ubuntu-25.04|plucky)
            image="docker.io/library/ubuntu:25.04"
            target="ubuntu-25.04"
            ;;
        debian-13|trixie)
            image="docker.io/library/debian:trixie"
            target="debian-13"
            ;;
        debian-12|bookworm)
            image="docker.io/library/debian:bookworm"
            target="debian-12"
            ;;
        *)
            echo "Unknown target '${target}'. Supported: ubuntu-24.04, ubuntu-25.04, debian-13, debian-12" >&2
            return 1
            ;;
    esac

    echo "==> [${target}] Building .deb inside ${image} using ${RUNTIME}..."
    mkdir -p "${OUT_DIR}/${target}"

    "${RUNTIME}" run --rm \
        -v "${ROOT_DIR}:/workspace:ro" \
        -v "${OUT_DIR}/${target}:/out" \
        "${image}" bash -lc '
            set -euo pipefail
            export DEBIAN_FRONTEND=noninteractive
            sed -i "s|URIs: http://deb.debian.org/debian$|URIs: http://cloudflaremirrors.com/debian|g" /etc/apt/sources.list.d/debian.sources 2>/dev/null || true
            apt-get update -qq
            apt-get install -y -qq --no-install-recommends \
                build-essential \
                ca-certificates \
                debhelper \
                devscripts \
                dpkg-dev \
                dbus \
                g++ \
                gcc \
                libevdev-dev \
                libsystemd-dev \
                libwayland-bin \
                libwayland-dev \
                libxkbcommon-dev \
                make \
                pkgconf \
                qt6-base-dev \
                qt6-svg-dev
            rm -rf /tmp/build && mkdir -p /tmp/build/src
            tar -C /workspace \
                --exclude=./build \
                --exclude=./pkg \
                --exclude=./dist \
                --exclude=./.git \
                --exclude="./*.pkg.tar.zst" \
                -cf - . | tar -C /tmp/build/src -xf -
            cd /tmp/build/src
            chmod +x debian/rules debian/vnsnowkey.postinst
            dpkg-buildpackage -us -uc -b
            cp -vf /tmp/build/vnsnowkey_*.deb /out/
            echo "==> Built package info:"
            for deb in /out/vnsnowkey_*.deb; do
                dpkg-deb --info "${deb}" | head -n 30
            done
        '
}

for t in "${TARGETS[@]}"; do
    build_one "${t}"
done

echo "==> Done! Generated .deb packages under ${OUT_DIR}:"
find "${OUT_DIR}" -name '*.deb' -ls
