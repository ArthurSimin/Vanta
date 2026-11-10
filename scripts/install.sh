#!/bin/sh
#
# Vanta installer.
#
# Detects the distribution, checks whether Vanta is already installed and
# either installs the newest release or updates the copy that is there.
# Downloads are verified against the release SHA256SUMS before anything is
# passed to a package manager.
#
# Written in POSIX sh on purpose: the README shows `curl ... | sh`, which may
# run under dash, BusyBox sh or bash. Nothing here needs bash.
#
# Usage: sh install.sh [--yes] [--force] [--dry-run]

set -eu

REPO="ArthurSimin/Vanta"
RELEASES="https://github.com/${REPO}/releases"
API="https://api.github.com/repos/${REPO}/releases/latest"

ASSUME_YES=0
FORCE=0
DRY_RUN=0

# Defined before setup_colors so an early failure still prints cleanly.
ACCENT=""
BOLD=""
DIM=""
RESET=""
GREEN=""
YELLOW=""
RED=""
OK="ok"
MARK=">"
CROSS="x"
ARROW="->"

# ---------------------------------------------------------------------------
# Output helpers
# ---------------------------------------------------------------------------

setup_colors() {
    ACCENT=""
    BOLD=""
    DIM=""
    RESET=""
    GREEN=""
    YELLOW=""
    RED=""

    if [ -t 1 ] && [ "${NO_COLOR:-}" = "" ] && [ "${TERM:-}" != "dumb" ]; then
        if [ "${COLORTERM:-}" = "truecolor" ] || [ "${COLORTERM:-}" = "24bit" ]; then
            ACCENT=$(printf '\033[38;2;124;58;237m')
        else
            ACCENT=$(printf '\033[38;5;99m')
        fi
        BOLD=$(printf '\033[1m')
        DIM=$(printf '\033[2m')
        RESET=$(printf '\033[0m')
        GREEN=$(printf '\033[32m')
        YELLOW=$(printf '\033[33m')
        RED=$(printf '\033[31m')
    fi
}

setup_symbols() {
    OK="ok"
    MARK=">"
    CROSS="x"
    ARROW="->"

    unicode=0
    if have locale && [ "$(locale charmap 2>/dev/null || true)" = "UTF-8" ]; then
        unicode=1
    fi
    case "${LC_ALL:-${LC_CTYPE:-${LANG:-}}}" in
        *UTF-8*|*utf8*|*UTF8*|*utf-8*) unicode=1 ;;
    esac
    if [ "$unicode" -eq 1 ]; then
        OK="✓"
        MARK="▸"
        CROSS="✗"
        ARROW="→"
    fi
}

have() {
    command -v "$1" >/dev/null 2>&1
}

say() {
    printf '%s\n' "$*"
}

info() {
    printf '%s%s%s\n' "$DIM" "$*" "$RESET"
}

step() {
    printf '%s%s%s %s\n' "$ACCENT" "$MARK" "$RESET" "$*"
}

ok() {
    printf '%s%s%s %s\n' "$GREEN" "$OK" "$RESET" "$*"
}

warn() {
    printf '%s!%s %s\n' "$YELLOW" "$RESET" "$*" >&2
}

cleanup() {
    if [ -n "${WORKDIR:-}" ] && [ -d "$WORKDIR" ]; then
        rm -rf "$WORKDIR"
    fi
    WORKDIR=""
}

