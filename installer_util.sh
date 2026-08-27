#!/bin/bash

__print_verbose(){
    if [[ $VERBOSE = true ]]; then
        echo "$@"
    fi
}

# ######################################
# Check install tag.
# Match if:
# - ${INSTALL_TAG_FILE} file is empty
# - ${INSTALL_TAG_FILE} file doesn't exist
# - ${INSTALL_TAG_FILE} file has string that
#   matches give tag string
# Note:
#   The extra install data in a dir/
#   which matches the tag
#   provided is only installed if the
#   whole component matches a given tag
__check_install_tag(){
    if [ -z "${INSTALL_TAG}" ]; then return 0; fi
    local src=${1:-${SRC_DIR}}
    if ! __check_dir "${src}"; then return 1; fi
    ######
    # - File doesn't exit
    # - File is empty
    # - File only has spaces
    if __is_empty "$src/${INSTALL_TAG_FILE}"; then return 0; fi
    while IFS= read -r line; do
        if [ "${INSTALL_TAG}" = "${line}" ]; then
            return 0;
        fi
    done < <(grep -v '^ *#' < "${src}/${INSTALL_TAG_FILE}")
    return 1
}

# ################################
# Check system tag,
# if does not match, exit script
__check_install_tag_fatal(){
    if __check_install_tag "${1}" ; then return 0; fi
    echo "${INSTALL_TAG} not supported"
    exit 0
}

# #####################################
# Check to see if directory exists
# @param directory to check
# @return 0 if exists, 1 otherwise
__check_dir(){
    local dir="${1}"
    if [ -z "${dir}" ] || [ ! -d "${dir}" ]; then
        echo "ERROR: Missing directory '${src}'."
        return 1
    fi
    return 0
}

# #####################################
# - File doesn't exit
# - File is empty
# - File only has spaces
# @param file to check
# @return 0 if empty, 1 on error
__is_empty(){
    local path="${1}"
    if [ ! -f "${path}" ] || \
       [ ! -s "${path}" ] || \
       ! grep  -q '[^[:space:]]' "${path}" ;
    then
        return 0
    fi
    return 1
}

