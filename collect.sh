#!/usr/bin/env bash
set -Eeuo pipefail

# Usage:
#   ./collect.sh [-o output_file] [input ...]
#
# Examples:
#   ./collect.sh
#   ./collect.sh src include tests
#   ./collect.sh -o collected-source.txt main.cpp lib/util.cpp include
#   ./collect.sh -o collected-source.txt "D:/linux/automation/__new/rmc-fabric" ../shared/readme.md
#
# Notes:
#   - Options must come before inputs.
#   - Each input is a file or a directory. Directories are scanned
#     recursively; files are collected directly.
#   - An explicitly named file is collected even if Git ignores it;
#     directory scans honour Git ignore rules.
#   - If no input is given, the current directory is used.
#   - Overlapping or repeated inputs are de-duplicated: every file is
#     collected at most once.

OUTPUT="collected-source.txt"

usage() {
    cat <<'EOF'
Usage:
  collect.sh [-o output_file] [input ...]

Options (must come before inputs):
  -o FILE   Write output to FILE (default: collected-source.txt)
  -h        Show this help

Each input is a file or a directory. Directories are scanned
recursively (honouring Git ignore rules inside a repository);
files are collected directly, even if ignored by Git.

If no input is given, the current directory is used.
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

INPUTS=("$@")

if ((${#INPUTS[@]} == 0)); then
    INPUTS=(".")
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

# Start with an empty output file BEFORE processing any input, so the
# output file itself is never collected.
: > "$OUTPUT"

# De-duplication state and file counter. The scan loops below run in the
# current shell (process substitution, not pipelines) so that updates to
# these persist across inputs.
declare -A SEEN=()
COLLECTED_COUNT=0

# Resolve a file path to a canonical physical path. The dirname is
# resolved the same way as directory inputs (cd && pwd), so a file
# passed directly de-duplicates against the same file found by a
# directory scan.
resolve_file() {
    local file="$1"
    local dir

    dir="$(cd "$(dirname "$file")" && pwd)"
    printf '%s/%s\n' "$dir" "$(basename "$file")"
}

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

    # Skip duplicates produced by overlapping or repeated inputs.
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

# Scan one resolved directory input.
collect_from_dir() {
    local root="$1"
    local git_root=""
    local prefix
    local relpath
    local file

    # Find the Git repository containing this directory, if one exists.
    # Each directory input may belong to a different repository (or to
    # none).
    if command -v git >/dev/null 2>&1; then
        git_root="$(
            git -C "$root" rev-parse --show-toplevel 2>/dev/null || true
        )"
    fi

    if [[ -n "$git_root" ]]; then
        # Git-aware scan.
        #
        # Ask Git itself for every file that is NOT ignored:
        #   --cached            tracked files
        #   --others            untracked files
        #   --exclude-standard  honour .gitignore at every level,
        #                       .git/info/exclude, and core.excludesFile
        #
        # When the directory is a subdirectory of the repository,
        # --show-prefix gives the pathspec that limits the listing to
        # that subtree.

        # Normalize to the same path style as $root (e.g. /d/... rather
        # than D:/... on MSYS) so file paths compare correctly against
        # $OUTPUT and de-duplicate reliably.
        git_root="$(cd "$git_root" && pwd)"

        prefix="$(git -C "$root" rev-parse --show-prefix)"

        while IFS= read -r -d '' relpath; do
            file="$git_root/$relpath"

            # Skip non-regular files (e.g. submodule gitlinks, files
            # deleted from the working tree but still tracked) and the
            # output file.
            [[ -f "$file" && "$file" != "$OUTPUT" ]] || continue

            append_file "$file"
        done < <(
            git -C "$git_root" ls-files -z \
                --cached --others --exclude-standard --full-name \
                -- "${prefix:-.}"
        )
    else
        # Basic scan when the directory is not inside a Git repository.
        while IFS= read -r -d '' file; do
            append_file "$file"
        done < <(
            find "$root" \
                -type d -name ".git" -prune -o \
                -type f ! -path "$OUTPUT" -print0
        )
    fi
}

for INPUT in "${INPUTS[@]}"; do
    if [[ -f "$INPUT" ]]; then
        # Explicitly named file: collect it directly, even if Git would
        # ignore it.
        FILE_PATH="$(resolve_file "$INPUT")"

        if [[ "$FILE_PATH" == "$OUTPUT" ]]; then
            echo "Warning: skipping the output file itself: $INPUT" >&2
            continue
        fi

        if ! is_text_file "$FILE_PATH"; then
            echo "Warning: skipping non-text file: $INPUT" >&2
            continue
        fi

        append_file "$FILE_PATH"
    elif [[ -d "$INPUT" ]]; then
        ROOT="$(cd "$INPUT" && pwd)"
        collect_from_dir "$ROOT"
    else
        echo "Error: input is neither a file nor a directory: $INPUT" >&2
        exit 1
    fi
done

echo "Collected $COLLECTED_COUNT file(s) from ${#INPUTS[@]} input(s) into: $OUTPUT"
