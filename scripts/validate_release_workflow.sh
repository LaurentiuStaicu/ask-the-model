#!/usr/bin/env bash
set -euo pipefail

workflow="${1:-.github/workflows/flatpak.yml}"

if [[ ! -f "$workflow" ]]; then
  printf 'Release workflow not found: %s\n' "$workflow" >&2
  exit 1
fi

first_line() {
  local needle="$1"
  local line

  line="$(grep -nF -- "$needle" "$workflow" | head -n 1 | cut -d: -f1 || true)"
  if [[ -z "$line" ]]; then
    printf 'Missing release-workflow contract text: %s\n' "$needle" >&2
    exit 1
  fi

  printf '%s\n' "$line"
}

create_line="$(first_line 'github.rest.repos.createRelease({')"
draft_line="$(first_line 'draft: true,')"
upload_line="$(first_line 'github.rest.repos.uploadReleaseAsset({')"
update_line="$(first_line 'github.rest.repos.updateRelease({')"
publish_line="$(first_line 'draft: false,')"

if ! (( create_line < draft_line &&
        draft_line < upload_line &&
        upload_line < update_line &&
        update_line < publish_line )); then
  printf '%s\n'     'Release publication order must be create draft -> upload asset -> publish draft.' >&2
  printf 'lines: create=%s draft=%s upload=%s update=%s publish=%s\n'     "$create_line" "$draft_line" "$upload_line" "$update_line" "$publish_line" >&2
  exit 1
fi

required=(
  'github.paginate('
  'github.rest.repos.listReleases'
  'release && release.draft'
  'release.immutable'
  'release.target_commitish !== context.sha'
  'Published GitHub Release'
  'only after all release assets were attached'
)

for needle in "${required[@]}"; do
  if ! grep -Fq -- "$needle" "$workflow"; then
    printf 'Missing release-workflow hardening guard: %s\n' "$needle" >&2
    exit 1
  fi
done

printf '%s\n'   'Release workflow contract OK: draft-first asset publication is enforced.'