die() {
    printf '%s%s%s %s\n' "$RED" "$CROSS" "$RESET" "$*" >&2
    cleanup
    exit 1
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP

print_banner() {
    banner=$(cat <<'VANTA_BANNER'
$$\    $$\                     $$\
$$ |   $$ |                    $$ |
$$ |   $$ |$$$$$$\  $$$$$$$\ $$$$$$\    $$$$$$\
\$$\  $$  |\____$$\ $$  __$$\\_$$  _|   \____$$\
 \$$\$$  / $$$$$$$ |$$ |  $$ | $$ |     $$$$$$$ |
  \$$$  / $$  __$$ |$$ |  $$ | $$ |$$\ $$  __$$ |
   \$  /  \$$$$$$$ |$$ |  $$ | \$$$$  |\$$$$$$$ |
    \_/    \_______|\__|  \__|  \____/  \_______|
VANTA_BANNER
)
    printf '\n'
    printf '%s%s%s%s\n' "$ACCENT" "$BOLD" "$banner" "$RESET"
    printf '%s\n\n' "${DIM}Linux game launcher installer${RESET}"
}

usage() {
    cat <<'USAGE'
Vanta installer

Usage:
  sh install.sh [options]

Options:
  -y, --yes       Install without asking for confirmation.
  -f, --force     Reinstall the newest release even if it is already installed
                  or a newer version is present.
  -n, --dry-run   Show what would happen without downloading or installing.
  -h, --help      Show this help.

With no options the installer shows the detected system and the planned
action, then asks before changing anything.
USAGE
}

# ---------------------------------------------------------------------------
# Environment detection
# ---------------------------------------------------------------------------

detect_tools() {
    CURL=""
    WGET=""
    if have curl; then CURL=$(command -v curl); fi
    if have wget; then WGET=$(command -v wget); fi
    if [ -z "$CURL" ] && [ -z "$WGET" ]; then
        die "Neither curl nor wget is installed. Install one and run the command again."
    fi

    SHA_TOOL=""
    if have sha256sum; then
        SHA_TOOL="sha256sum"
    elif have shasum; then
        SHA_TOOL="shasum"
    elif have openssl; then
        SHA_TOOL="openssl"
    else
        die "No SHA-256 tool was found. Install coreutils (sha256sum) and run the command again."
    fi
}

detect_os() {
    OS_ID=""
    OS_LIKE=""
    OS_NAME="Linux"

    if [ -r /etc/os-release ]; then
        # shellcheck disable=SC1091
        . /etc/os-release
        OS_ID=${ID:-}
        OS_LIKE=${ID_LIKE:-}
        OS_NAME=${PRETTY_NAME:-${NAME:-Linux}}
    fi
    OS_ID=$(printf '%s' "$OS_ID" | tr '[:upper:]' '[:lower:]')
    OS_LIKE=$(printf '%s' "$OS_LIKE" | tr '[:upper:]' '[:lower:]')

    OS_FAMILY="unknown"
    case " $OS_ID $OS_LIKE " in
        *" arch "*|*" archlinux "*) OS_FAMILY="arch" ;;
        *" debian "*|*" ubuntu "*) OS_FAMILY="debian" ;;
        *" fedora "*|*" rhel "*|*" centos "*|*" suse "*|*" opensuse "*) OS_FAMILY="rpm" ;;
    esac
}

detect_privilege_tool() {
    PRIV_TOOL="none"
    USE_PKEXEC=0

    if [ "$(id -u)" -eq 0 ]; then
        PRIV_TOOL="root"
        return
    fi

    gui_terminal=0
    # ps does not need to open the terminal, so it works when stdin is the
    # `curl | sh` pipe and stays quiet when there is no controlling tty.
    tty_path=$(ps -o tty= -p "$$" 2>/dev/null | tr -d '[:space:]' || true)
    case "$tty_path" in
        pts/*|"?"|"")
            if [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ] \
                || [ -n "${KITTY_WINDOW_ID:-}" ] || [ -n "${ALACRITTY_WINDOW_ID:-}" ] \
                || [ -n "${KONSOLE_VERSION:-}" ] || [ -n "${VTE_VERSION:-}" ] \
                || [ -n "${WT_SESSION:-}" ] || [ -n "${WEZTERM_PANE:-}" ] \
                || [ -n "${TERM_PROGRAM:-}" ] || [ -n "${GNOME_TERMINAL_SCREEN:-}" ]; then
                gui_terminal=1
            fi
            ;;
        *)
            gui_terminal=0
            ;;
    esac
    # A remote shell is text-first even when X forwarding is configured.
    if [ -n "${SSH_CONNECTION:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
        gui_terminal=0
    fi

    if [ "$gui_terminal" -eq 1 ] && have pkexec; then
        PRIV_TOOL="pkexec"
        USE_PKEXEC=1
    elif have sudo; then
        PRIV_TOOL="sudo"
    elif have doas; then
        PRIV_TOOL="doas"
    elif have pkexec; then
        PRIV_TOOL="pkexec"
        USE_PKEXEC=1
    fi
}

run_root() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    elif [ "$USE_PKEXEC" -eq 1 ]; then
        pkexec "$@"
    elif have sudo; then
        sudo "$@"
    elif have doas; then
        doas "$@"
    else
        die "No way to elevate privileges was found. Re-run this installer as root."
    fi
}

# ---------------------------------------------------------------------------
# Release lookup and version handling
# ---------------------------------------------------------------------------

fetch_latest() {
    LATEST=""

    if [ -n "$CURL" ]; then
        json=$("$CURL" -fsSL --retry 2 --connect-timeout 15 \
            -H 'Accept: application/vnd.github+json' \
            -H 'User-Agent: Vanta-Installer' "$API" 2>/dev/null || true)
        if [ -n "$json" ]; then
            LATEST=$(printf '%s\n' "$json" \
                | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' \
                | head -n1)
        fi
        if [ -z "$LATEST" ]; then
            effective=$("$CURL" -fsSIL -o /dev/null -w '%{url_effective}' \
                "$RELEASES/latest" 2>/dev/null || true)
            case "$effective" in
                */tag/*) LATEST=${effective##*/} ;;
            esac
        fi
    else
        json=$("$WGET" -qO- --timeout=15 \
            --header='Accept: application/vnd.github+json' \
            --header='User-Agent: Vanta-Installer' "$API" 2>/dev/null || true)
        if [ -n "$json" ]; then
            LATEST=$(printf '%s\n' "$json" \
                | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' \
                | head -n1)
        fi
        if [ -z "$LATEST" ]; then
            effective=$("$WGET" --spider -S "$RELEASES/latest" 2>&1 \
                | sed -n 's/.*[Ll]ocation: //p' | tr -d '\r' | tail -n1)
            case "$effective" in
                */tag/*) LATEST=${effective##*/} ;;
            esac
        fi
    fi

    LATEST=${LATEST#v}
    LATEST=$(printf '%s' "$LATEST" | tr -d '\r')
    if ! printf '%s\n' "$LATEST" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'; then
        die "Could not find the newest Vanta release. Check your connection, or open $RELEASES/latest."
    fi
}

