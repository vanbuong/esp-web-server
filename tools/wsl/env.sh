# Sourced by the other tools/wsl scripts: loads ESP-IDF v6.1.1 (installed in WSL by EIM)
# and sets REPO to the repository root.
IDF_ACTIVATE="$HOME/.espressif/tools/activate_idf_v6.1.1.sh"
if [ ! -r "$IDF_ACTIVATE" ]; then
    echo "ESP-IDF v6.1.1 not found in WSL ($IDF_ACTIVATE). See README: toolchain setup." >&2
    exit 2
fi
# The activation script reads unset variables (e.g. ZSH_VERSION), so nounset is off while it loads.
set +u
# shellcheck disable=SC1090
source "$IDF_ACTIVATE" >/dev/null
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
