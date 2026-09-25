using AskTheModel;

private void
test_native_bridge () {
    const string input =
        "## Heading\n\n" +
        "Paragraph with `inline` code.";

    PresentationNative.Document document;

    try {
        assert (
            PresentationNative.normalize (
                input,
                (size_t) input.length,
                out document
            )
        );
    } catch (GLib.Error error) {
        assert_not_reached ();
    }

    assert (!PresentationNative.is_fallback (document));
    assert (PresentationNative.fallback_reason (document) == null);
    assert (PresentationNative.block_count (document) == 2);

    assert (
        PresentationNative.block_type_at (
            document,
            0
        ) == PresentationNative.BlockType.HEADING
    );
    assert (
        PresentationNative.heading_level_at (
            document,
            0
        ) == 2
    );
    assert (
        PresentationNative.segment_count_at (
            document,
            0
        ) == 1
    );
    assert (
        PresentationNative.segment_text_at (
            document,
            0,
            0
        ) == "Heading"
    );

    assert (
        PresentationNative.block_type_at (
            document,
            1
        ) == PresentationNative.BlockType.PARAGRAPH
    );
    assert (
        PresentationNative.segment_count_at (
            document,
            1
        ) == 3
    );
    assert (
        PresentationNative.segment_type_at (
            document,
            1,
            1
        ) == PresentationNative.SegmentType.INLINE_CODE
    );
    assert (
        PresentationNative.segment_text_at (
            document,
            1,
            1
        ) == "inline"
    );

    assert (
        PresentationNative.info_at (
            document,
            1
        ) == null
    );
}

int
main (string[] args) {
    Test.init (ref args);

    Test.add_func (
        "/presentation/native-bridge",
        test_native_bridge
    );

    return Test.run ();
}
