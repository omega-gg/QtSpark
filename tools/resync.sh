#!/bin/sh
set -e

#--------------------------------------------------------------------------------------------------
# Resync report: what changed upstream since the pinned Spark version, file by file.
# See doc/resync.md for the procedure.
#--------------------------------------------------------------------------------------------------

if [ $# -lt 2 ]; then

    echo "Usage: resync <spark checkout> <new ref> [--full]"
    echo ""
    echo "Prints, for every tracked upstream file, the diff between the pinned ref and <new ref>"
    echo "and the port files to update. --full prints the diffs, otherwise only the stats."

    exit 1
fi

spark="$1"
ref="$2"
full="$3"

root="$(cd "$(dirname "$0")/.." && pwd)"

pin=$(grep '^ref ' "$root/upstream/spark.pin" | cut -d ' ' -f 2)

#--------------------------------------------------------------------------------------------------
# Refs
#--------------------------------------------------------------------------------------------------

git -C "$spark" rev-parse --verify --quiet "$pin^{commit}" > /dev/null || {

    echo "Pinned ref $pin not found in $spark, run git fetch --tags there."

    exit 1
}

git -C "$spark" rev-parse --verify --quiet "$ref^{commit}" > /dev/null || {

    echo "Ref $ref not found in $spark."

    exit 1
}

echo "Spark resync report: $pin ($(git -C "$spark" rev-parse --short "$pin"))" \
     "to $ref ($(git -C "$spark" rev-parse --short "$ref"))"
echo ""

#--------------------------------------------------------------------------------------------------
# Tracked files
#--------------------------------------------------------------------------------------------------

changed=0

grep -v '^#' "$root/upstream/files.txt" | grep -v '^[[:space:]]*$' | while read -r file ports; do

    if git -C "$spark" diff --quiet "$pin" "$ref" -- "$file"; then
        continue
    fi

    echo "=================================================================================="
    echo "$file"
    echo "  port: $(echo "$ports" | tr ',' ' ')"
    echo ""

    if [ "$full" = "--full" ]; then
        git -C "$spark" --no-pager diff "$pin" "$ref" -- "$file"
    else
        git -C "$spark" --no-pager diff --stat "$pin" "$ref" -- "$file" | sed 's/^/  /'
    fi

    echo ""
done

#--------------------------------------------------------------------------------------------------
# Untracked upstream changes
#--------------------------------------------------------------------------------------------------

echo "=================================================================================="
echo "Upstream files added, deleted or renamed under src/ and rust/ (review if relevant):"
echo ""

git -C "$spark" --no-pager diff --name-status --diff-filter=ADR "$pin" "$ref" -- src rust \
    | sed 's/^/  /'

echo ""
echo "Other changed upstream files that are not tracked (usually out of scope):"
echo ""

tracked=$(grep -v '^#' "$root/upstream/files.txt" | grep -v '^[[:space:]]*$' | cut -d ' ' -f 1)

git -C "$spark" --no-pager diff --name-only --diff-filter=M "$pin" "$ref" -- src rust \
    | while read -r file; do

    echo "$tracked" | grep -qx "$file" || echo "  $file"
done
