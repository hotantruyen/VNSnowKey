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
    exit 1
fi

TARGETS=("$@")
if [ ${#TARGETS[@]} -eq 0 ]; then
    TARGETS=("fedora-opensuse")
fi

build_one() {
    local target="$1"
    local image=""
    local setup_cmd=""
    case "${target}" in
        fedora-opensuse|rpm|universal)
            # Baseline RPM (Fedora 41 without %{?dist}): installs cleanly on
            # Fedora 41/42+ and openSUSE Tumbleweed.
            image="registry.fedoraproject.org/fedora:41"
            target="fedora-opensuse"
            setup_cmd='dnf install -y -q rpm-build rpmdevtools gcc gcc-c++ make pkgconf-pkg-config libevdev-devel libxkbcommon-devel qt6-qtbase-devel qt6-qtsvg-devel systemd-devel wayland-devel dbus-daemon'
            ;;
        fedora-41|fedora)
            image="registry.fedoraproject.org/fedora:41"
            target="fedora-41"
            setup_cmd='dnf install -y -q rpm-build rpmdevtools gcc gcc-c++ make pkgconf-pkg-config libevdev-devel libxkbcommon-devel qt6-qtbase-devel qt6-qtsvg-devel systemd-devel wayland-devel dbus-daemon'
            ;;
        fedora-42)
            image="registry.fedoraproject.org/fedora:42"
            target="fedora-42"
            setup_cmd='dnf install -y -q rpm-build rpmdevtools gcc gcc-c++ make pkgconf-pkg-config libevdev-devel libxkbcommon-devel qt6-qtbase-devel qt6-qtsvg-devel systemd-devel wayland-devel dbus-daemon'
            ;;
        opensuse-tumbleweed|tumbleweed)
            image="registry.opensuse.org/opensuse/tumbleweed:latest"
            target="opensuse-tumbleweed"
            setup_cmd='zypper --non-interactive install -y --no-recommends rpm-build gcc gcc-c++ make pkgconf-pkg-config libevdev-devel libxkbcommon-devel qt6-base-devel qt6-svg-devel systemd-devel wayland-devel dbus-1-daemon'
            ;;
        *)
            echo "Unknown target '${target}'. Supported: fedora-41, fedora-42, opensuse-tumbleweed" >&2
            return 1
            ;;
    esac

    local spec_ver
    spec_ver=$(awk '/^Version:/{print $2; exit}' "${ROOT_DIR}/packaging/vnsnowkey.spec")
    echo "==> [${target}] Building .rpm (${spec_ver}) inside ${image} using ${RUNTIME}..."
    mkdir -p "${OUT_DIR}/${target}"

    "${RUNTIME}" run --rm \
        -v "${ROOT_DIR}:/workspace:ro" \
        -v "${OUT_DIR}/${target}:/out" \
        "${image}" bash -lc "
            set -euo pipefail
            ${setup_cmd}
            rm -rf /tmp/rpmbuild && mkdir -p /tmp/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS} /tmp/vnsnowkey-${spec_ver}
            tar -C /workspace \
                --exclude=./build \
                --exclude=./pkg \
                --exclude=./dist \
                --exclude=./.git \
                --exclude='./*.pkg.tar.zst' \
                -cf - . | tar -C /tmp/vnsnowkey-${spec_ver} -xf -
            tar -C /tmp -czf /tmp/rpmbuild/SOURCES/vnsnowkey-${spec_ver}.tar.gz vnsnowkey-${spec_ver}
            cp /workspace/packaging/vnsnowkey.spec /tmp/rpmbuild/SPECS/vnsnowkey.spec
            rpmbuild --define '_topdir /tmp/rpmbuild' -bb /tmp/rpmbuild/SPECS/vnsnowkey.spec
            if [ '${target}' = 'fedora-opensuse' ]; then
                rpm_src=\$(find /tmp/rpmbuild/RPMS -name '*.rpm' | head -n 1)
                rpm_name=\$(rpm -qp --qf '%{NAME}-%{VERSION}-%{ARCH}.rpm' \"\${rpm_src}\")
                cp -vf \"\${rpm_src}\" \"/out/\${rpm_name}\"
            else
                find /tmp/rpmbuild/RPMS -name '*.rpm' -exec cp -vf {} /out/ \;
            fi
            for rpm in /out/*.rpm; do
                rpm -qpi \"\${rpm}\"
            done
        "
}

for t in "${TARGETS[@]}"; do
    build_one "${t}"
done

echo "==> Done! Generated .rpm packages under ${OUT_DIR}:"
find "${OUT_DIR}" -name '*.rpm' -ls
