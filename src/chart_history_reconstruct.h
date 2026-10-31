#pragma once
#include "chart_spec.h"
#include "chart_spec_v4.h"
G_BEGIN_DECLS

/* Reconstruct a persisted qualified GISTEMP chart from the pinned repository
 * snapshot. The source bytes are re-admitted before VerifiedSeries/ChartSpec
 * construction; persisted identities are checked against the rebuilt objects.
 * No persisted numeric points or display state are trusted. */
gboolean atm_chart_history_reconstruct_gistemp (
    const char *snapshot_path,
    const char *repository_id,
    const char *repository_version,
    const char *snapshot_sha,
    const char *chart_schema,
    const char *chart_spec_id,
    const char *chart_kind,
    const char *reconstruction_profile,
    const char *series_profile,
    const char *admission_profile,
    const char *scientific_id,
    const char *qualified_id,
    const char *source_path,
    AtmChartSpec **out,
    GError **error
);


/* Reconstruct a persisted qualified Energy Institute TES v2 chart from the
 * exact four-file bundle at the conversation's pinned EWD snapshot. Source
 * admission, Evidence/SRA/series identity and ChartSpec v2 are rebuilt; no
 * stored numeric points, display coordinates or model output are trusted. */
gboolean atm_chart_history_reconstruct_energy_institute (
    const char *snapshot_path,
    const char *repository_id,
    const char *repository_version,
    const char *snapshot_sha,
    const char *chart_schema,
    const char *chart_spec_id,
    const char *chart_kind,
    const char *reconstruction_profile,
    const char *series_profile,
    const char *admission_profile,
    const char *scientific_id,
    const char *qualified_id,
    const char *source_path,
    AtmChartSpec **out,
    GError **error
);

/* Rebuild a live EI v2 chart from the approved four-file snapshot. CFF release
 * version remains separate from the ChartSpec source version (snapshot:<sha>). */
gboolean atm_chart_live_reconstruct_energy_institute (
    const char *snapshot_path,
    const char *repository_id,
    const char *snapshot_sha,
    const char *chart_kind,
    AtmChartSpec **out,
    GError **error
);

/* Live reconstruction entry point; declaration needed by the C test caller
 * and the C emitted by Vala for Application.vala. */
gboolean atm_chart_live_reconstruct_gistemp (
    const char *snapshot_path,
    const char *repository_id,
    const char *repository_version,
    const char *snapshot_sha,
    AtmChartSpec **out,
    GError **error
);

/* Reconstruct a persisted BNR BLS C01 ChartSpec v3 from the exact five-file
 * pinned RMD source bundle. No numeric point or raster state is persisted. */
gboolean atm_chart_history_reconstruct_bnr_bls_c01 (
    const char *snapshot_path,
    const char *repository_id,
    const char *repository_version,
    const char *snapshot_sha,
    const char *chart_schema,
    const char *chart_spec_id,
    const char *chart_kind,
    const char *reconstruction_profile,
    const char *series_profile,
    const char *admission_profile,
    const char *scientific_id,
    const char *qualified_id,
    const char *source_path,
    AtmChartSpec **out,
    GError **error
);

/* Build a live BNR C01 spec only from the exact, pinned five-file RMD bundle. */
gboolean atm_chart_live_reconstruct_bnr_bls_c01 (
    const char *snapshot_path,
    const char *repository_id,
    const char *snapshot_sha,
    const char *chart_kind,
    AtmChartSpec **out,
    GError **error
);


/*
 * Catalog-selected live reconstruction for CHART-06. The immutable catalog
 * selects one closed source adapter from the series ID. The caller must first
 * authorize the requested series against the conversation's repository scope
 * and pass the actual pin identity; this function never accepts model-authored
 * scientific metadata. Durable History rows retain source-specific v1/v2/v3
 * identities; an optional v4 presentation sidecar is separately reconstructed
 * and compared against the catalog and source identity on reopen.
 */
gboolean atm_chart_live_reconstruct_v4 (
    const char *series_id,
    const char *snapshot_path,
    const char *repository_id,
    const char *repository_version,
    const char *snapshot_sha,
    AtmChartSpecV4 **out,
    GError **error
);


/* Wrap an already re-qualified v1/v2/v3 source spec for native v4 Chart/Data
 * presentation without changing the persisted source-specific recipe schema. */
gboolean atm_chart_wrap_qualified_spec_v4 (
    const char *series_id,
    const AtmChartSpec *qualified_source_spec,
    AtmChartSpecV4 **out,
    GError **error
);


/*
 * Build the complete catalog-derived sidecar input for one qualified v4 spec.
 * Returned string pointers are borrowed and remain valid while the spec lives.
 */
gboolean atm_chart_history_presentation_input_from_v4 (
    const AtmChartSpecV4 *spec,
    AtmConversationChartPresentationInput *out,
    GError **error
);

/*
 * Rebuild a v4 presentation from a source-specific spec already re-qualified
 * from the pinned History snapshot, then compare every stored sidecar field
 * and the semantic presentation ID before returning a renderable v4 spec.
 */
gboolean atm_chart_history_reconstruct_v4 (
    const char *series_id,
    const AtmChartSpec *qualified_source_spec,
    const AtmConversationChartPresentationInput *persisted,
    AtmChartSpecV4 **out,
    GError **error
);

G_END_DECLS
