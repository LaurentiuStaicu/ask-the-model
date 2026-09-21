#!/usr/bin/env bash
set -euo pipefail

fail() {
  printf 'AtM README contract FAIL: %s\n' "$1" >&2
  exit 1
}

require_file() {
  [[ -f "$1" ]] || fail "missing required file: $1"
}

require_text() {
  local file="$1"
  local needle="$2"
  grep -Fq -- "$needle" "$file" || fail "$file is missing required text: $needle"
}

for path in \
  README.md \
  meson.build \
  CITATION.cff \
  STATUS.md \
  LICENSE \
  assets/application-map-v0.3.svg \
  docs/USER_INTERFACE_GUIDE.md \
  docs/DEVELOPMENT_GUIDE.md \
  docs/MODEL_GUIDE.md \
  docs/TROUBLESHOOTING.md \
  docs/DEPENDENCIES_AND_COMPATIBILITY.md \
  docs/REPOSITORY_RETRIEVAL_ARCHITECTURE.md \
  docs/REPOSITORY_RETRIEVAL_ACCEPTANCE.md \
  .github/CONTRIBUTING.md \
  .github/SUPPORT.md
do
  require_file "$path"
done

version="$(sed -n "s/^[[:space:]]*version:[[:space:]]*'\([^']*\)'.*/\1/p" meson.build | head -n1)"
[[ -n "$version" ]] || fail "could not read project version from meson.build"

require_text CITATION.cff "version: $version"
require_text STATUS.md "Current release: Ask the Model (AtM) v$version"
require_file "releases/v$version.md"
require_text "releases/v$version.md" "# Ask the Model v$version"
require_text README.md "AtM v$version"

require_text README.md '<img src="assets/icon.png" alt="Ask the Model icon" width="112">'
require_text README.md '<h2 align="center">Ask the Model (AtM)</h2>'
require_text README.md "img alt=\"Version: $version\""
require_text README.md 'img alt="MIT License"'
require_text README.md 'img alt="Linux / Flatpak"'
require_text README.md 'img alt="Download Flatpak"'
require_text README.md 'img alt="Getting started"'
require_text README.md 'img alt="Interface guide"'
require_text README.md 'img alt="Developer guide"'

header="$(sed -n '1,26p' README.md)"
if grep -Fq 'blue?style=' <<<"$header"; then
  fail "header uses a non-suite blue badge"
fi
for colour in 333333 707070 a0a0a0; do
  grep -Fqi -- "color=$colour" <<<"$header" || \
    grep -Fqi -- "-$colour?style=flat-square" <<<"$header" || \
    fail "header is missing suite grayscale token $colour"
done

require_text README.md 'A local-first desktop interface for local AI chat and repository-grounded exploration'
require_text README.md 'must currently be downloaded and managed through the provider rather than AtM.'
require_text README.md 'AtM does <strong>not</strong> search every SSD, HDD or folder'
require_text README.md 'AtM asks the provider for the installed model list through <code>GET /api/tags</code>.'
require_text README.md 'Only models advertising the <code>completion</code> capability are placed in the AtM model selector.'
require_text README.md "AtM v$version currently provides text chat and repository-grounded text retrieval"
require_text README.md 'fixed EWD/CBD/RMD repository catalog'
require_text README.md 'resolves repository Refresh to exact Git SHAs'
require_text README.md 'stores immutable validated snapshots under <code>~/Ask the Model/Repositories</code>'
require_text README.md 'pins AI-model and repository snapshot identity per chat on the first Send'
require_text README.md 'shows compact numbered citations with exact repository/version/SHA/source/locator provenance'
require_text README.md 'READY is not a scientific certification'
require_text README.md 'turn generated text into an authoritative scientific-model result without repository-grounded provenance or actual model execution.'

require_text README.md 'href="docs/USER_INTERFACE_GUIDE.md"'
require_text README.md 'href="docs/DEVELOPMENT_GUIDE.md"'
require_text README.md 'href="docs/MODEL_GUIDE.md"'
require_text README.md 'href="docs/TROUBLESHOOTING.md"'
require_text README.md 'href="docs/DEPENDENCIES_AND_COMPATIBILITY.md"'
require_text README.md 'href="docs/REPOSITORY_RETRIEVAL_ARCHITECTURE.md"'
require_text README.md 'href="docs/REPOSITORY_RETRIEVAL_ACCEPTANCE.md"'
require_text README.md 'href="STATUS.md"'
require_text README.md 'href=".github/SUPPORT.md"'
require_text README.md 'href=".github/CONTRIBUTING.md"'
require_text README.md 'href="LICENSE"'
require_text README.md 'href="CITATION.cff"'

require_text STATUS.md 'validated immutable EWD/CBD/RMD snapshots'
require_text STATUS.md 'exact remote SHA/version Refresh'
require_text STATUS.md 'compact numbered source references'
require_text STATUS.md 'source-detail windows exposing repository/version, exact snapshot SHA'
require_text STATUS.md 'The first Send freezes:'
require_text STATUS.md 'Repository text is treated as untrusted data'
require_text STATUS.md 'The Flatpak receives write access only to the dedicated `~/Ask the Model` directory'
require_text STATUS.md 'AtM does not bundle, install, start, stop or update the local AI provider.'
require_text STATUS.md 'scientific certification of a repository merely because it is READY'
require_text STATUS.md 'in-app AI-model download/import/delete'
require_text STATUS.md 'execution or simulation of EWD, CBD or RMD'

printf 'AtM README contract PASS: version %s, visual shell, released repository-grounded capability boundaries and documentation routes are consistent.\n' "$version"
