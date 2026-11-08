#!/usr/bin/env bash
#
# Builds the distribution packages for a Vanta release from an install tree.
#
# The tree is produced by `cmake --install build --prefix <root>/usr`, so it
# already contains the binary, the desktop entry, the hicolor icon and the docs.
# Each format is produced only when its tooling is present; the script reports
# what it built and fails when nothing could be built at all.
#
# Usage:
#   packaging/build-packages.sh --root <install-root> --version 1.0.1 --out dist

set -euo pipefail

ROOT=""
VERSION=""
OUT="dist"
MAINTAINER="ArthurSimin <311915283+ArthurSimin@users.noreply.github.com>"
PROJECT_URL="https://github.com/ArthurSimin/Vanta"
SUMMARY="Linux game launcher with Wine, Proton and OnlineFix support"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --root)        ROOT="$2"; shift 2 ;;
        --version)     VERSION="$2"; shift 2 ;;
        --out)         OUT="$2"; shift 2 ;;
        --maintainer)  MAINTAINER="$2"; shift 2 ;;
        --url)         PROJECT_URL="$2"; shift 2 ;;
        -h|--help)     sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [[ -z "$ROOT" || -z "$VERSION" ]]; then
    echo "error: --root and --version are required" >&2
    exit 2
fi
if [[ ! -x "$ROOT/usr/bin/vanta" ]]; then
    echo "error: $ROOT/usr/bin/vanta is missing; run cmake --install first" >&2
    exit 1
fi

VERSION="${VERSION#v}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
ROOT="$(cd "$ROOT" && pwd)"

# Reproducible timestamps when the caller supplies one (CI sets this).
SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(date +%s)}"
BUILD_DATE="$(date -u -d "@$SOURCE_DATE_EPOCH" '+%a %b %d %Y' 2>/dev/null \
    || date -u '+%a %b %d %Y')"
INSTALLED_BYTES="$(du -sb "$ROOT/usr" | cut -f1)"
INSTALLED_KIB="$(du -sk "$ROOT/usr" | cut -f1)"

built=()

# ---------------------------------------------------------------------------
# Debian / Ubuntu
# ---------------------------------------------------------------------------
# Qt 6 was renamed to the *t64 ABI on Ubuntu 24.04 and Debian 13, so each Qt
# dependency offers both names. One control file then installs on Debian 12
# through 13 and Ubuntu 22.04 through 24.04.
build_deb() {
    command -v dpkg-deb >/dev/null 2>&1 || { echo "skipping .deb (dpkg-deb missing)"; return 0; }

    local work="$OUT/.deb"
    rm -rf "$work"
    mkdir -p "$work/DEBIAN"
    cp -a "$ROOT/usr" "$work/"

    local size
    size="$(du -sk "$work/usr" | cut -f1)"

    # Qt 6 was renamed to the *t64 ABI on Ubuntu 24.04 and Debian 13, so each
    # dependency offers both names. One control file then installs on Debian 12
    # through 13 and Ubuntu 22.04 through 24.04.
    #
    # The binary is built against Qt 6.2 symbols on purpose (see AGENTS.md), so
    # any Qt 6 runtime from 6.2 onward satisfies it. The versioned minimum below
    # documents that instead of silently depending on the build host's Qt.
    cat > "$work/DEBIAN/control" <<EOF
Package: vanta
Version: $VERSION
Section: games
Priority: optional
Architecture: amd64
Maintainer: $MAINTAINER
Installed-Size: $size
Depends: libc6 (>= 2.34), libstdc++6, libqt6core6 (>= 6.2.4) | libqt6core6t64 (>= 6.2.4), libqt6gui6 (>= 6.2.4) | libqt6gui6t64 (>= 6.2.4), libqt6widgets6 (>= 6.2.4) | libqt6widgets6t64 (>= 6.2.4), libqt6network6 (>= 6.2.4) | libqt6network6t64 (>= 6.2.4), libqt6svg6 (>= 6.2.4) | libqt6svg6t64 (>= 6.2.4), libqt6concurrent6 (>= 6.2.4) | libqt6concurrent6t64 (>= 6.2.4)
Recommends: hicolor-icon-theme
Homepage: $PROJECT_URL
Description: $SUMMARY
 Vanta keeps a local library of the games you already own and launches
 native Linux builds and Windows builds through Wine or Proton. It
 discovers the runners other launchers installed, scans game folders
 read-only and reports what it finds.
 .
 Vanta never downloads games and never claims that a detected OnlineFix
 configuration works.
EOF

    dpkg-deb --root-owner-group -Zxz --build "$work" \
        "$OUT/vanta_${VERSION}_amd64.deb" >/dev/null
    rm -rf "$work"
    built+=("vanta_${VERSION}_amd64.deb")
}

# ---------------------------------------------------------------------------
# Fedora / openSUSE / RHEL
# ---------------------------------------------------------------------------
# No explicit Qt Requires: rpm's automatic dependency generator records the
# sonames actually linked (libQt6Widgets.so.6 and friends), which every RPM
# distribution provides under its own package name. Pinning qt6-qtbase here
# would break openSUSE, where the package is called libQt6Widgets6.
build_rpm() {
    command -v rpmbuild >/dev/null 2>&1 || { echo "skipping .rpm (rpmbuild missing)"; return 0; }

    local work="$OUT/.rpm"
    rm -rf "$work"
    mkdir -p "$work"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}

    cat > "$work/SPECS/vanta.spec" <<EOF
