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
require_text STATUS.md 'v0.3.0 is the first public repository-aware release for the fixed EWD/CBD/RMD scientific dynamical-model suite.'
require_text STATUS.md 'repository and local-AI model identity frozen on the first Send for each chat;'
require_text STATUS.md 'current-turn temporary `[S#]` source-label resolution before commit;'
require_text STATUS.md 'fail-closed handling of unknown source labels;'
require_text STATUS.md 'compact user-visible numbered sources with repository/version/SHA/logical-source/locator/excerpt provenance'
require_text STATUS.md 'AtM does not bundle, install, start, stop or update the local AI provider.'
require_text STATUS.md 'AI models remain provider-managed artifacts. AtM does not download, import, move, update or delete AI model files.'
require_text STATUS.md 'Repository evidence can ground an AI response, but AtM does not present AI-generated interpretation as a canonical model result.'
require_text STATUS.md "## Not implemented in v$version"
require_text STATUS.md 'conversation persistence across application restarts;'
require_text STATUS.md 'arbitrary repository support beyond EWD/CBD/RMD;'
require_text STATUS.md 'execution or simulation of scientific models;'

printf 'AtM README contract PASS: version %s, visual shell, capability boundaries and documentation routes are consistent.\n' "$version"
