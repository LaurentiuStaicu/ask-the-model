'use strict';

function assertReleaseTagMatchesCommit(tagRef, currentSha, tag) {
  if (!/^[0-9a-f]{40}$/.test(currentSha || '')) {
    throw new Error('Release tag preflight requires the exact 40-hex workflow commit SHA.');
  }

  if (!tagRef || !tagRef.object) {
    throw new Error(
      `Tag ${tag} does not exist; refusing to attach or publish a release without a verified tag ref.`
    );
  }

  if (
    tagRef.object.type !== 'commit' ||
    tagRef.object.sha !== currentSha
  ) {
    const observed = tagRef.object
      ? `${tagRef.object.type || 'unknown'} ${tagRef.object.sha || 'unknown'}`
      : 'missing object';
    throw new Error(
      `Tag ${tag} must resolve directly to workflow commit ${currentSha}; found ${observed}.`
    );
  }
}


function assertDraftCanCreateMissingTag(release, currentSha, tag) {
  if (!/^[0-9a-f]{40}$/.test(currentSha || '')) {
    throw new Error('Release tag preflight requires the exact 40-hex workflow commit SHA.');
  }

  if (!release || !release.draft || release.tag_name !== tag) {
    throw new Error(
      `Only the matching draft release can create missing tag ${tag}.`
    );
  }

  if (release.target_commitish !== currentSha) {
    throw new Error(
      `Draft release ${tag} must target exact workflow commit ${currentSha} before creating its missing tag.`
    );
  }

  if (!Array.isArray(release.assets) || release.assets.length !== 0) {
    throw new Error(
      `Draft release ${tag} must have no assets before creating its missing tag.`
    );
  }
}

function classifyReleaseTagState({ release, tagRef, currentSha, assetName }) {
  if (!/^[0-9a-f]{40}$/.test(currentSha || '')) {
    throw new Error('Release tag preflight requires the exact 40-hex workflow commit SHA.');
  }

  const hasPublishedAsset = Boolean(
    release &&
    !release.draft &&
    Array.isArray(release.assets) &&
    release.assets.some((asset) => asset.name === assetName)
  );

  // Preserve the published-release idempotency path. Never mutate an
  // already-published release that already contains the expected asset.
  if (hasPublishedAsset) {
    return 'published-noop';
  }

  if (!release) {
    if (tagRef) {
      const observed = tagRef.object
        ? `${tagRef.object.type || 'unknown'} ${tagRef.object.sha || 'unknown'}`
        : 'missing object';
      throw new Error(
        `Tag ${tagRef.ref || 'for this version'} already exists without an associated GitHub Release (${observed}); refusing to reuse a pre-existing version tag.`
      );
    }
    return 'create-draft';
  }

  if (release.draft) {
    if (!tagRef) {
      assertDraftCanCreateMissingTag(release, currentSha, release.tag_name);
      return 'resume-draft-create-tag';
    }
    assertReleaseTagMatchesCommit(tagRef, currentSha, release.tag_name);
    if (release.target_commitish && release.target_commitish !== currentSha) {
      throw new Error(
        `Draft release ${release.tag_name} targets ${release.target_commitish}, not workflow commit ${currentSha}.`
      );
    }
    return 'resume-draft';
  }

  // A mutable legacy published release missing its bundle may be repaired
  // only when its existing tag identifies this exact workflow commit.
  assertReleaseTagMatchesCommit(tagRef, currentSha, release.tag_name);
  if (release.immutable) {
    throw new Error(
      `Immutable release ${release.tag_name} is missing ${assetName}; refusing an impossible post-publication repair.`
    );
  }
  return 'repair-published';
}


function assertDraftAssetDigestMatches(asset, expectedDigest, assetName) {
  if (!/^sha256:[0-9a-f]{64}$/.test(expectedDigest || '')) {
    throw new Error('Expected release asset digest must be a canonical SHA-256 digest.');
  }

  if (
    !asset ||
    asset.name !== assetName ||
    !/^sha256:[0-9a-f]{64}$/.test(asset.digest || '')
  ) {
    throw new Error(
      `Draft release asset ${assetName} has no verifiable SHA-256 digest; refusing to publish an unverifiable bundle. Remove the draft asset and rerun the workflow.`
    );
  }

  if (asset.digest !== expectedDigest) {
    throw new Error(
      `Draft release asset ${assetName} digest does not match the current verified build; refusing publication. Remove the draft asset and rerun the workflow to upload the checked bundle.`
    );
  }
}

module.exports = {
  assertReleaseTagMatchesCommit,
  assertDraftCanCreateMissingTag,
  assertDraftAssetDigestMatches,
  classifyReleaseTagState,
};
