[CCode (cheader_filename = "conversation_turn_guard.h")]
namespace AskTheModel.TurnGuardNative {
    [Compact]
    [CCode (
        cname = "AtmTurnGuard",
        free_function = "atm_turn_guard_free",
        has_type_id = false
    )]
    public class Guard {
        [CCode (cname = "atm_turn_guard_new")]
        public Guard ();

        [CCode (cname = "atm_turn_guard_begin")]
        public bool begin (
            int64 repository_generation_id,
            string model,
            string? model_digest
        ) throws GLib.Error;

        [CCode (cname = "atm_turn_guard_commit")]
        public bool commit (
            int64 current_repository_generation_id,
            string current_model,
            string? current_model_digest
        ) throws GLib.Error;

        [CCode (cname = "atm_turn_guard_abort")]
        public void abort ();

        [CCode (cname = "atm_turn_guard_is_open")]
        public bool is_open ();
    }
}