Name:           vanta
Version:        $VERSION
Release:        1
Summary:        $SUMMARY
License:        AGPL-3.0-only
URL:            $PROJECT_URL
BuildArch:      x86_64
AutoReqProv:    yes

%description
Vanta keeps a local library of the games you already own and launches native
Linux builds and Windows builds through Wine or Proton. It discovers the
runners other launchers installed, scans game folders read-only and reports
what it finds.

Vanta never downloads games and never claims that a detected OnlineFix
configuration works.

%prep

%build

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}
cp -a $ROOT/usr %{buildroot}/

%files
%{_bindir}/vanta
%{_datadir}/applications/vanta.desktop
%{_datadir}/icons/hicolor/scalable/apps/vanta.svg
%doc %{_datadir}/doc/vanta/README.md
%doc %{_datadir}/doc/vanta/ONLINEFIX.md
%doc %{_datadir}/doc/vanta/AGENTS.md
%license %{_datadir}/doc/vanta/LICENSE

%changelog
* $BUILD_DATE $MAINTAINER - $VERSION-1
- Vanta $VERSION
EOF

    rpmbuild -bb \
        --define "_topdir $work" \
        --define "_binary_payload w2.xzdio" \
        --define "use_source_date_epoch_as_buildtime 1" \
        --define "clamp_mtime_to_source_date_epoch 1" \
        "$work/SPECS/vanta.spec" >"$OUT/.rpm.log" 2>&1 || {
            echo "error: rpmbuild failed" >&2
            tail -30 "$OUT/.rpm.log" >&2
            return 1
        }

    local produced
    produced="$(find "$work/RPMS" -name '*.rpm' -type f | head -1)"
    if [[ -z "$produced" ]]; then
        echo "error: rpmbuild produced no package" >&2
        return 1
    fi
    cp "$produced" "$OUT/vanta-${VERSION}-1.x86_64.rpm"
    rm -rf "$work" "$OUT/.rpm.log"
    built+=("vanta-${VERSION}-1.x86_64.rpm")
}

# ---------------------------------------------------------------------------
# Arch Linux / Manjaro / CachyOS
# ---------------------------------------------------------------------------
# A pacman package is a zstd-compressed tar with .PKGINFO metadata, an optional
# .INSTALL scriptlet and a .MTREE manifest. Building it directly means the
# release job does not need pacman or makepkg on the runner.
build_pacman() {
    command -v bsdtar >/dev/null 2>&1 || { echo "skipping .pkg.tar.zst (bsdtar missing)"; return 0; }
    command -v zstd   >/dev/null 2>&1 || { echo "skipping .pkg.tar.zst (zstd missing)";   return 0; }

    local work="$OUT/.pacman"
    rm -rf "$work"
    mkdir -p "$work"
    cp -a "$ROOT/usr" "$work/"

    cat > "$work/.PKGINFO" <<EOF
# Generated by packaging/build-packages.sh for Vanta $VERSION
pkgname = vanta
pkgbase = vanta
pkgver = ${VERSION}-1
pkgdesc = $SUMMARY
url = $PROJECT_URL
builddate = $SOURCE_DATE_EPOCH
packager = $MAINTAINER
size = $INSTALLED_BYTES
arch = x86_64
license = AGPL-3.0-only
depend = qt6-base
depend = qt6-svg
EOF

    cat > "$work/.INSTALL" <<'EOF'
post_install() {
    gtk-update-icon-cache -q -t -f usr/share/icons/hicolor 2>/dev/null || true
    update-desktop-database -q usr/share/applications 2>/dev/null || true
}

post_upgrade() {
    post_install
}

post_remove() {
    gtk-update-icon-cache -q -t -f usr/share/icons/hicolor 2>/dev/null || true
    update-desktop-database -q usr/share/applications 2>/dev/null || true
}
EOF

    # Normalise ownership and mtime before the manifest is written: pacman
    # records both in .MTREE, and a package built as a normal user must still
    # look like it was installed by root.
    chmod 0755 "$work/usr/bin/vanta"
    find "$work" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
    chmod 0644 "$work/.PKGINFO" "$work/.INSTALL"

    (
        cd "$work"
        bsdtar --uid 0 --gid 0 --uname root --gname root \
            -czf .MTREE --format=mtree \
            --options='!all,use-set,type,uid,gid,mode,time,size,md5,sha256,link' \
            --exclude=.MTREE \
            .PKGINFO .INSTALL usr
        bsdtar --uid 0 --gid 0 --uname root --gname root \
            -cf - .PKGINFO .INSTALL .MTREE usr | zstd -19 -T0 -q -o \
            "$OUT/vanta-${VERSION}-1-x86_64.pkg.tar.zst"
    )
    rm -rf "$work"
    built+=("vanta-${VERSION}-1-x86_64.pkg.tar.zst")
}

build_deb
build_rpm
build_pacman

if [[ ${#built[@]} -eq 0 ]]; then
    echo "error: no package could be built; install dpkg-deb, rpmbuild or bsdtar+zstd" >&2
    exit 1
fi

echo
echo "Built ${#built[@]} package(s) in $OUT:"
for pkg in "${built[@]}"; do
    printf '  %-40s %s\n' "$pkg" "$(du -h "$OUT/$pkg" | cut -f1)"
done
echo "  installed size: ${INSTALLED_KIB} KiB"
