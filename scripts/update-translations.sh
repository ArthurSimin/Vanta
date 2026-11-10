#!/usr/bin/env bash
#
# Regenerate the translation catalogs from the source tree.
#
# Two steps:
#   1. lupdate scans src/ and refreshes every translations/vanta_*.ts, adding
#      new strings and marking removed ones obsolete. Existing translations are
#      kept.
#   2. lrelease compiles the complete catalogs into resources/i18n/vanta_*.qm.
#      Those .qm files are committed and compiled into the binary through
#      resources.qrc, so a release build needs no Qt Linguist tooling.
#
# Scaffold catalogs (a language that is not finished yet) are refreshed by
# lupdate but not released, so they never ship half-translated.
#
# Usage: scripts/update-translations.sh
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

find_tool() {
    local name="$1"
    if command -v "$name" >/dev/null 2>&1; then
        command -v "$name"
        return
    fi
    # Distribution packages keep the Qt 6 tools out of PATH on some systems.
    local candidate
    for candidate in /usr/lib/qt6/bin/"$name" /usr/lib64/qt6/bin/"$name"; do
        if [ -x "$candidate" ]; then
            echo "$candidate"
            return
        fi
    done
    echo "error: $name was not found. Install the Qt 6 Linguist tools." >&2
    exit 1
}

lupdate="$(find_tool lupdate)"
lrelease="$(find_tool lrelease)"

# Complete catalogs: translated and shipped in the binary.
shipped=(ru pt_BR pt es)
# Scaffolds: kept up to date for translators, not compiled into the binary.
scaffolds=(de fr it pl uk zh_CN ja ko)

mkdir -p translations resources/i18n

catalogs=()
for code in "${shipped[@]}" "${scaffolds[@]}"; do
    catalogs+=("-ts" "translations/vanta_${code}.ts")
done

"$lupdate" src "${catalogs[@]}"

for code in "${shipped[@]}"; do
    "$lrelease" "translations/vanta_${code}.ts" -qm "resources/i18n/vanta_${code}.qm"
done

echo "Catalogs refreshed and shipped catalogs compiled."