version_compare() {
    awk -v a="$1" -v b="$2" '
        function norm(v) {
            sub(/^[vV]/, "", v)
            sub(/[-+].*/, "", v)
            return v
        }
        BEGIN {
            na = split(norm(a), A, ".")
            nb = split(norm(b), B, ".")
            n = (na > nb ? na : nb)
            for (i = 1; i <= n; i++) {
                x = (i <= na && A[i] ~ /^[0-9]+$/) ? A[i] + 0 : 0
                y = (i <= nb && B[i] ~ /^[0-9]+$/) ? B[i] + 0 : 0
                if (x > y) { print 1; exit }
                if (x < y) { print -1; exit }
            }
            print 0
        }'
}

# ---------------------------------------------------------------------------
# Installed copy detection
# ---------------------------------------------------------------------------

detect_installed() {
    PACKAGE_KIND="none"
    PACKAGE_VERSION=""
    INSTALLED_KIND="none"
    INSTALLED=""
    INSTALLED_PATH=""

    if have pacman && pacman -Q vanta >/dev/null 2>&1; then
        PACKAGE_KIND="pacman"
        PACKAGE_VERSION=$(pacman -Q vanta 2>/dev/null | awk '{print $2}' | sed 's/-[0-9][0-9]*$//')
    elif have dpkg-query && dpkg-query -W -f='${Status}' vanta 2>/dev/null | grep -q 'install ok installed'; then
        PACKAGE_KIND="deb"
        PACKAGE_VERSION=$(dpkg-query -W -f='${Version}' vanta 2>/dev/null | sed 's/-[0-9][0-9]*$//')
    elif have rpm && rpm -q vanta >/dev/null 2>&1; then
        PACKAGE_KIND="rpm"
        PACKAGE_VERSION=$(rpm -q --qf '%{VERSION}' vanta 2>/dev/null)
    fi

    INSTALLED_PATH=$(command -v vanta 2>/dev/null || true)
    if [ -z "$INSTALLED_PATH" ]; then
        for candidate in /usr/local/bin/vanta /usr/bin/vanta; do
            if [ -x "$candidate" ]; then
                INSTALLED_PATH="$candidate"
                break
            fi
        done
    fi

    if [ -n "$INSTALLED_PATH" ]; then
        INSTALLED=$("$INSTALLED_PATH" --version 2>/dev/null \
            | sed -n 's/^Vanta[[:space:]]\{1,\}\([0-9][0-9.]*\).*$/\1/p' \
            | head -n1 || true)
        case "$INSTALLED_PATH" in
            /usr/local/*)
                INSTALLED_KIND="tarball"
                ;;
            *)
                if [ "$PACKAGE_KIND" != "none" ]; then
                    INSTALLED_KIND="$PACKAGE_KIND"
                else
                    INSTALLED_KIND="tarball"
                fi
                ;;
        esac
        if [ -z "$INSTALLED" ]; then
            INSTALLED="$PACKAGE_VERSION"
        fi
    elif [ "$PACKAGE_KIND" != "none" ]; then
        INSTALLED_KIND="$PACKAGE_KIND"
        INSTALLED="$PACKAGE_VERSION"
    fi

    if [ -z "$INSTALLED" ] && [ "$INSTALLED_KIND" != "none" ]; then
        INSTALLED="unknown"
    fi

    if [ "$INSTALLED_KIND" = "tarball" ] && [ "$PACKAGE_KIND" != "none" ]; then
        warn "A package and a manual copy are both present; $INSTALLED_PATH takes precedence."
    fi
}

detect_foreign_package() {
    FOREIGN_PACKAGE=""
    [ -n "$INSTALLED_PATH" ] || return 0
    case "$INSTALLED_PATH" in
        /usr/local/*) return 0 ;;
    esac

    if have pacman && pacman -Qo "$INSTALLED_PATH" >/dev/null 2>&1; then
        FOREIGN_PACKAGE=$(pacman -Qoq "$INSTALLED_PATH" 2>/dev/null | head -n1)
    elif have dpkg && dpkg -S "$INSTALLED_PATH" >/dev/null 2>&1; then
        FOREIGN_PACKAGE=$(dpkg -S "$INSTALLED_PATH" 2>/dev/null | sed 's/:.*//' | head -n1)
    elif have rpm && rpm -qf "$INSTALLED_PATH" >/dev/null 2>&1; then
        FOREIGN_PACKAGE=$(rpm -qf --qf '%{NAME}\n' "$INSTALLED_PATH" 2>/dev/null | head -n1)
    fi

    if [ -n "$FOREIGN_PACKAGE" ] && [ "$FOREIGN_PACKAGE" != "vanta" ]; then
        warn "Vanta is installed by the package \"$FOREIGN_PACKAGE\", which this installer does not manage."
        die "Update it with your package manager, or remove it before switching to the official release."
    fi

    if [ -z "$FOREIGN_PACKAGE" ] && [ "$INSTALLED_KIND" = "tarball" ] \
        && [ "${INSTALLED_PATH#/usr/local/}" = "$INSTALLED_PATH" ]; then
        warn "A manual Vanta binary at $INSTALLED_PATH is not managed by a package."
        info "The update goes to /usr/local, which normally takes precedence in PATH."
    fi
}

