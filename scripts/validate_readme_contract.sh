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

require_regex() {
  local file="$1"
  local pattern="$2"
  grep -Eq -- "$pattern" "$file" || fail "$file does not match required pattern: $pattern"
}

for path in   README.md   meson.build   CITATION.cff   STATUS.md   LICENSE   docs/MODEL_GUIDE.md   docs/TROUBLESHOOTING.md   docs/DEPENDENCIES_AND_COMPATIBILITY.md   .github/CONTRIBUTING.md   .github/SUPPORT.md
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
require_text README.md 'img alt="Latest release"'
require_text README.md 'img alt="MIT License"'
require_text README.md 'img alt="Linux / Flatpak"'
require_text README.md 'img alt="Download Flatpak"'
require_text README.md 'img alt="Getting started"'
require_text README.md 'img alt="Model guide"'

header="$(sed -n '1,24p' README.md)"
if grep -Fq 'blue?style=' <<<"$header"; then
  fail "header uses a non-suite blue badge"
fi
for colour in 333333 707070 a0a0a0; do
  grep -Fqi -- "color=$colour" <<<"$header" || grep -Fqi -- "-$colour?style=flat-square" <<<"$header" || fail "header is missing suite grayscale token $colour"
done

require_text README.md 'It is deliberately separate from the software that actually runs the model and from the model files themselves.'
require_text README.md 'must currently be downloaded and managed outside AtM.'
require_text README.md 'does <strong>not</strong> download, import, move or delete AI models for you.'
require_text README.md 'AtM does <strong>not</strong> search every SSD, HDD or folder'
require_text README.md 'AtM asks the provider for the installed model list through <code>GET /api/tags</code>.'
require_text README.md 'Only models advertising the <code>completion</code> capability are placed in the AtM model selector.'
require_text README.md "AtM v$version currently provides text chat only"
require_text README.md 'ingest or retrieve scientific repositories;'
require_text README.md 'show repository-grounded citations or provenance;'
require_text README.md 'turn ordinary AI chat output into an authoritative scientific-model result.'
require_text README.md 'Each source repository remains canonical for its documentation, code, data, assumptions, provenance, validation and release boundaries.'
require_text README.md "Ordinary local chat in v$version should not be interpreted as repository-grounded scientific analysis."

require_text README.md 'href="docs/MODEL_GUIDE.md"'
require_text README.md 'href="docs/TROUBLESHOOTING.md"'
require_text README.md 'href="docs/DEPENDENCIES_AND_COMPATIBILITY.md"'
require_text README.md 'href="STATUS.md"'
require_text README.md 'href=".github/SUPPORT.md"'
require_text README.md 'href=".github/CONTRIBUTING.md"'
require_text README.md 'href="LICENSE"'
require_text README.md 'href="CITATION.cff"'

require_text STATUS.md 'Repository-aware retrieval and scientific provenance are not implemented in this release.'
require_text STATUS.md 'AtM does not bundle, install, start, stop or update the local provider.'
require_text STATUS.md 'AtM must not present an AI-generated explanation as if it were a canonical model result'
require_text STATUS.md 'repository ingestion or retrieval;'
require_text STATUS.md 'model installation, download or deletion;'

printf 'AtM README contract PASS: version %s, visual shell, capability boundaries and documentation routes are consistent.\n' "$version"
