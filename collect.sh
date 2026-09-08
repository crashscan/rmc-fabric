#!/usr/bin/env bash
set -Eeuo pipefail

# Usage:
#   ./collect.sh [-o output_file] [source_directory ...]
#
# Examples:
#   ./collect.sh
#   ./collect.sh src include tests
#   ./collect.sh -o collected-source.txt "/path/to/project" "/path/to/other"
#   ./collect.sh -o collected-source.txt "D:/linux/automation/__new/rmc-fabric" ../shared
#
# Notes:
#   - Options must come before source directories.
#   - If no source directory is given, the current directory is used.
#   - Each source is scanned with its own Git context: sources may live
#     in different repositories, or in no repository at all.
#   - Overlapping or repeated sources are de-duplicated: every file is
#     collected at most once.

OUTPUT="collected-source.txt"

usage() {
    cat <<'EOF'
Usage:
  collect.sh [-o output_file] [source_directory ...]

Options (must come before source directories):
  -o FILE   Write output to FILE (default: collected-source.txt)
  -h        Show this help

If no source directory is given, the current directory is used.
EOF
}

while getopts ":o:h" opt; do
    case "$opt" in
        o)
            OUTPUT="$OPTARG"
            ;;
        h)
            usage
            exit 0
            ;;
        :)
            echo "Error: option -$OPTARG requires an argument." >&2
            usage >&2
            exit 2
            ;;
        \?)
            echo "Error: unknown option -$OPTARG." >&2
            usage >&2
            exit 2
            ;;
    esac
done
shift $((OPTIND - 1))

SOURCES=("$@")

if ((${#SOURCES[@]} == 0)); then
    SOURCES=(".")
fi

# Resolve the output path without requiring that the file already exists.
if command -v realpath >/dev/null 2>&1; then
    OUTPUT="$(realpath -m "$OUTPUT")"
else
    OUTPUT_DIR="$(dirname "$OUTPUT")"
    OUTPUT_NAME="$(basename "$OUTPUT")"
    OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"
    OUTPUT="$OUTPUT_DIR/$OUTPUT_NAME"
fi

# Start with an empty output file BEFORE scanning any source, so the
# output file itself is never collected.
: > "$OUTPUT"

# De-duplication state and file counter. The scan loops below run in the
# current shell (process substitution, not pipelines) precisely so that
# updates to these persist across sources.
declare -A SEEN=()
COLLECTED_COUNT=0

# Convert Unix/MSYS paths to Windows-style paths when possible.
display_path() {
    local path="$1"

    if command -v cygpath >/dev/null 2>&1; then
        cygpath -w "$path"
    elif [[ "$path" =~ ^/([a-zA-Z])/(.*)$ ]]; then
        # Git Bash/MSYS format:
        #   /d/path/file.cpp -> D:\path\file.cpp
        printf '%s:\\%s\n' \
            "${BASH_REMATCH[1]^}" \
            "${BASH_REMATCH[2]//\//\\}"
    else
        printf '%s\n' "$path"
    fi
}

# Determine a Markdown code-fence language from the filename.
language_for_file() {
    local file="$1"
    local name
    local lower

    name="$(basename "$file")"
    lower="${name,,}"

    case "$lower" in
        *.cpp|*.cc|*.cxx|*.c++)
            printf 'cpp'
            ;;
        *.h|*.hh|*.hpp|*.hxx|*.h++)
            printf 'cpp'
            ;;
        *.c)
            printf 'c'
            ;;
        *.cmake|cmakelists.txt)
            printf 'cmake'
            ;;
        *.md|readme|readme.*)
            printf 'markdown'
            ;;
        *.sh|*.bash)
            printf 'bash'
            ;;
        *.py)
            printf 'python'
            ;;
        *.js|*.jsx)
            printf 'javascript'
            ;;
        *.ts|*.tsx)
            printf 'typescript'
            ;;
        *.java)
            printf 'java'
            ;;
        *.rs)
            printf 'rust'
            ;;
        *.go)
            printf 'go'
            ;;
        *.json)
            printf 'json'
            ;;
        *.yaml|*.yml)
            printf 'yaml'
            ;;
        *.xml)
            printf 'xml'
            ;;
        *.sql)
            printf 'sql'
            ;;
        *.txt|*.ini|*.cfg|*.conf|*.properties)
            printf 'text'
            ;;
        *)
            printf 'text'
            ;;
    esac
}

# Return success if the file is text-based.
is_text_file() {
    local file="$1"
    local mime_type

    if command -v file >/dev/null 2>&1; then
        mime_type="$(file --brief --mime-type "$file" 2>/dev/null || true)"

        [[ "$mime_type" == text/* ]] ||
        [[ "$mime_type" == application/json ]] ||
        [[ "$mime_type" == application/xml ]] ||
        [[ "$mime_type" == application/yaml ]] ||
        [[ "$mime_type" == application/x-yaml ]] ||
        [[ "$mime_type" == application/javascript ]] ||
        [[ "$mime_type" == application/x-sh ]] ||
        [[ "$mime_type" == application/x-cmake ]]
    else
        # Fallback when the `file` command is unavailable.
        # Empty files are considered text files.
        [[ ! -s "$file" ]] || grep -Iq . "$file"
    fi
}

# Append one file to the output.
append_file() {
    local file="$1"
    local formatted_path
    local language

    # Skip duplicates produced by overlapping or repeated sources.
    if [[ -n "${SEEN[$file]:-}" ]]; then
        return 0
    fi
    SEEN["$file"]=1

    if ! is_text_file "$file"; then
        return 0
    fi

    formatted_path="$(display_path "$file")"
    language="$(language_for_file "$file")"

    {
        printf '######## file: %s\n' "$formatted_path"
        printf '```%s\n' "$language"
        cat "$file"
        printf '\n```\n\n'
    } >> "$OUTPUT"

    ((++COLLECTED_COUNT))
}

# Scan one resolved source directory.
collect_from_source() {
    local root="$1"
    local git_root=""
    local prefix