# ---------------------------------------------------------------------------
# Package format selection
# ---------------------------------------------------------------------------

choose_rpm_tool() {
    RPM_TOOL=""
    if [ -e /run/ostree-booted ] && have rpm-ostree; then
        RPM_TOOL="rpm-ostree"
    else
        case " $OS_ID $OS_LIKE " in
            *" suse "*|*" opensuse "*)
                if have zypper; then RPM_TOOL="zypper"; fi
                ;;
        esac
        if [ -z "$RPM_TOOL" ]; then
            if have dnf; then
                RPM_TOOL="dnf"
            elif have yum; then
                RPM_TOOL="yum"
            elif have zypper; then
                RPM_TOOL="zypper"
            elif have rpm-ostree; then
                RPM_TOOL="rpm-ostree"
            elif have rpm; then
                RPM_TOOL="rpm"
            fi
        fi
    fi
    if [ -z "$RPM_TOOL" ]; then
        die "No RPM package manager was found. Install dnf, zypper or rpm."
    fi
}

choose_format() {
    FORMAT=""
    case "$OS_FAMILY" in
        arch)
            if have pacman; then FORMAT="pacman"; fi
            ;;
        debian)
            if have apt-get; then FORMAT="deb"; fi
            ;;
        rpm)
            if have dnf || have yum || have zypper || have rpm-ostree || have rpm; then
                FORMAT="rpm"
            fi
            ;;
    esac

    if [ -z "$FORMAT" ]; then
        if have pacman; then
            FORMAT="pacman"
        elif have apt-get; then
            FORMAT="deb"
        elif have dnf || have yum || have zypper || have rpm-ostree || have rpm; then
            FORMAT="rpm"
        else
            FORMAT="tarball"
        fi
    fi

    if [ "$FORMAT" = "rpm" ]; then
        choose_rpm_tool
    fi
}

