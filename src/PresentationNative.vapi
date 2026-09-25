[CCode (cheader_filename = "presentation_document.h,presentation_normalize.h")]
namespace AskTheModel.PresentationNative {
    [CCode (
        cname = "AtmPresentationBlockType",
        cprefix = "ATM_PRESENTATION_BLOCK_",
        cheader_filename = "presentation_document.h"
    )]
    public enum BlockType {
        PARAGRAPH,
        HEADING,
        LIST_ITEM,
        QUOTE,
        CODE,
        SPACER
    }

    [CCode (
        cname = "AtmPresentationSegmentType",
        cprefix = "ATM_PRESENTATION_SEGMENT_",
        cheader_filename = "presentation_document.h"
    )]
    public enum SegmentType {
        TEXT,
        INLINE_CODE
    }

    [Compact]
    [CCode (
        cname = "AtmPresentationDocument",
        free_function = "atm_presentation_document_free",
        has_type_id = false
    )]
    public class Document {
    }

    [CCode (
        cname = "atm_presentation_normalize",
        cheader_filename = "presentation_normalize.h"
    )]
    public static bool normalize (
        string input,
        size_t length,
        out Document document
    ) throws GLib.Error;

    [CCode (cname = "atm_presentation_document_is_fallback")]
    public static bool is_fallback (
        Document document
    );

    [CCode (cname = "atm_presentation_document_fallback_reason")]
    public static unowned string? fallback_reason (
        Document document
    );

    [CCode (cname = "atm_presentation_document_block_count")]
    public static uint block_count (
        Document document
    );

    [CCode (cname = "atm_presentation_document_block_type_at")]
    public static BlockType block_type_at (
        Document document,
        uint block_index
    );

    [CCode (cname = "atm_presentation_document_heading_level_at")]
    public static uint heading_level_at (
        Document document,
        uint block_index
    );

    [CCode (cname = "atm_presentation_document_nesting_level_at")]
    public static uint nesting_level_at (
        Document document,
        uint block_index
    );

    [CCode (cname = "atm_presentation_document_quote_depth_at")]
    public static uint quote_depth_at (
        Document document,
        uint block_index
    );

    [CCode (cname = "atm_presentation_document_ordered_at")]
    public static bool ordered_at (
        Document document,
        uint block_index
    );

    [CCode (cname = "atm_presentation_document_ordinal_at")]
    public static uint ordinal_at (
        Document document,
        uint block_index
    );

    [CCode (cname = "atm_presentation_document_info_at")]
    public static unowned string? info_at (
        Document document,
        uint block_index
    );

    [CCode (cname = "atm_presentation_document_segment_count_at")]
    public static uint segment_count_at (
        Document document,
        uint block_index
    );

    [CCode (cname = "atm_presentation_document_segment_type_at")]
    public static SegmentType segment_type_at (
        Document document,
        uint block_index,
        uint segment_index
    );

    [CCode (cname = "atm_presentation_document_segment_text_at")]
    public static unowned string? segment_text_at (
        Document document,
        uint block_index,
        uint segment_index
    );
}
