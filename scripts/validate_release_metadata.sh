#!/usr/bin/env bash
set -euo pipefail

fail() {
  printf 'AtM release metadata FAIL: %s\n' "$1" >&2
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

metainfo="data/io.github.laurentiustaicu.ask_the_model.metainfo.xml"
desktop="data/io.github.laurentiustaicu.ask_the_model.desktop"
contract=".github/release_metadata_contract.json"

for path in \
  meson.build \
  CITATION.cff \
  README.md \
  STATUS.md \
  CHANGELOG.md \
  "$metainfo" \
  "$desktop" \
  "$contract"
do
  require_file "$path"
done

version="$(sed -n "s/^[[:space:]]*version:[[:space:]]*'\([^']*\)'.*/\1/p" meson.build | head -n1)"
[[ -n "$version" ]] || fail "could not read application version from meson.build"

release_date="$(
  sed -n 's/^[[:space:]]*date-released:[[:space:]]*//p' CITATION.cff |
    head -n1 |
    tr -d '"' |
    tr -d "'" |
    xargs
)"
[[ -n "$release_date" ]] || fail "could not read date-released from CITATION.cff"
[[ "$release_date" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]] ||
  fail "CITATION.cff date-released is not YYYY-MM-DD: $release_date"

require_regex CITATION.cff "^[[:space:]]*version:[[:space:]]*\"?${version}\"?[[:space:]]*$"
require_regex CITATION.cff "^[[:space:]]*date-released:[[:space:]]*\"?${release_date}\"?[[:space:]]*$"

require_text README.md "img alt=\"Version: $version\""
require_text STATUS.md "Current release: Ask the Model (AtM) v$version"
require_text STATUS.md "released $release_date."
require_text CHANGELOG.md "## $version - $release_date"

release_file="releases/v$version.md"
require_file "$release_file"
require_text "$release_file" "# Ask the Model v$version"
require_text "$release_file" "$release_date"

first_appstream_release="$(grep -m1 -E '<release[[:space:]]+version=' "$metainfo" || true)"
[[ -n "$first_appstream_release" ]] ||
  fail "AppStream metadata has no release entry"
[[ "$first_appstream_release" == *"version=\"$version\""* ]] ||
  fail "first AppStream release does not match version $version: $first_appstream_release"
[[ "$first_appstream_release" == *"date=\"$release_date\""* ]] ||
  fail "first AppStream release does not match date $release_date: $first_appstream_release"

require_text "$contract" "\"version\": \"$version\""
require_text "$contract" "\"date\": \"$release_date\""

# Desktop Entry Version=1.0 is the desktop-file specification version,
# not the Ask the Model application version. Keep that distinction explicit.
require_regex "$desktop" '^Version=1\.0$'

printf 'AtM release metadata PASS: version %s and date %s are atomic across public metadata.\n' \
  "$version" "$release_date"
