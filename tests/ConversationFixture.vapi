[CCode (cheader_filename = "conversation_fixture.h")]
namespace AskTheModelTest.ConversationFixtureNative {
    [CCode (cname = "atm_test_conversation_fixture_create")]
    public static bool create (
        string repository_id,
        string acronym,
        string display_name,
        string repository_version,
        string snapshot_sha,
        out string snapshot_root,
        out string cache_root,
        out string index_path,
        out string detected_version
    ) throws GLib.Error;

    [CCode (cname = "atm_test_conversation_fixture_remove")]
    public static void remove (
        string snapshot_root,
        string cache_root
    );
}
