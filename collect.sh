#!/usr/bin/env bash
set -Eeuo pipefail

# Usage:
#   ./collect.sh [source_directory] [output_file]
#
# Examples:
#   ./collect.sh .
#   ./collect.sh "/path/to/project" collected-source.txt
#   ./collect.sh "D:/linux/automation/__new/rmc-fabric" collected-source.txt

ROOT="${1:-.}"
OUTPUT="${2:-collected-source.txt}"

# Resolve the source directory.
ROOT="$(cd "$ROOT" && pwd)"

# Resolve the output path without requiring that the file already exists.
if command -v realpath >/dev/null 2>&1; then
    OUTPUT="$(realpath -m "$OUTPUT")"
else
    OUTPUT_DIR="$(dirname "$OUTPUT")"
    OUTPUT_NAME="$(basename "$OUTPUT")"
    OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"
    OUTPUT="$OUTPUT_DIR/$OUTPUT_NAME"
fi

# Find the Git repository containing ROOT, if one exists.
GIT_ROOT=""

if command -v git >/dev/null 2>&1; then
    GIT_ROOT="$(
        git -C "$ROOT" rev-parse --show-toplevel 2>/dev/null || true
    )"
fi

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
}

# Start with an empty output file.
: > "$OUTPUT"

if [[ -n "$GIT_ROOT" ]]; then
    # Git-aware scan.
    #
    # Ask Git itself for every file that is NOT ignored:
    #   --cached            tracked files
    #   --others            untracked files
    #   --exclude-standard  honour .gitignore at every level,
    #                       .git/info/exclude, and core.excludesFile
    #
    # Delegating the ignore rules to Git fixes the previous behaviour,
    # where only ignored *directories* were pruned and ignored *files*
    # (e.g. upload.txt, patch*.txt, plan*.txt) were still collected.
    #
    # When ROOT is a subdirectory of the repository, --show-prefix gives
    # the pathspec that limits the listing to that subtree.
    PREFIX="$(git -C "$ROOT" rev-parse --show-prefix)"

    git -C "$GIT_ROOT" ls-files -z \
        --cached --others --exclude-standard --full-name \
        -- "${PREFIX:-.}" |
    while IFS= read -r -d '' relpath; do
        file="$GIT_ROOT/$relpath"

        # Skip non-regular files (e.g. submodule gitlinks, files deleted
        # from the working tree but still tracked) and the output file.
        [[ -f "$file" && "$file" != "$OUTPUT" ]] || continue

        append_file "$file"
    done
else
    # Basic scan when the directory is not inside a Git repository.
    find "$ROOT" \
        -type d -name ".git" -prune -o \
        -type f ! -path "$OUTPUT" -print0 |
    while IFS= read -r -d '' file; do
        append_file "$file"
    done
fi

echo "Collected files into: $OUTPUT"