# #####################################
# Check if value is contained in list
# @param key to check
# @param list of values
# @return 0 if key is in the list, 1 otherwise
__contains(){
    if [ $# -lt 2 ]; then return 1; fi
    local key=${1}; shift
    if [ -z "$key" ]; then return 1; fi
    local list="$*"
    for v in $list; do
     	if [ -n "$v" ] && [ "$key" == "$v" ]; then
     	    return 0
     	fi
    done
    return 1
}

# #####################################
# Get default info. Package name is the
# last directory name in the working
# directory path. If doesn't exist,
# use 'unknown'. Version is 0.0.0
__get_default_info(){
    PKG_DISPLAY_NAME="${SCRIPT_DIR##*/}"
    PKG_NAME="${SCRIPT_DIR##*/}"
    if [ -z "$PKG_NAME" ]; then PKG_NAME="unknown"; fi
    PKG_VERSION="0.0.0"
    PKG_FULL_VERSION="${PKG_NAME}-${PKG_VERSION}"
}

# #####################################
# Populate the package info variables
# from ${PKG_INFO_FILE}.
# shellcheck disable=2034
__update_info(){
    local info_file="${SCRIPT_DIR:?}/${PKG_INFO_FILE}"
    if [ -f "${info_file}" ]; then
        PKG_DISPLAY_NAME="$( grep "${PKG_DISPLAY_NAME_ID}" "${info_file}" | cut -d ':' -f 2 | tr -dc '[:print:]' | tr -d '[:space:]')"
        PKG_NAME="$( grep "${PKG_NAME_ID}" "${info_file}" | cut -d ':' -f 2 | tr -dc '[:print:]' | tr -d '[:space:]')"
        PKG_VERSION="$( grep "${PKG_VERSION_ID}" "${info_file}" | cut -d ':' -f 2 | tr -dc '[:print:]' | tr -d '[:space:]')"
        if [ -n "${PKG_NAME}" ] && [ -n "${PKG_VERSION}" ]; then
            PKG_DISPLAY_NAME="${PKG_DISPLAY_NAME:-${PKG_NAME}}"
            PKG_FULL_VERSION="${PKG_NAME}-${PKG_VERSION}"
            return 0
        fi
    fi
    __get_default_info
    return 0
}

# #####################################
# Create file MD5 checksum
# @param file
# @param file location directory
# @return 0 on success, 1 otherwise
__make_md5(){
    local file=${1}
    local file_dir=
    if ! file_dir="$(readlink -m "${2-.}" )"; then return 1; fi
    local file_path=${file_dir:?}/${file:?}
    if [ ! -f "${file_path}" ]; then return 1; fi
    pushd "${file_dir}" &>/dev/null || return 1
    md5sum "${file}" > "${file}.md5" 2>/dev/null
    local r_code=$?
    popd &>/dev/null || return 1;
    return $r_code
}

# #####################################
# Remove a directory
# The directory path is relative to
# the TRG_DIR
# @param directory path to remove in TRG_DIR
# @return 0 on success, error otherwise
__clear_dir(){
   local path=$1
   if [ -z "${path}" ]; then
       echo "Error: Clear dir failed, please specify directory."
       return 1
   fi
   if [ ! -d "${TRG_DIR:?}/${path:?}" ]; then
       return 0
   fi
   rm -fr "${TRG_DIR:?}/${path:?}/" 2>/dev/null
   return 0
}

# #####################################
# Check to see if current directory
# can and should be installed
# @param directory to check
# @return 0 to install, 1 to skip
__install_checks(){
    local src="${1}"
    if [ -z "${src}" ] || ! __check_dir "${src}" || \
       [ -f "${src}/${INSTALL_SKIP_FILE}" ]
    then
        return 1;
    fi
    return 0
}

# #####################################
# Run specified install script in a given
# directory
# @param directory path
# @return 0 on success, 1 otherwise
__install(){
    local __path="$1"
    local __file="$2"
    if [ -z "${__path}" ] || [ -z "$__file" ]; then return 1; fi
    if [ ! -d "${__path}" ] || [ ! -f "${__path}/${__file}" ]; then return 1; fi
    #echo "XXX->$__installer_file"
    local tmp_src_dir=${SRC_DIR}
    SRC_DIR=${__path}
    # shellcheck disable=SC1090
    source "${__path}/${__file}"
    SRC_DIR=${tmp_src_dir};
    return 0
}

# #####################################
# Install data in the specified directory
# @param directory path
# @return 0 on success, 1 otherwise
__install_dir(){
    local __dir="${1:?}"
    local __installer_file="$INSTALL_CONFIG_PREFIX"
    if [ -n "$INSTALL_TAG" ]; then
        __installer_file="${INSTALL_TAG}.${INSTALL_CONFIG_PREFIX}"
    fi
    local rcode=0
    __install "${__dir}" "${__installer_file}"
    rcode=$((rcode + $?))
    __install "${__dir}" "${INSTALL_CONFIG_ALL}"
    rcode=$((rcode + $?))
    if [ "$rcode" -eq 2 ]; then return 1; fi
    return 0
}

# #####################################
# Install data in the specified directory
__install_dir_recursive(){
    local src=
    if ! src="$(readlink -m "${1}")"; then return 1; fi
    if ! __install_checks "${src}"; then return 1; fi
    __install_dir "${src}"
    find "${src:?}"/* -prune -type d 2>/dev/null | while IFS= read -r d; do
        if [ "${SRC_DIR}/${TRG_DIR}" = "${d}" ]; then continue; fi
        __install_dir_recursive "${d}"
    done
    return 0
}

# #####################################
# Install data in the specified directory
__install_components(){
    local src=
    if ! src="$(readlink -m "${1}")" || [ -z "${src}" ]; then return 1; fi
    shift
    local components="$*"
    if ! __install_checks "${src}"; then return 1; fi
    find "${src}"/* -prune -type d 2>/dev/null | while IFS= read -r d; do
        if [ -z "${components}" ] || __contains "${d##*/}" "${components}"; then
            __print_verbose "Installing component: '${d##*/}'"
            __install_dir_recursive "${d}"
        fi
    done
    return 0
}

