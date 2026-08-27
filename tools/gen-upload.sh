#!/usr/bin/env bash
set -euo pipefail

# Get-ChildItem -Recurse -File -Include *.cpp,*.h,*.hpp,*.c,README*,CMakeLists.txt,*.conf,*.monitrc,*-agentd |
# >> Where-Object { $_.FullName -notmatch '[\\/]cmake-build-debug-remote-host([\\/]|$)' } |
# >> ForEach-Object {
# >>     "######## file: $($_.FullName)`n``````cpp`n$([System.IO.File]::ReadAllText($_.FullName))`n```````n"
# >> } | Out-File -Encoding utf8 upload.txt

output_file="${1:-upload.txt}"

detect_lang() {
    local path="$1"
    local base ext lower

    base="$(basename "$path")"
    lower="$(printf '%s' "$base" | tr '[:upper:]' '[:lower:]')"

    case "$lower" in
        cmakelists.txt) echo "cmake" ;;
        readme|readme.*)
            case "$lower" in
                *.md) echo "md" ;;
                *.rst) echo "rst" ;;
                *.txt) echo "text" ;;
                *) echo "md" ;;
            esac
            ;;
        *.conf) echo "conf" ;;
        *.monitrc) echo "conf" ;;
        *-agentd) echo "text" ;;
        *)
            ext="${lower##*.}"
            case "$ext" in
                c|h|hpp|hh|hxx|cc|cpp|cxx) echo "cpp" ;;
                cmake) echo "cmake" ;;
                md|markdown) echo "md" ;;
                rst) echo "rst" ;;
                txt) echo "text" ;;
                sh|bash) echo "bash" ;;
                py) echo "python" ;;
                json) echo "json" ;;
                yaml|yml) echo "yaml" ;;
                xml) echo "xml" ;;
                ini) echo "ini" ;;
                service) echo "ini" ;;
                *) echo "text" ;;
            esac
            ;;
    esac
}

matches_name() {
    local name lower
    name="$(basename "$1")"
    lower="$(printf '%s' "$name" | tr '[:upper:]' '[:lower:]')"

    case "$lower" in
        *.cpp|*.h|*.hpp|*.c|readme*|cmakelists.txt|*.conf|*.monitrc|*-agentd)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

: > "$output_file"

find . -type f \
    ! -path '*/cmake-build-debug-remote-host/*' \
    -print0 |
while IFS= read -r -d '' file; do
    if ! matches_name "$file"; then
        continue
    fi

    lang="$(detect_lang "$file")"

    {
        printf '######## file: %s\n' "$file"
        printf '```%s\n' "$lang"
        cat "$file"
        printf '\n```\n\n'
    } >> "$output_file"
done
