#!/usr/bin/env bash
#
# Build signed Debian *source* packages of cwsd and upload them to a Launchpad
# PPA, which then builds the cwsd binary for each series.
#
# Usage:
#   packaging/publish-ppa.sh [--no-upload] [--series "noble jammy"] [--rev N]
#
#   --no-upload   build + sign the source packages but don't dput them
#   --series ...  space-separated Ubuntu series (overrides SERIES below)
#   --rev N       per-series revision suffix (bump to re-upload the same version)
#
# Prerequisites (install once):
#   sudo apt install devscripts debhelper dput
# and a GPG key whose UID matches DEBEMAIL, registered on your Launchpad account.
#
set -euo pipefail

PPA="ppa:benishor/hamtools"
DEBFULLNAME="Adrian Scripca"
DEBEMAIL="benishor@gmail.com"
KEYID="18B97354B106F3841ADEC0CF85BED1A01D653065"  # sign by fingerprint
SERIES=(noble resolute)       # Ubuntu series to publish for
REV=1                          # per-series revision; bump to re-upload a version
export DEBFULLNAME DEBEMAIL

UPLOAD=1
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-upload) UPLOAD=0; shift ;;
        --series)    read -r -a SERIES <<< "$2"; shift 2 ;;
        --rev)       REV="$2"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

REPO="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
cd "$REPO"

need=()
for t in dpkg-buildpackage dpkg-source debsign git; do
    command -v "$t" >/dev/null || need+=("$t")
done
command -v dh >/dev/null || need+=("debhelper (dh)")
(( UPLOAD )) && { command -v dput >/dev/null || need+=("dput"); }
if (( ${#need[@]} )); then
    echo "Missing tools: ${need[*]}" >&2
    echo "Install with: sudo apt install devscripts debhelper dput" >&2
    exit 1
fi

# cwsd versions from git tags (vX.Y.Z); there is no project(VERSION) in CMake.
BASEVER="$(git describe --tags --abbrev=0 2>/dev/null | sed 's/^v//')"
[[ "$BASEVER" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || {
    echo "could not derive X.Y.Z from the latest git tag (got '$BASEVER')" >&2; exit 1; }

if ! git diff --quiet HEAD -- debian packaging CMakeLists.txt shared; then
    echo "WARNING: uncommitted changes to debian/, packaging/, CMakeLists.txt or" >&2
    echo "         shared/; the upload reflects HEAD (committed state)." >&2
fi

OUT="$REPO/dist"
mkdir -p "$OUT"
echo "Publishing cwsd $BASEVER to $PPA for: ${SERIES[*]}"

for series in "${SERIES[@]}"; do
    ver="${BASEVER}~${series}${REV}"
    work="$(mktemp -d)"
    trap 'rm -rf "$work"' EXIT

    # Clean, deterministic export of the committed tree (no build/, .git, …).
    git archive --format=tar HEAD | tar -x -C "$work"

    cat > "$work/debian/changelog" <<EOF
cwsd ($ver) $series; urgency=medium

  * Build for $series.

 -- $DEBFULLNAME <$DEBEMAIL>  $(date -R)
EOF

    echo "==> $series: building source package cwsd_${ver}"
    ( cd "$work"
      dpkg-buildpackage -S -d -us -uc
      debsign ${KEYID:+-k"$KEYID"} "../cwsd_${ver}_source.changes"
    )

    cp "$work"/../cwsd_"${ver}"* "$OUT"/ 2>/dev/null || true
    changes="$OUT/cwsd_${ver}_source.changes"

    if (( UPLOAD )); then
        echo "==> $series: dput -> $PPA"
        dput "$PPA" "$changes"
    else
        echo "==> $series: built $changes (upload skipped)"
    fi

    rm -rf "$work"; trap - EXIT
done

echo "Done. Artifacts in $OUT/"
(( UPLOAD )) && echo "Watch the builds at https://launchpad.net/~benishor/+archive/ubuntu/hamtools"
