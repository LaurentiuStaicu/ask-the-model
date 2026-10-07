namespace AskTheModel.Tests {

private const string MODEL = "test-model";
private const string DIGEST = "test-digest";

private static void
write_json_response (
    Soup.ServerMessage message,
    string body
) {
    message.set_status (Soup.Status.OK, null);
    message.set_response (
        "application/json",
        Soup.MemoryUse.COPY,
        body.data
    );
}

private static void
tags_handler (
    Soup.Server server,
    Soup.ServerMessage message,
    string path,
    GLib.HashTable<string, string>? query
) {
    write_json_response (
        message,
        "{\"models\":[{\"name\":\"test-model\",\"digest\":\"test-digest\"},{\"name\":\"replacement-model\",\"digest\":\"replacement-digest\"}]}"
    );
}

private static void
show_handler (
    Soup.Server server,
    Soup.ServerMessage message,
    string path,
    GLib.HashTable<string, string>? query
) {
    write_json_response (
        message,
        "{\"capabilities\":[\"completion\"]}"
    );
}

private static void
chat_handler (
    Soup.Server server,
    Soup.ServerMessage message,
    string path,
    GLib.HashTable<string, string>? query
) {
    write_json_response (
        message,
        "{\"message\":{\"content\":\"provider-answer\"},\"done\":true}\n"
    );
}

private static int result_code = 0;

private static async void
run_checks (
    GLib.MainLoop loop
) {
    var server = new Soup.Server (null);
    server.add_handler ("/api/tags", tags_handler);
    server.add_handler ("/api/show", show_handler);
    server.add_handler ("/api/chat", chat_handler);

    try {
        int64 live_generation = 41;

        server.listen_local (
            0,
            Soup.ServerListenOptions.IPV4_ONLY
        );

        var uris = server.get_uris ();
        assert (uris != null);
        assert (uris.data != null);

        string base_url = "http://%s:%d".printf (
            uris.data.get_host (),
            uris.data.get_port ()
        );
        Environment.set_variable (
            "ATM_M12_TEST_BASE_URL",
            base_url,
            true
        );

        var provider = new AskTheModel.OllamaProvider ();
        provider.set_current_generation_reader (() => {
            return 41;
        });
        assert (yield provider.discover ());
        assert (provider.is_ready ());
        assert (provider.model_name == MODEL);
        assert (provider.model_digest == DIGEST);

        var stale_begin_conversation =
            new AskTheModel.OllamaConversation ();

        bool stale_begin_rejected = false;

        try {
            yield provider.chat_grounded (
                "hello",
                "Follow the grounding rules.",
                "evidence",
                "Use only the supplied evidence.",
                stale_begin_conversation,
                true,
                41,
                "wrong-model",
                DIGEST
            );
        } catch (AskTheModel.ProviderError.STALE_IDENTITY error) {
            stale_begin_rejected = true;
        }

        assert (stale_begin_rejected);
        assert (stale_begin_conversation.message_count () == 0);

        var stale_model_provider =
            new AskTheModel.OllamaProvider ();
        stale_model_provider.set_current_generation_reader (() => {
            return live_generation;
        });

        assert (yield stale_model_provider.discover ());
        assert (stale_model_provider.select_model (MODEL));

        var stale_model_conversation =
            new AskTheModel.OllamaConversation ();

        bool stale_model_rejected = false;

        stale_model_provider.response_chunk.connect ((chunk) => {
            assert (stale_model_provider.select_model ("replacement-model"));
        });

        try {
            yield stale_model_provider.chat_grounded (
                "hello",
                "Follow the grounding rules.",
                "evidence",
                "Use only the supplied evidence.",
                stale_model_conversation,
                true,
                41,
                MODEL,
                DIGEST
            );
        } catch (AskTheModel.ProviderError.STALE_IDENTITY error) {
            stale_model_rejected = true;
        }

        assert (stale_model_rejected);
        assert (stale_model_conversation.message_count () == 0);

        live_generation = 41;

        var stale_generation_provider =
            new AskTheModel.OllamaProvider ();
        stale_generation_provider.set_current_generation_reader (() => {
            return live_generation;
        });

        assert (yield stale_generation_provider.discover ());
        assert (stale_generation_provider.select_model (MODEL));

        var stale_generation_conversation =
            new AskTheModel.OllamaConversation ();

        bool stale_generation_rejected = false;

        stale_generation_provider.response_chunk.connect ((chunk) => {
            live_generation = 42;
        });

        try {
            yield stale_generation_provider.chat_grounded (
                "hello",
                "Follow the grounding rules.",
                "evidence",
                "Use only the supplied evidence.",
                stale_generation_conversation,
                true,
                41,
                MODEL,
                DIGEST
            );
        } catch (AskTheModel.ProviderError.STALE_IDENTITY error) {
            stale_generation_rejected = true;
        }

        assert (stale_generation_rejected);
        assert (stale_generation_conversation.message_count () == 0);

        var valid_conversation =
            new AskTheModel.OllamaConversation ();

        string answer = yield provider.chat_grounded (
            "hello",
            "Follow the grounding rules.",
            "evidence",
            "Use only the supplied evidence.",
            valid_conversation,
            true,
            41,
            MODEL,
            DIGEST
        );

        assert (answer == "provider-answer");
        assert (valid_conversation.message_count () == 2);

        Environment.unset_variable (
            "ATM_M12_TEST_BASE_URL"
        );
    } catch (GLib.Error error) {
        stderr.printf (
            "ollama-provider-turn-guard: %s\n",
            error.message
        );
        result_code = 1;
    } finally {
        Environment.unset_variable (
            "ATM_M12_TEST_BASE_URL"
        );
        server.disconnect ();
        loop.quit ();
    }
}

public static int
main (
    string[] args
) {
    var loop = new GLib.MainLoop ();
    run_checks.begin (loop);
    loop.run ();
    return result_code;
}

}
