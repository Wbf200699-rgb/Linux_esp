#!/usr/bin/env bash
# Source this file from Bash: . ./activate.sh
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    echo '请在 Bash 终端运行：source ./activate.sh'
    exit 1
fi

_p4_workspace="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
export IDF_PATH="${_p4_workspace}/.esp/esp-idf"
export IDF_TOOLS_PATH="${_p4_workspace}/.espressif"
export IDF_PYTHON_ENV_PATH="${IDF_TOOLS_PATH}/python_env/idf5.5_py3.12_env"
export IDF_TARGET=esp32p4
export IDF_GITHUB_ASSETS=dl.espressif.com/github_assets
export CCACHE_DIR="${_p4_workspace}/.setup/ccache"
export PIP_CACHE_DIR="${_p4_workspace}/.setup/pip-cache"
export BISON_PKGDATADIR="${_p4_workspace}/.setup/sysroot/usr/share/bison"
export LD_LIBRARY_PATH="${_p4_workspace}/.setup/sysroot/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export PATH="${_p4_workspace}/.setup/sysroot/usr/bin:${PATH}"
# Keep the workspace Python independent of ROS packages inherited from the shell.
unset PYTHONPATH PYTHONHOME
source "${IDF_PATH}/export.sh"
_p4_activation_status=$?
unset _p4_workspace
return "${_p4_activation_status}"