# #####################################
# Copy file to target directory and
# create md5 checksum after copy was successful
# Source files are relative to the SRC_DIR
# path, target directory is relative to the TRG_DIR
# @param source file to copy
# @param target directory
# @return 0 on success, error code on failure
__copy_md5_file() {
    local src=$1
    local trg=$2
    local trg_file=${3:-${src}}
    if [ ! -f "${SRC_DIR}/${src}" ]; then
        echo "Error: MD5 copy failed, source '${SRC_DIR}/${src}' is missing or is not a regular file."
        return 2
    fi
    if ! copy "${src}" "${trg}" "${trg_file}"; then return 3; fi
    __print_verbose "Build md5 for file '$trg_file'"
    if ! __make_md5 "${trg_file}" "${TRG_DIR}/${trg}"; then
        echo "Error: Failed to create md5 checksum '${TRG_DIR}/${trg}/${trg_file}'."
        return 5
    fi
    return 0
}


# #############################################################################
# Config file API
#
# #############################################################################

# #####################################
# Append data to a file, if file doesn't
# exist, create it.
# Target directory is relative to the TRG_DIR
# @param file path
# @param data to append
# @return 0 on success, error code on failure
append_data(){
    local file_path="$1"
    shift
    local data="$*"
    __print_verbose "Append data to file '$file_path'"
    if [ -z "${file_path}" ]; then
        echo "Error: Append failed, empty target dir or file."
        return 1
    fi
    file_path=$( echo "${TRG_DIR:?}/$file_path" | tr -s '/' )
    if ! touch "$file_path" || [ ! -f "${file_path}" ]; then
        echo "Error: Target file '${file_path}' does not exist."
        return 2
    fi
    echo "${data}" >> "${file_path}"
    return 0
}

# #####################################
# Copy file to target directory
# Source files are relative to the SRC_DIR
# path, target directory is relative to the TRG_DIR
# @param source file to copy
# @param target directory
# @param target file
# @return 0 on success, error code on failure
copy(){
    local src="$1"
    local trg=$2
    local trg_file=$3
    __print_verbose "Copy '$src' to '$trg/$trg_file' "
    if [ -z "${trg}" ] || [ -z "${src}" ]; then
        echo "Error: Copy failed, empty target dir or source file."
        return 1
    fi
    #if [ ! -f "${SRC_DIR}/${src}" ] && [ ! -d "${SRC_DIR}/${src}" ]; then
    #    echo "Error: Copy failed, missing source '${SRC_DIR}/${src}'."
    #    return 2
    #fi
    if ! make_dir "${trg}"; then return 3; fi
    local target="${TRG_DIR}/${trg}"
    if [ -n "$trg_file" ]; then
        if [ ! -f "${SRC_DIR}/${src}" ]; then
            echo "Error: Copy failed, specified target file '${trg_file}', but source is a directory '${SRC_DIR}/${src}'."
            return 2
        fi
        target="${TRG_DIR}/${trg}/${trg_file}"
    fi
    # shellcheck disable=SC2086
    if ! cp -r ${SRC_DIR}/${src} "${target}"  ; then
        echo "Error: Failed to copy '${SRC_DIR}/${src}' to '${target}'."
        return 4
    fi
    return 0
}

