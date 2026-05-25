#!/usr/bin/env bash
# Build + publish a new firmware release to GitHub.
#
#   ./release.sh <version> [release notes...]
#
# Steps:
#   1. Sanity-check version + working tree clean (no uncommitted changes)
#   2. Bump FW_VERSION in config.h to <version>
#   3. arduino-cli compile fresh binary
#   4. Compute SHA-256 of the binary
#   5. Create GitHub Release tagged sonos-eth-p4-v<version>, attach the .bin
#   6. Rewrite manifest.json to point at the new release asset
#   7. Commit version bump + manifest, tag, push
#
# Boards on the network will pick up the new version within ~6h (or now if you
# hit "check now" in their web UI / curl /api/checkupdate).

set -euo pipefail
cd "$(dirname "$0")"

FQBN=esp32:esp32:esp32p4
REPO_OWNER=davidvivesprice
REPO_NAME=arduino-projects
TAG_PREFIX=sonos-eth-p4-v

usage() {
  echo "usage: ./release.sh <version> [release notes...]"
  echo "       version must be semver: X.Y.Z (e.g. 1.0.1)"
  exit 1
}

[[ $# -lt 1 ]] && usage
VER="$1"; shift || true
NOTES="${*:-Routine update.}"

# ── Sanity checks ─────────────────────────────────────────────────────────
[[ "$VER" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "invalid version '$VER' — must be X.Y.Z"; exit 1; }

TAG="${TAG_PREFIX}${VER}"
ASSET="SonosEthRemoteP4-v${VER}.bin"

# Refuse to release with uncommitted changes — the manifest + version bump
# need to be the only change in the release commit, for clean rollback later.
if [[ -n "$(git status --porcelain config.h manifest.json 2>/dev/null)" ]]; then
  echo "config.h or manifest.json has uncommitted changes — commit them first or stash"
  exit 1
fi

# Refuse to overwrite an existing tag.
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
  echo "tag $TAG already exists locally — pick a new version"
  exit 1
fi
if gh release view "$TAG" --repo "$REPO_OWNER/$REPO_NAME" >/dev/null 2>&1; then
  echo "release $TAG already exists on GitHub — pick a new version"
  exit 1
fi

echo "==> releasing v$VER (tag $TAG)"

# ── Bump version in config.h ──────────────────────────────────────────────
echo "==> bumping FW_VERSION in config.h"
# Edit constexpr char FW_VERSION[] = "X.Y.Z";  →  new version
sed -i.bak -E "s|(constexpr char FW_VERSION\[\] = \")[0-9]+\.[0-9]+\.[0-9]+(\";)|\1${VER}\2|" config.h
rm config.h.bak
grep "FW_VERSION" config.h | head -1

# ── Compile fresh ─────────────────────────────────────────────────────────
echo "==> compiling (clean build to avoid stale objects)"
arduino-cli compile --clean --fqbn $FQBN --output-dir ./build .

BIN=./build/SonosEthRemoteP4.ino.bin
[[ -f "$BIN" ]] || { echo "build output not found at $BIN"; exit 2; }

# ── Hash + rename for upload ──────────────────────────────────────────────
SHA=$(shasum -a 256 "$BIN" | awk '{print $1}')
SIZE=$(stat -f%z "$BIN")
echo "==> binary: ${SIZE} bytes  sha256=${SHA}"

cp "$BIN" "/tmp/${ASSET}"

# ── Rewrite manifest.json ─────────────────────────────────────────────────
ASSET_URL="https://github.com/${REPO_OWNER}/${REPO_NAME}/releases/download/${TAG}/${ASSET}"
echo "==> rewriting manifest.json"
cat > manifest.json <<EOF
{
  "version": "${VER}",
  "url": "${ASSET_URL}",
  "sha256": "${SHA}",
  "size": ${SIZE},
  "released": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "notes": "${NOTES//\"/\\\"}"
}
EOF

# ── Commit + tag + push ───────────────────────────────────────────────────
echo "==> committing version bump"
git add config.h manifest.json
git commit -m "SonosEthRemoteP4 v${VER}

${NOTES}"
git tag "$TAG"
git push
git push --tags

# ── Create GitHub Release with the binary attached ────────────────────────
echo "==> creating GitHub release"
gh release create "$TAG" \
  --repo "${REPO_OWNER}/${REPO_NAME}" \
  --title "Sonos ETH P4 v${VER}" \
  --notes "${NOTES}

\`\`\`
sha256: ${SHA}
size:   ${SIZE} bytes
\`\`\`" \
  "/tmp/${ASSET}"

rm -f "/tmp/${ASSET}"

echo
echo "✓ Release v${VER} published"
echo "  manifest: https://raw.githubusercontent.com/${REPO_OWNER}/${REPO_NAME}/main/projects/SonosEthRemoteP4/manifest.json"
echo "  asset:    ${ASSET_URL}"
echo
echo "Boards will auto-update within ~6h. To trigger immediately on one board:"
echo "  curl http://tpsvc-<room>.local/api/checkupdate"
echo
echo "Or update all 7 in parallel:"
echo "  for r in mstbed mstbth grkit grliv fam bed2 bed3; do"
echo "    curl -s http://tpsvc-\$r.local/api/checkupdate &"
echo "  done; wait"
