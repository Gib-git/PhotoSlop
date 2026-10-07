#!/usr/bin/env bash
# Raises the version in VERSION, turns the Unreleased changelog section into a dated
# release section, commits, and tags the commit. Pushing the tag starts the Release workflow.
#
#   scripts/bump-version.sh patch|minor|major|X.Y.Z
set -euo pipefail

cd "$(dirname "$0")/.."

if [ $# -ne 1 ]; then
    echo "usage: $0 patch|minor|major|X.Y.Z" >&2
    exit 1
fi

if [ -n "$(git status --porcelain)" ]; then
    echo "The working tree has uncommitted changes. Commit or stash them first." >&2
    exit 1
fi

current=$(tr -d '[:space:]' < VERSION)
IFS=. read -r major minor patch <<< "$current"

case "$1" in
    major) next="$((major + 1)).0.0" ;;
    minor) next="$major.$((minor + 1)).0" ;;
    patch) next="$major.$minor.$((patch + 1))" ;;
    [0-9]*.[0-9]*.[0-9]*) next="$1" ;;
    *)
        echo "usage: $0 patch|minor|major|X.Y.Z" >&2
        exit 1
        ;;
esac

if git rev-parse -q --verify "refs/tags/v$next" > /dev/null; then
    echo "Tag v$next already exists." >&2
    exit 1
fi

# The Unreleased section must have notes; they become the GitHub release notes.
notes=$(awk '/^## \[Unreleased\]/ { on = 1; next } /^## / { on = 0 } on && NF' CHANGELOG.md)
if [ -z "$notes" ]; then
    echo "CHANGELOG.md has nothing under ## [Unreleased]. Add release notes there first." >&2
    exit 1
fi

echo "$next" > VERSION
today=$(date +%Y-%m-%d)
awk -v heading="## [$next] - $today" '
    /^## \[Unreleased\]/ { print; print ""; print heading; next }
    { print }
' CHANGELOG.md > CHANGELOG.md.tmp
mv CHANGELOG.md.tmp CHANGELOG.md

git add VERSION CHANGELOG.md
git commit -q -m "Release v$next"
git tag -a "v$next" -m "PhotoSlop $next"

echo "Bumped $current -> $next and tagged v$next."
echo "Publish it with: git push --follow-tags"
