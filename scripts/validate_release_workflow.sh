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

development_traceability=(
  '"$PUBLISH_DIR/SOURCE_COMMIT"'
  'Source main commit:'
  'Publish Ask the Model development Flatpak from ${GITHUB_SHA}'
)

for needle in "${development_traceability[@]}"; do
  if ! grep -Fq -- "$needle" "$workflow"; then
    printf 'Missing development-repository source traceability guard: %s\n' "$needle" >&2
    exit 1
  fi
done

verification_job_line="$(first_line '  flatpak:')"
workflow_read_line="$(first_line '  contents: read')"
stage_line="$(first_line '      - name: Stage verified publication inputs')"
publish_job_line="$(first_line '  publish:')"
publish_if_line="$(first_line "    if: github.event_name == 'push' && github.ref == 'refs/heads/main'")"
publish_needs_line="$(first_line '    needs: flatpak')"
publish_write_line="$(first_line '      contents: write')"
download_line="$(first_line '      - name: Download verified publication inputs')"

if ! (( workflow_read_line < verification_job_line &&
        verification_job_line < stage_line &&
        stage_line < publish_job_line &&
        publish_job_line < publish_if_line &&
        publish_if_line < publish_needs_line &&
        publish_needs_line < publish_write_line &&
        publish_write_line < download_line )); then
  printf '%s\n' 'Flatpak workflow privilege split/order is invalid.' >&2
  exit 1
fi

if [[ "$(grep -Fc -- 'contents: write' "$workflow")" -ne 1 ]]; then
  printf '%s\n' 'Flatpak workflow must grant contents: write exactly once, in the publication job.' >&2
  exit 1
fi

least_privilege=(
  'actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02'
  'actions/download-artifact@d3f86a106a0bac45b974a628896c90dbdf5c8093'
  'name: atm-flatpak-publication'
  'include-hidden-files: true'
)

for needle in "${least_privilege[@]}"; do
  if ! grep -Fq -- "$needle" "$workflow"; then
    printf 'Missing Flatpak least-privilege publication guard: %s\n' "$needle" >&2
    exit 1
  fi
done

printf '%s\n' 'Release workflow contract OK: draft-first publication, source traceability and least-privilege verification/publication separation are enforced.'
