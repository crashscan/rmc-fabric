#!/bin/bash
# shellcheck disable=SC1091,2034

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
source "${SCRIPT_DIR}/installer_util.sh"

# #########################
# Package info
PKG_INFO_FILE="info"
PKG_DISPLAY_NAME_ID="display"
PKG_NAME_ID="pkg"
PKG_VERSION_ID="version"
PKG_DISPLAY_NAME="unknown"
PKG_NAME="unknown"
PKG_VERSION="0.0.0"
PKG_FULL_VERSION="unknown-0.0.0"
# Directories/files
SRC_DIR="${SCRIPT_DIR}"
TRG_DIR=".package"
TMP_DIR=".package"
INSTALL_CONFIG_PREFIX="installer.cfg.sh"
INSTALL_CONFIG_ALL="all.${INSTALL_CONFIG_PREFIX}"
INSTALL_SKIP_FILE=".ignore"
# Flags
VERBOSE=false
INSTALL_TO_TARGET=false
INSTALL_TO_ARCHIVE=false
INSTALL_COMPONENTS=false
COMPONENTS_LIST=
# Tags
INSTALL_TAG=""
INSTALL_TAG_FILE=".install_tag"
# #########################

read -r -d '' HELP << EOM
Usage: ${BASH_SOURCE##*/} [OPTION]...
Install ${PKG_DISPLAY_NAME} updater.

Mandatory arguments to long options are mandatory for short options too.
  -h  Display Help
  -t  Installation target directory
  -i  Install to target
  -a  Create installation archive
  -s  Specify install tag
  -c  Install using components list, if no list is provided install all components
  -v  Verbose
EOM

if [ $# -eq 0 ]; then
    echo "$HELP"
    exit 0
fi
__update_info

while getopts "hviac:s:t:" option; do
    case $option in
        h) # display Help
            echo "$HELP"
            exit;;
        t) # Target install directory
            TRG_DIR=${OPTARG:-.}
            ;;
        i) # Install to dir
            INSTALL_TO_TARGET=true 
            ;;
        a) # Archive install
            INSTALL_TO_ARCHIVE=true
            ;;
        v) # Verbose output
        	VERBOSE=true
            ;;
     	s) # Install components with specific install tag
     	    INSTALL_TAG=${OPTARG}
            ;;
     	c) # Install components
            COMPONENTS_LIST=${OPTARG}
            INSTALL_COMPONENTS=true
            ;;
        \?) # Invalid option
            echo "Invalid option"
            echo "$HELP"
            exit;;
    esac
done

# #####################################
# Install the files to the target
# directory using specified install paths.
install2target(){
    if [[ $INSTALL_COMPONENTS = true ]]; then
        __install_components "${SRC_DIR}" "${COMPONENTS_LIST}"
    else
        __install_dir_recursive "${SRC_DIR}"
    fi
    return $?
}

# #####################################
# Install the files to temporary dir
# and create an archive in the target
# directory.
install2archive(){
    # Destructor
    # Change target directory on exit
    # shellcheck disable=SC2317
    install2archive_cleanup() {
        TRG_DIR="${user_trg}"
    }
    trap install2archive_cleanup RETURN

    local name=
    local user_trg=${TRG_DIR}

    TRG_DIR="$(mktemp -d)"
    name="${PKG_FULL_VERSION:?}"
    if ! __clear_dir "${name}"; then return 1; fi
    if ! make_dir "${name}"; then return 1; fi
    TRG_DIR="${TRG_DIR}/${name}"
    install2target
    __print_verbose "Build archive '${name}.tgz'"
    if ! tar -czvf "${user_trg}/${name}.tgz" -C "${TRG_DIR}" . &>/dev/null; then
        echo "Failed to install to archive '${user_trg}/${name}.tgz'"
        return 1
    fi
    rm -rf "${TRG_DIR:?}"a
    return 0
}

if [[ $INSTALL_TO_TARGET = true ]]; then
    echo "Installing to target ... "
    if install2target; then echo "Success."; else echo "Failed."; fi
fi

if [[ $INSTALL_TO_ARCHIVE = true ]]; then
    echo "Building install archive ... "
    if install2archive; then echo "Success."; else echo "Failed."; fi
fi

if [[ $INSTALL_TO_TARGET = false ]] && [[ $INSTALL_TO_ARCHIVE = false ]]; then
    echo "$HELP"
    exit 0
fi

exit 0