select_format() {
    if [ "$INSTALLED_KIND" != "none" ]; then
        FORMAT="$INSTALLED_KIND"
    else
        choose_format
    fi

    case "$FORMAT" in
        pacman)
            have pacman || die "pacman is not available; cannot install the package."
            ;;
        deb)
            if ! have apt-get && ! have dpkg; then
                die "Neither apt-get nor dpkg is available; cannot install the package."
            fi
            ;;
        rpm)
            choose_rpm_tool
            ;;
        tarball)
            have tar || die "tar is not available; cannot unpack the portable build."
            ;;
        *)
            die "Unsupported package format: $FORMAT"
            ;;
    esac

    case "$FORMAT" in
        pacman) FORMAT_LABEL="pacman package (.pkg.tar.zst)" ;;
        deb) FORMAT_LABEL="Debian package (.deb)" ;;
        rpm) FORMAT_LABEL="RPM package (.rpm) via $RPM_TOOL" ;;
        tarball) FORMAT_LABEL="portable tarball (/usr/local)" ;;
    esac
}

# ---------------------------------------------------------------------------
# Download and verification
# ---------------------------------------------------------------------------

download() {
    url=$1
    out=$2
    quiet=${3:-0}

    if [ -n "$CURL" ]; then
        if [ "$quiet" -eq 1 ] || [ ! -t 2 ]; then
            "$CURL" -fsSL --retry 3 --retry-delay 2 --connect-timeout 15 -o "$out" "$url"
        else
            "$CURL" -fL --retry 3 --retry-delay 2 --connect-timeout 15 --progress-bar -o "$out" "$url"
        fi
    else
        "$WGET" -q -O "$out" "$url"
    fi
}

sha256_of() {
    case "$SHA_TOOL" in
        sha256sum) sha256sum "$1" | awk '{print $1}' ;;
        shasum) shasum -a 256 "$1" | awk '{print $1}' ;;
        openssl) openssl dgst -sha256 "$1" | awk '{print $NF}' ;;
    esac
}

human_size() {
    awk -v b="$1" 'BEGIN {
        split("B KiB MiB GiB", u, " ")
        i = 1
        while (b >= 1024 && i < 4) { b /= 1024; i++ }
        if (i == 1) printf "%.0f %s", b, u[i]
        else printf "%.1f %s", b, u[i]
    }'
}

# dnf and yum install a local RPM but do not reinstall or downgrade the same
# package, so those two cases go through rpm once the dependencies are already
# present. --oldpackage is what allows a deliberate downgrade.
install_rpm_file() {
    if [ "$VERSION_CMP" = "-1" ]; then
        run_root rpm -Uvh --replacepkgs --oldpackage "$1"
    else
        run_root rpm -Uvh --replacepkgs "$1"
    fi
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

while [ $# -gt 0 ]; do
    case "$1" in
        -y|--yes) ASSUME_YES=1 ;;
        -f|--force) FORCE=1 ;;
        -n|--dry-run) DRY_RUN=1 ;;
        -h|--help) setup_colors; setup_symbols; print_banner; usage; exit 0 ;;
        *) setup_colors; setup_symbols; print_banner; die "Unknown option: $1 (try --help)" ;;
    esac
    shift
done

setup_colors
setup_symbols
print_banner

[ "$(uname -s)" = "Linux" ] || die "Vanta runs on Linux only."
ARCH=$(uname -m)
case "$ARCH" in
    x86_64|amd64) ;;
    *) die "Vanta ships x86_64 packages only; this machine is $ARCH." ;;
esac

detect_tools
detect_os
detect_privilege_tool
fetch_latest
detect_installed
detect_foreign_package
select_format

VERSION_CMP=0
ACTION="install"
if [ "$INSTALLED_KIND" != "none" ]; then
    VERSION_CMP=$(version_compare "$LATEST" "$INSTALLED")
    if [ "$VERSION_CMP" = "1" ]; then
        ACTION="update"
    elif [ "$VERSION_CMP" = "0" ]; then
        ACTION="none"
    else
        ACTION="newer"
    fi
    if [ "$FORCE" -eq 1 ]; then
        ACTION="reinstall"
    fi
fi

case "$ACTION" in
    install) ACTION_LABEL="Install Vanta $LATEST" ;;
    update) ACTION_LABEL="Update Vanta $INSTALLED to $LATEST" ;;
    reinstall)
        if [ "$VERSION_CMP" = "-1" ]; then
            ACTION_LABEL="Replace Vanta $INSTALLED with $LATEST"
        else
            ACTION_LABEL="Reinstall Vanta $LATEST"
        fi
        ;;
    none) ACTION_LABEL="Already up to date" ;;
    newer) ACTION_LABEL="Installed version is newer than the newest release" ;;
esac

