#!/bin/sh
# Release a version of charts:
#   tools/release.sh <x.y.z> ["summary"]
# On a clean, up-to-date main: bumps the version (include/charts.h,
# app/cli.c, docs/charts.1), checks it is below 1.0 and above the last one,
# runs make test, commits "version x.y.z: <summary>", tags vx.y.z with the
# same message and pushes main and the tag together. The tag push starts the
# release workflow, which builds every target and publishes the GitHub
# Release. The summary defaults to the subject of the last commit.
# RELEASE_TRAILERS, if set, is appended to the message as its last paragraph.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
die() { echo "release: $*" >&2; exit 1; }

NEW=${1:-}
[ -n "$NEW" ] || die "usage: tools/release.sh <x.y.z> [\"summary\"]"
echo "$NEW" | grep -Eq '^0\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$' ||
    die "$NEW is not 0.y.z: charts stays below 1.0 until the owner certifies 1.0"
TAG=v$NEW

[ "$(git symbolic-ref --short HEAD 2>/dev/null)" = main ] || die "not on main"
[ -z "$(git status --porcelain)" ] || die "the tree is not clean (git status)"
git fetch --quiet --tags origin main
git merge-base --is-ancestor origin/main HEAD || die "main is behind or apart from origin/main: pull --ff-only first"
git rev-parse -q --verify "refs/tags/$TAG" >/dev/null && die "tag $TAG exists"
git ls-remote --exit-code --tags origin "refs/tags/$TAG" >/dev/null 2>&1 && die "tag $TAG exists on origin"

OLD=$(sed -n 's/^#define LC_VERSION "\(.*\)"$/\1/p' include/charts.h)
[ -n "$OLD" ] || die "no LC_VERSION in include/charts.h"
[ "$OLD" != "$NEW" ] || die "the version is already $NEW"
[ "$(printf '%s\n%s\n' "$OLD" "$NEW" | sort -t. -k1,1n -k2,2n -k3,3n | tail -1)" = "$NEW" ] ||
    die "$NEW is not above $OLD"

SUMMARY=${2:-$(git log -1 --format=%s)}
PREV=$(git describe --tags --abbrev=0 --match 'v[0-9]*' 2>/dev/null || true)

sed -i.bak "s/^#define LC_VERSION \"$OLD\"\$/#define LC_VERSION \"$NEW\"/" include/charts.h
sed -i.bak "s/fputs(\"charts $OLD\\\\n\",f)/fputs(\"charts $NEW\\\\n\",f)/" app/cli.c
sed -i.bak "1s/\"charts $OLD\"/\"charts $NEW\"/" docs/charts.1
rm -f include/charts.h.bak app/cli.c.bak docs/charts.1.bak
grep -q "LC_VERSION \"$NEW\"" include/charts.h || die "bump failed: include/charts.h"
grep -q "charts $NEW\\\\n" app/cli.c || die "bump failed: app/cli.c"
grep -q "\"charts $NEW\"" docs/charts.1 || die "bump failed: docs/charts.1"

restore() { git checkout -- include/charts.h app/cli.c docs/charts.1; }
if ! { make && make test; }; then restore; die "make test failed; version left at $OLD"; fi
[ "$(./charts --version)" = "charts $NEW" ] || { restore; die "./charts --version does not say $NEW"; }

MSG=$(mktemp "${TMPDIR:-/tmp}/charts-release.XXXXXX")
trap 'rm -f "$MSG"' EXIT HUP INT TERM
{
    echo "version $NEW: $SUMMARY"
    echo
    if [ -n "$PREV" ]; then
        echo "Changes since $PREV:"
        git log --reverse --no-merges --format='- %s' "$PREV..HEAD"
    else
        echo "Changes:"
        git log -1 --format='- %s'
    fi
    if [ -n "${RELEASE_TRAILERS:-}" ]; then echo; printf '%s\n' "$RELEASE_TRAILERS"; fi
} > "$MSG"

git add include/charts.h app/cli.c docs/charts.1
git commit -q -F "$MSG"
git tag -a "$TAG" -F "$MSG"
git push --atomic origin main "$TAG"
echo "released $TAG: the release workflow is building it"
command -v gh >/dev/null 2>&1 && echo "watch: gh run list --workflow release.yml --limit 1" || true
