
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

# Enable .gitignore filtering if the directory is inside a Git repository
# containing at least one .gitignore file.
USE_GITIGNORE=false

if [[ -n "$GIT_ROOT" ]] && \
   find "$GIT_ROOT" -type f -name ".gitignore" -print -quit 2>/dev/null | grep -q .; then
    USE_GITIGNORE=true
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

if [[ "$USE_GITIGNORE" == true ]]; then
    # Git-aware scan.
    #
    # The nested bash process receives the Git root and directory as
    # positional arguments. It does not depend on shell functions being
    # exported, avoiding:
    #
    #   is_ignored_directory: command not found
    #
    find "$ROOT" \
        -type d -name ".git" -prune -o \
        -type d -exec bash -c '
            git_root="$1"
            directory="$2"

            # Do not prune the repository/source root itself.
            if [[ "$directory" == "$git_root" ]]; then
                exit 1
            fi

            relative_path="${directory#"$git_root"/}"

            # Exit successfully when Git says this directory is ignored.
            git -C "$git_root" check-ignore \
                --quiet \
                --no-index \
                -- \
                "${relative_path%/}/"
        ' _ "$GIT_ROOT" {} \; -prune -o \
        -type f ! -path "$OUTPUT" -print0 |
    while IFS= read -r -d '' file; do
        append_file "$file"
    done
else
    # Basic scan when no .gitignore file is available.
    find "$ROOT" \
        -type d -name ".git" -prune -o \
        -type f ! -path "$OUTPUT" -print0 |
    while IFS= read -r -d '' file; do
        append_file "$file"
    done
fi

echo "Collected files into: $OUTPUT"
