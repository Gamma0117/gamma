#!/usr/bin/env bash
# vcpkg asset source for networks that block GitHub archive tarballs
# (github.com/<owner>/<repo>/archive/<ref>.tar.gz) but allow `git fetch`.
#
# It rebuilds the tarball with `git archive | gzip -n`, which is byte-identical
# to what GitHub serves, so vcpkg still verifies the port's SHA512. Other URLs
# are downloaded as-is.
#
# Usage (see CLAUDE.md, "실행 환경"):
#   export X_VCPKG_ASSET_SOURCES="x-script,$PWD/tools/vcpkg_github_archive.sh {url} {sha512} {dst}"
set -euo pipefail

url="$1"
dst="$3"

if [[ "$url" =~ ^https://github\.com/([^/]+)/([^/]+)/archive/(.+)\.tar\.gz$ ]]; then
    owner="${BASH_REMATCH[1]}"
    repo="${BASH_REMATCH[2]}"
    ref="${BASH_REMATCH[3]}"

    # GitHub names the top-level directory <repo>-<ref>, dropping a leading "v" before a digit.
    name="${ref#refs/tags/}"
    name="${name#refs/heads/}"
    [[ "$name" =~ ^v[0-9] ]] && name="${name#v}"

    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    git init -q --bare "$tmp/src.git"
    git -C "$tmp/src.git" fetch -q --depth 1 "https://github.com/$owner/$repo.git" "$ref"
    git -C "$tmp/src.git" archive --format=tar --prefix="$repo-$name/" FETCH_HEAD | gzip -n > "$dst"
else
    curl -fsSL --retry 3 -o "$dst" "$url"
fi
