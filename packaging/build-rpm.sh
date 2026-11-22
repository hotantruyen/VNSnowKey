#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT_DIR}/dist"
mkdir -p "${OUT_DIR}"

RUNTIME=""
if [ "${RPM_BUILD_CONTAINER:-0}" = "1" ] || ! command -v rpmbuild >/dev/null 2>&1; then
    if command -v podman >/dev/null 2>&1; then
        RUNTIME="podman"
    elif command -v docker >/dev/null 2>&1; then
        RUNTIME="docker"
    else
        echo "Error: neither 'rpmbuild', 'podman' nor 'docker' is installed." >&2
        exit 1
    fi
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
            setup_cmd='echo -e "fastestmirror=True\nmax_parallel_downloads=10\nzchunk=False" >> /etc/dnf/dnf.conf && dnf install -y -q rpm-build rpmdevtools gcc gcc-c++ make pkgconf-pkg-config libevdev-devel libxkbcommon-devel qt6-qtbase-devel qt6-qtsvg-devel systemd-devel wayland-devel dbus-daemon'
            ;;
        fedora-41|fedora)
            image="registry.fedoraproject.org/fedora:41"
            target="fedora-41"
            setup_cmd='echo -e "fastestmirror=True\nmax_parallel_downloads=10\nzchunk=False" >> /etc/dnf/dnf.conf && dnf install -y -q rpm-build rpmdevtools gcc gcc-c++ make pkgconf-pkg-config libevdev-devel libxkbcommon-devel qt6-qtbase-devel qt6-qtsvg-devel systemd-devel wayland-devel dbus-daemon'
            ;;
        fedora-42)
            image="registry.fedoraproject.org/fedora:42"
            target="fedora-42"
            setup_cmd='echo -e "fastestmirror=True\nmax_parallel_downloads=10\nzchunk=False" >> /etc/dnf/dnf.conf && dnf install -y -q rpm-build rpmdevtools gcc gcc-c++ make pkgconf-pkg-config libevdev-devel libxkbcommon-devel qt6-qtbase-devel qt6-qtsvg-devel systemd-devel wayland-devel dbus-daemon'
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

    if [ "${RPM_BUILD_CONTAINER:-0}" != "1" ] && command -v rpmbuild >/dev/null 2>&1; then
        echo "==> [${target}] Building .rpm (${spec_ver}) natively using host rpmbuild..."
        mkdir -p "${OUT_DIR}/${target}"
        rm -rf /tmp/rpmbuild && mkdir -p /tmp/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS} "/tmp/vnsnowkey-${spec_ver}"
        tar -C "${ROOT_DIR}" \
            --exclude=./build \
            --exclude=./pkg \
            --exclude=./dist \
            --exclude=./.git \
            --exclude='./*.pkg.tar.zst' \
            -cf - . | tar -C "/tmp/vnsnowkey-${spec_ver}" -xf -
        tar -C /tmp -czf "/tmp/rpmbuild/SOURCES/vnsnowkey-${spec_ver}.tar.gz" "vnsnowkey-${spec_ver}"
        cp "${ROOT_DIR}/packaging/vnsnowkey.spec" /tmp/rpmbuild/SPECS/vnsnowkey.spec
        rpmbuild --define '_topdir /tmp/rpmbuild' --nodeps -bb /tmp/rpmbuild/SPECS/vnsnowkey.spec
        if [ "${target}" = "fedora-opensuse" ]; then
            rpm_src=$(find /tmp/rpmbuild/RPMS -name '*.rpm' | head -n 1)
            rpm_name=$(rpm -qp --qf '%{NAME}-%{VERSION}-%{ARCH}.rpm' "${rpm_src}")
            cp -vf "${rpm_src}" "${OUT_DIR}/${target}/${rpm_name}"
        else
            find /tmp/rpmbuild/RPMS -name '*.rpm' -exec cp -vf {} "${OUT_DIR}/${target}/" \;
        fi
        for rpm in "${OUT_DIR}/${target}"/*.rpm; do
            rpm -qpi "${rpm}"
        done
        rm -rf /tmp/rpmbuild "/tmp/vnsnowkey-${spec_ver}"
        return 0
    fi

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