case "$PRIV_TOOL" in
    pkexec) PRIV_LABEL="pkexec (graphical prompt)" ;;
    sudo) PRIV_LABEL="sudo" ;;
    doas) PRIV_LABEL="doas" ;;
    root) PRIV_LABEL="root (no prompt)" ;;
    *) PRIV_LABEL="not available" ;;
esac
if [ "$ACTION" = "none" ] || [ "$ACTION" = "newer" ]; then
    PRIV_LABEL="not needed"
fi

case "$FORMAT" in
    pacman) ASSET="vanta-${LATEST}-1-x86_64.pkg.tar.zst" ;;
    deb) ASSET="vanta_${LATEST}_amd64.deb" ;;
    rpm) ASSET="vanta-${LATEST}-1.x86_64.rpm" ;;
    tarball) ASSET="vanta-${LATEST}-linux-x86_64.tar.gz" ;;
esac

printf '%s%-11s%s %s (%s)\n' "$DIM" "System" "$RESET" "$OS_NAME" "$ARCH"
printf '%s%-11s%s %s\n' "$DIM" "Package" "$RESET" "$FORMAT_LABEL"
if [ "$INSTALLED_KIND" = "none" ]; then
    printf '%s%-11s%s %s\n' "$DIM" "Installed" "$RESET" "not installed"
elif [ "$INSTALLED" = "unknown" ]; then
    printf '%s%-11s%s %s\n' "$DIM" "Installed" "$RESET" "Vanta (version unknown)"
else
    printf '%s%-11s%s %s\n' "$DIM" "Installed" "$RESET" "Vanta $INSTALLED"
fi
printf '%s%-11s%s %s\n' "$DIM" "Latest" "$RESET" "Vanta $LATEST"
printf '%s%-11s%s %s\n' "$DIM" "Action" "$RESET" "$ACTION_LABEL"
printf '%s%-11s%s %s\n\n' "$DIM" "Elevation" "$RESET" "$PRIV_LABEL"

if [ "$ACTION" = "none" ]; then
    ok "Vanta $LATEST is already installed and up to date."
    exit 0
fi
if [ "$ACTION" = "newer" ]; then
    ok "Vanta $INSTALLED is newer than the newest release, $LATEST."
    info "Nothing to do. Use --force to replace it with the release build."
    exit 0
fi

if [ "$FORMAT" = "tarball" ]; then
    warn "The portable tarball needs the Qt 6 runtime; install it with your distribution's package manager."
    if have ldconfig && ! ldconfig -p 2>/dev/null | grep -q 'libQt6Widgets\.so\.6'; then
        warn "Qt 6 Widgets was not found in the linker cache. Install your distribution's Qt 6 runtime before starting Vanta."
    fi
fi

if [ "$DRY_RUN" -eq 1 ]; then
    info "Dry run: would download $ASSET and install it using the $FORMAT_LABEL."
    info "Nothing was downloaded or installed."
    exit 0
fi

if [ "$(id -u)" -ne 0 ] && [ "$PRIV_TOOL" = "none" ]; then
    die "No sudo, doas or pkexec was found. Re-run this installer as root."
fi

if [ "$ASSUME_YES" -eq 0 ]; then
    # -r/-w on /dev/tty succeed even without a controlling terminal; actually
    # opening it is the only reliable test.
    if ! ( exec 3<>/dev/tty ) 2>/dev/null; then
        die "No terminal is available for confirmation. Re-run with --yes to install without a prompt."
    fi

    case "$ACTION" in
        update) say "Vanta $INSTALLED will be replaced by Vanta $LATEST." ;;
        reinstall)
            if [ "$VERSION_CMP" = "-1" ]; then
                say "Vanta $INSTALLED will be replaced by Vanta $LATEST."
            else
                say "Vanta $LATEST will be installed again."
            fi
            ;;
        *) say "Vanta $LATEST will be installed." ;;
    esac
    if [ "$PRIV_TOOL" = "pkexec" ]; then
        say "A graphical authentication prompt will appear."
    elif [ "$PRIV_TOOL" = "sudo" ] || [ "$PRIV_TOOL" = "doas" ]; then
        say "Your password may be requested in this terminal."
    fi

    printf '%s' "Proceed? [Y/n] " > /dev/tty
    if ! IFS= read -r answer < /dev/tty; then
        printf '\n' > /dev/tty
        die "No answer was read. Re-run with --yes to install without a prompt."
    fi
    case "$answer" in
        ""|y|Y|yes|YES|Yes) ;;
        *)
            say "Cancelled. Nothing was changed."
            exit 0
            ;;
    esac
