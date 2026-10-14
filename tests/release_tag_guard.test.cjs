'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');

const {
  assertReleaseTagMatchesCommit,
  classifyReleaseTagState,
} = require('../scripts/release_tag_guard.cjs');

const SHA_A = 'a'.repeat(40);
const SHA_B = 'b'.repeat(40);
const TAG = 'v0.7.0';
const ASSET = 'AskTheModel.flatpak';

function ref(sha, type = 'commit') {
  return {
    ref: `refs/tags/${TAG}`,
    object: { type, sha },
  };
}

function draftRelease(overrides = {}) {
  return {
    tag_name: TAG,
    draft: true,
    target_commitish: SHA_A,
    assets: [],
    ...overrides,
  };
}

function publishedRelease(overrides = {}) {
  return {
    tag_name: TAG,
    draft: false,
    target_commitish: SHA_A,
    immutable: false,
    assets: [],
    ...overrides,
  };
}

test('new release is allowed only when the version tag is absent', () => {
  assert.equal(
    classifyReleaseTagState({
      release: undefined,
      tagRef: null,
      currentSha: SHA_A,
      assetName: ASSET,
    }),
    'create-draft'
  );
});

test('pre-existing tag without a GitHub Release fails closed even if it points at this commit', () => {
  assert.throws(
    () => classifyReleaseTagState({
      release: undefined,
      tagRef: ref(SHA_A),
      currentSha: SHA_A,
      assetName: ASSET,
    }),
    /already exists without an associated GitHub Release/
  );
});

test('draft release cannot resume if its tag ref is absent', () => {
  assert.throws(
    () => classifyReleaseTagState({
      release: draftRelease(),
      tagRef: null,
      currentSha: SHA_A,
      assetName: ASSET,
    }),
    /does not exist/
  );
});

test('draft release cannot resume if the actual tag points at another commit', () => {
  assert.throws(
    () => classifyReleaseTagState({
      release: draftRelease(),
      tagRef: ref(SHA_B),
      currentSha: SHA_A,
      assetName: ASSET,
    }),
    /must resolve directly to workflow commit/
  );
});

test('draft release rejects annotated/non-direct tag refs', () => {
  assert.throws(
    () => classifyReleaseTagState({
      release: draftRelease(),
      tagRef: ref(SHA_A, 'tag'),
      currentSha: SHA_A,
      assetName: ASSET,
    }),
    /must resolve directly to workflow commit/
  );
});

test('draft resumes only when both the actual tag and target commit match', () => {
  assert.equal(
    classifyReleaseTagState({
      release: draftRelease(),
      tagRef: ref(SHA_A),
      currentSha: SHA_A,
      assetName: ASSET,
    }),
    'resume-draft'
  );

  assert.throws(
    () => classifyReleaseTagState({
      release: draftRelease({ target_commitish: SHA_B }),
      tagRef: ref(SHA_A),
      currentSha: SHA_A,
      assetName: ASSET,
    }),
    /targets .* not workflow commit/
  );
});

test('published release with its asset remains an immutable no-op', () => {
  assert.equal(
    classifyReleaseTagState({
      release: publishedRelease({
        immutable: true,
        assets: [{ name: ASSET }],
      }),
      tagRef: ref(SHA_B),
      currentSha: SHA_A,
      assetName: ASSET,
    }),
    'published-noop'
  );
});

test('published release missing its asset requires a matching direct tag', () => {
  assert.equal(
    classifyReleaseTagState({
      release: publishedRelease(),
      tagRef: ref(SHA_A),
      currentSha: SHA_A,
      assetName: ASSET,
    }),
    'repair-published'
  );

  assert.throws(
    () => classifyReleaseTagState({
      release: publishedRelease(),
      tagRef: ref(SHA_B),
      currentSha: SHA_A,
      assetName: ASSET,
    }),
    /must resolve directly to workflow commit/
  );
});

test('immutable published release missing its asset cannot be repaired', () => {
  assert.throws(
    () => classifyReleaseTagState({
      release: publishedRelease({ immutable: true }),
      tagRef: ref(SHA_A),
      currentSha: SHA_A,
      assetName: ASSET,
    }),
    /Immutable release .* is missing/
  );
});

test('tag guard refuses a non-SHA workflow identity', () => {
  assert.throws(
    () => assertReleaseTagMatchesCommit(ref(SHA_A), 'main', TAG),
    /40-hex workflow commit SHA/
  );
});
