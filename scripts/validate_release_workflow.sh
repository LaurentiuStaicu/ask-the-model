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

first_line_after() {
  local after_line="$1"
  local needle="$2"
  local line

  line="$(awk -v after_line="$after_line" -v needle="$needle" '
    NR > after_line && index($0, needle) {
      print NR
      exit
    }
  ' "$workflow")"

  if [[ -z "$line" ]]; then
    printf 'Missing release-workflow contract text after line %s: %s\n' "$after_line" "$needle" >&2
    exit 1
  fi

  printf '%s\n' "$line"
}

tag_preflight_line="$(first_line 'let tagRef = await getTagRefOrNull();')"
create_line="$(first_line 'github.rest.repos.createRelease({')"
draft_line="$(first_line 'draft: true,')"
pre_upload_tag_read_line="$(first_line 'const verifiedTagRef = await getTagRefOrNull();')"
pre_upload_tag_assert_line="$(first_line 'assertReleaseTagMatchesCommit(verifiedTagRef, context.sha, tag);')"
draft_asset_guard_line="$(first_line 'if (release.draft && existingAsset) {')"
draft_asset_assert_line="$(first_line 'assertDraftAssetDigestMatches(')"
upload_guard_line="$(first_line 'if (!existingAsset) {')"
upload_line="$(first_line 'github.rest.repos.uploadReleaseAsset({')"
draft_publish_guard_line="$(first_line 'if (release.draft) {')"
pre_publish_tag_read_line="$(first_line 'const finalTagRef = await getTagRefOrNull();')"
pre_publish_tag_assert_line="$(first_line 'assertReleaseTagMatchesCommit(finalTagRef, context.sha, tag);')"
update_line="$(first_line 'github.rest.repos.updateRelease({')"
publish_line="$(first_line 'draft: false,')"

if ! (( tag_preflight_line < create_line &&
        create_line < draft_line &&
        draft_line < upload_guard_line &&
        upload_guard_line < pre_upload_tag_read_line &&
        pre_upload_tag_read_line < pre_upload_tag_assert_line &&
        pre_upload_tag_assert_line < upload_line &&
        draft_asset_guard_line < draft_asset_assert_line &&
        draft_asset_assert_line < upload_guard_line &&
        draft_asset_assert_line < draft_publish_guard_line &&
        upload_line < draft_publish_guard_line &&
        draft_publish_guard_line < pre_publish_tag_read_line &&
        pre_publish_tag_read_line < pre_publish_tag_assert_line &&
        pre_publish_tag_assert_line < update_line &&
        update_line < publish_line )); then
  printf '%s\n' 'The version tag must be rechecked inside every asset-upload branch (not only draft paths) and again before final draft publication.' >&2
  printf 'lines: preflight=%s create=%s draft=%s upload-branch=%s pre-upload-read=%s pre-upload-assert=%s upload=%s pre-publish-read=%s pre-publish-assert=%s update=%s publish=%s\n' \
    "$tag_preflight_line" "$create_line" "$draft_line" "$upload_guard_line" "$pre_upload_tag_read_line" "$pre_upload_tag_assert_line" \
    "$upload_line" "$pre_publish_tag_read_line" "$pre_publish_tag_assert_line" "$update_line" "$publish_line" >&2
  exit 1
fi

required=(
  'github.paginate('
  'github.rest.repos.listReleases'
  'async function getTagRefOrNull()'
  'const {'
  'classifyReleaseTagState({'
  'assertDraftCanCreateMissingTag'
  'github.rest.repos.compareCommits({'
  'github.rest.git.createRef({'
  "const crypto = require('crypto');"
  'assertDraftAssetDigestMatches('
  "if (disposition === 'published-noop')"
  "if (disposition === 'repair-published')"
  'assertReleaseTagMatchesCommit(verifiedTagRef, context.sha, tag)'
  'assertReleaseTagMatchesCommit(finalTagRef, context.sha, tag)'
  'if (error.status === 404)'
  'throw error;'
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
  'WORKSPACE="$GITHUB_WORKSPACE"'
  'git config --global --add safe.directory "$WORKSPACE"'
  'cp -a "$WORKSPACE/repo" "$PUBLISH_DIR/repo"'
  '"$PUBLISH_DIR/SOURCE_COMMIT"'
  'Source main commit:'
  'git -C "$WORKSPACE" checkout --orphan flatpak-repo-publish'
  'Publish Ask the Model development Flatpak from ${GITHUB_SHA}'
  'git -C "$WORKSPACE" push --force origin HEAD:flatpak-repo'
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
publish_if_line="$(first_line_after "$publish_job_line" "    if: github.event_name == 'push' && github.ref == 'refs/heads/main'")"
publish_needs_line="$(first_line_after "$publish_if_line" '    needs: flatpak')"
publish_write_line="$(first_line_after "$publish_needs_line" '      contents: write')"
download_line="$(first_line_after "$publish_write_line" '      - name: Download verified publication inputs')"

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
  'name: atm-flatpak-publication'
  'include-hidden-files: true'
)

for needle in "${least_privilege[@]}"; do
  if ! grep -Fq -- "$needle" "$workflow"; then
    printf 'Missing Flatpak least-privilege publication guard: %s\n' "$needle" >&2
    exit 1
  fi
done

# Artifact actions must remain pinned to immutable commit SHAs, but the guard
# must not require one historical SHA forever: Dependabot updates the SHA when
# a reviewed action release is bumped. Require the action identity, a full
# 40-hex pin, and an explicit version comment. Accept major-only labels (such
# as the published upload-artifact "v4" label) as well as full x.y.z labels.
for action in 'actions/upload-artifact' 'actions/download-artifact'; do
  if ! grep -Eq "^[[:space:]]*uses: ${action}@[0-9a-f]{40}[[:space:]]+# v[0-9]+(\.[0-9]+){0,2}$" "$workflow"; then
    printf 'Missing fully pinned, version-labelled artifact action: %s\n' "$action" >&2
    exit 1
  fi
done

guard_module="scripts/release_tag_guard.cjs"
guard_tests="tests/release_tag_guard.test.cjs"
[[ -f "$guard_module" ]] || { printf 'Missing release tag guard module: %s\n' "$guard_module" >&2; exit 1; }
[[ -f "$guard_tests" ]] || { printf 'Missing release tag guard tests: %s\n' "$guard_tests" >&2; exit 1; }
for needle in \
  'function classifyReleaseTagState' \
  'function assertDraftAssetDigestMatches' \
  'if (!release)' \
  'already exists without an associated GitHub Release' \
  'release.target_commitish !== currentSha' \
  'release.immutable' \
  "tagRef.object.type !== 'commit'" \
  'tagRef.object.sha !== currentSha' \
  "return 'published-noop'"; do
  if ! grep -Fq -- "$needle" "$guard_module"; then
    printf 'Missing release tag-guard condition: %s\n' "$needle" >&2
    exit 1
  fi
done

printf '%s\n' 'Release workflow contract OK: tag preflight, draft-first publication, source traceability and least-privilege verification/publication separation are enforced.'