else
    info "Proceeding without confirmation (--yes)."
fi

WORKDIR=$(mktemp -d) || die "Could not create a temporary directory."

ASSET_URL="${RELEASES}/download/v${LATEST}/${ASSET}"
SUMS_URL="${RELEASES}/download/v${LATEST}/SHA256SUMS"

step "Downloading $ASSET"
download "$ASSET_URL" "$WORKDIR/$ASSET" || die "Download failed. Open $RELEASES/tag/v$LATEST to download the package by hand."
[ -s "$WORKDIR/$ASSET" ] || die "The downloaded file is empty."
bytes=$(wc -c < "$WORKDIR/$ASSET" | tr -d '[:space:]')
info "  $(human_size "$bytes")"

step "Verifying SHA-256"
download "$SUMS_URL" "$WORKDIR/SHA256SUMS" 1 || die "Could not download SHA256SUMS from the release."
EXPECTED=$(awk -v f="$ASSET" '
    {
        name = $2
        sub(/^\*/, "", name)
        sub(/\r$/, "", name)
        if (name == f) { print tolower($1); exit }
    }' "$WORKDIR/SHA256SUMS")
if [ -z "$EXPECTED" ]; then
    die "SHA256SUMS does not list $ASSET."
fi
case "$EXPECTED" in
    *[!0-9a-f]*) die "SHA256SUMS contains an invalid checksum for $ASSET." ;;
esac
if [ "${#EXPECTED}" -ne 64 ]; then
    die "SHA256SUMS contains an invalid checksum for $ASSET."
fi
ACTUAL=$(sha256_of "$WORKDIR/$ASSET") || die "Could not compute the SHA-256 checksum."
if [ -z "$ACTUAL" ]; then
    die "Could not compute the SHA-256 checksum."
fi
if [ "$ACTUAL" != "$EXPECTED" ]; then
    die "Checksum mismatch for $ASSET. The download was not installed."
fi
ok "SHA-256 verified"

case "$FORMAT" in
    pacman)
        step "Installing with pacman"
        run_root pacman -U --noconfirm "$WORKDIR/$ASSET" \
            || die "pacman did not complete. Nothing else was changed."
        ;;
    deb)
        if have apt-get; then
            step "Installing with apt"
            if [ "$VERSION_CMP" = "-1" ]; then
                run_root env DEBIAN_FRONTEND=noninteractive \
                    apt-get install -y --allow-downgrades "$WORKDIR/$ASSET" \
                    || die "apt did not complete. Nothing else was changed."
            elif [ "$ACTION" = "reinstall" ]; then
                run_root env DEBIAN_FRONTEND=noninteractive \
                    apt-get install -y --reinstall "$WORKDIR/$ASSET" \
                    || die "apt did not complete. Nothing else was changed."
            else
                run_root env DEBIAN_FRONTEND=noninteractive \
                    apt-get install -y "$WORKDIR/$ASSET" \
                    || die "apt did not complete. Nothing else was changed."
            fi
        else
            step "Installing with dpkg"
            run_root dpkg -i "$WORKDIR/$ASSET" \
                || die "dpkg did not complete. Run 'sudo apt-get -f install' to resolve dependencies."
        fi
        ;;
    rpm)
        case "$RPM_TOOL" in
            dnf)
                if [ "$VERSION_CMP" = "-1" ] || [ "$ACTION" = "reinstall" ]; then
                    step "Installing the RPM directly"
                    install_rpm_file "$WORKDIR/$ASSET" \
                        || die "rpm did not complete. Nothing else was changed."
                else
                    step "Installing with dnf"
                    run_root dnf -y install "$WORKDIR/$ASSET" \
                        || die "dnf did not complete. Nothing else was changed."
                fi
                ;;
            yum)
                if [ "$VERSION_CMP" = "-1" ] || [ "$ACTION" = "reinstall" ]; then
                    step "Installing the RPM directly"
                    install_rpm_file "$WORKDIR/$ASSET" \
                        || die "rpm did not complete. Nothing else was changed."
                else
                    step "Installing with yum"
                    run_root yum -y install "$WORKDIR/$ASSET" \
                        || die "yum did not complete. Nothing else was changed."
                fi
                ;;
            zypper)
                step "Installing with zypper"
                if [ "$ACTION" = "reinstall" ]; then
                    run_root zypper --non-interactive install --allow-unsigned-rpm --force "$WORKDIR/$ASSET" \
                        || die "zypper did not complete. Nothing else was changed."
                else
                    run_root zypper --non-interactive install --allow-unsigned-rpm "$WORKDIR/$ASSET" \
                        || die "zypper did not complete. Nothing else was changed."
                fi
                ;;
            rpm-ostree)
                step "Staging with rpm-ostree"
                run_root rpm-ostree install --assumeyes "$WORKDIR/$ASSET" \
                    || die "rpm-ostree did not complete. Nothing else was changed."
                ;;
            rpm)
                step "Installing the RPM directly"
                install_rpm_file "$WORKDIR/$ASSET" \
                    || die "rpm did not complete. Nothing else was changed."
                ;;
        esac
        ;;
    tarball)
        step "Unpacking the portable build"
        tar -xzf "$WORKDIR/$ASSET" -C "$WORKDIR" \
            || die "The tarball could not be unpacked."
        source_dir="$WORKDIR/vanta-${LATEST}-linux-x86_64"
        [ -d "$source_dir/usr" ] || die "The tarball does not contain the expected usr tree."
        step "Installing into /usr/local"
        # One elevated shell keeps this to a single authentication prompt.
        # The tarball is built on CI, so its files carry the runner's numeric
        # ownership; chown after the copy keeps a user-owned file out of /usr.
        # shellcheck disable=SC2016
        run_root sh -c '
            set -e
            mkdir -p /usr/local
            cp -a "$1/usr/." /usr/local/
            chown root:root \
                /usr/local/bin /usr/local/bin/vanta \
                /usr/local/share/applications \
                /usr/local/share/applications/vanta.desktop \
                /usr/local/share/icons \
                /usr/local/share/icons/hicolor \
                /usr/local/share/icons/hicolor/scalable \
                /usr/local/share/icons/hicolor/scalable/apps \
                /usr/local/share/icons/hicolor/scalable/apps/vanta.svg \
                /usr/local/share/doc
            chown -R root:root /usr/local/share/doc/vanta
            if command -v update-desktop-database >/dev/null 2>&1; then
                update-desktop-database /usr/local/share/applications >/dev/null 2>&1 || true
            fi
            if command -v gtk-update-icon-cache >/dev/null 2>&1; then
                gtk-update-icon-cache -q -t -f /usr/local/share/icons/hicolor >/dev/null 2>&1 || true
            fi
        ' sh "$source_dir" || die "Copying the files into /usr/local did not complete."
        ;;