# #####################################
# Copy file or directory contents to target directory and
# create md5 checksum after copy was successful. If the
# source is a directory, create md5 checksum for each file
# copied.
# Source files are relative to the SRC_DIR
# path, target directory is relative to the TRG_DIR
# @param source file or directory to copy
# @param target directory
# @param target file (only applies if source is a file)
# @return 0 on success, error code on failure
copy_md5() {
    local src=$1
    local trg=$2
    local trg_file=${3:-${src}}
    if [ ! -d "${SRC_DIR}/${src}" ]; then
        __copy_md5_file "${src}" "${trg}" "${trg_file}"
        return $?
    fi
    for f in "${SRC_DIR}"/"${src}"/*; do
        __copy_md5_file "${f##*/}" "${trg}"
    done
    return 0
}

# #####################################
# Create a directory
# The directory is created relative to
# the TRG_DIR
# @param directory path to create in TRG_DIR
# @return 0 on success, error otherwise
make_dir(){
    if [ -z "${1}" ]; then
        echo "ERROR: Make dir failed, please specify directory."
        return 1
    fi
    local __path="${TRG_DIR:?}/$1"
    __print_verbose "Create directory '$__path'"
    mkdir -p "${__path}" 2>/dev/null
    if [ ! -d "${__path}" ]; then
        echo "ERROR: Unable to make directory '${__path}'."
        return 3
    fi
    return 0
}

# #####################################
# Create a md5 for a given file or
# all filed in the directory
# Files/dir are relative to the TRG_DIR
# @param target directory
# @param target file (optional)
# @return 0 on success, error otherwise
make_md5(){
    local trg_dir=$1
    local trg_file=$2
    if [ ! -d "${TRG_DIR:?}/${trg_dir}" ]; then
        echo "ERROR: Missing directory '${TRG_DIR:?}/${trg_dir}'."
        return 1
    fi
    if [ -n "${trg_file}" ] && [ ! -f "${TRG_DIR:?}/${trg_file}" ]; then
        echo "ERROR: Missing file '${TRG_DIR:?}/${trg_file}'."
        return 1
    fi
    if [ -n "${trg_file}" ]; then
        __make_md5 "${trg_file}" "${TRG_DIR:?}/${trg_dir}"
        return $?
    fi
    rm -f "${TRG_DIR}"/"${trg_dir}"/*.md5
    for f in "${TRG_DIR}"/"${trg_dir}"/*; do
        __make_md5 "${f##*/}" "${TRG_DIR:?}/${trg_dir}"
    done
    return 0
}

# #####################################
# Create an empty file
# @param file to create
# @param directory path to create in TRG_DIR
# @return 0 on success, error otherwise
touch_file(){
    local __file=$1
    local __dir=$2
    if [ -z "${__file}" ] || [ -z "${__dir}" ]; then
        echo "ERROR: Touch file failed, empty target dir (${__dir}) or target file (${__file})."
        return 1
    fi
    __print_verbose "Touch '$__file' in '$__dir' "
    if ! make_dir "${__dir}"; then return 3; fi
    local trg_file_path="${TRG_DIR}/${__dir}/${__file}"
    if ! touch "$trg_file_path" 2>/dev/null ; then
        echo "ERROR: Failed to touch '$trg_file_path'."
        return 4
    fi
    return 0
}

# #####################################
# Change mod
# @param mod string
# @param directory or file path to modify in TRG_DIR
# @return 0 on success, error otherwise
change_mod(){
    local mod=$1
    local trg=$2
    if [ -z "${trg}" ] || [ -z "${mod}" ]; then
        echo "ERROR: Change mod failed, empty target (${trg}) or mod (${mod}) string."
        return 1
    fi
    __print_verbose "Change mod '$mod' of '$trg' "
    local target=
    if ! target="$(readlink -m "${TRG_DIR}/${trg}")"; then return 1; fi
    if [ -f "${target}" ]; then
        if ! chmod "${mod}" "${target}" 2>/dev/null ; then
            echo "ERROR: Failed to change mod '${mod}' of file '${target}'."
            return 4
        fi
    elif [ -d "${target}" ]; then
        if ! find "${target}" -type f -exec chmod "${mod}" {} \; ; then
            echo "ERROR: Failed to change mod '${mod}' in dir '${target}'."
            return 4
        fi
    fi
    return 0
}