esac

if [ "$FORMAT" = "rpm" ] && [ "$RPM_TOOL" = "rpm-ostree" ]; then
    ok "Vanta $LATEST is staged for the next deployment."
    info "Reboot to start the new version."
    exit 0
fi

candidate=""
if [ "$FORMAT" = "tarball" ] && [ -x /usr/local/bin/vanta ]; then
    candidate="/usr/local/bin/vanta"
else
    candidate=$(command -v vanta 2>/dev/null || true)
    if [ -z "$candidate" ] && [ -x /usr/local/bin/vanta ]; then
        candidate="/usr/local/bin/vanta"
    fi
fi
if [ -z "$candidate" ]; then
    warn "Vanta was installed, but no 'vanta' binary was found in PATH."
else
    got=$("$candidate" --version 2>/dev/null \
        | sed -n 's/^Vanta[[:space:]]\{1,\}\([0-9][0-9.]*\).*$/\1/p' \
        | head -n1 || true)
    if [ -z "$got" ]; then
        die "Vanta was installed, but $candidate could not report a version. Check that the Qt 6 runtime is installed."
    fi
    if [ "$got" != "$LATEST" ]; then
        die "Vanta was installed, but $candidate reports version $got instead of $LATEST. Check which 'vanta' is first in PATH."
    fi
fi

if [ "$ACTION" = "update" ] && [ "$INSTALLED" != "unknown" ]; then
    ok "Vanta $INSTALLED $ARROW $LATEST is installed."
else
    ok "Vanta $LATEST is installed."
fi

if [ "$FORMAT" = "tarball" ]; then
    case ":$PATH:" in
        *:/usr/local/bin:*) ;;
        *) info "Add /usr/local/bin to PATH if the 'vanta' command is not found." ;;
    esac
    info "Log out and back in if the application menu entry does not appear."
fi
say "Run 'vanta' to start it, or open it from your application menu."
if [ "$ACTION" = "update" ] || [ "$ACTION" = "reinstall" ]; then
    info "Close and reopen Vanta if it is already running."
fi