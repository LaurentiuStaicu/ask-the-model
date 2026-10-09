namespace AskTheModel {
    /*
     * Vala bindings for the M11 publication barrier (src/publication_barrier.h).
     *
     * The barrier supplies the condition ConversationPersistenceStore is
     * missing: a conversation may not be discarded until its export has been
     * durably published. The store currently discards unarchived conversations
     * BEFORE its automatic export runs, and that export is best-effort, so a
     * failed export becomes a log line and the next startup deletes the row.
     *
     * Keeping the binding in its own file rather than inline in the store is
     * deliberate: the store is large, and the C symbol names are the part a
     * reviewer most needs to see in one place next to the header they mirror.
     */

    namespace PublicationBarrierNative {
        [CCode (
            cname = "atm_publication_barrier_new",
            cheader_filename = "publication_barrier.h"
        )]
        public static extern void* barrier_new ();

        [CCode (
            cname = "atm_publication_barrier_free",
            cheader_filename = "publication_barrier.h"
        )]
        public static extern void barrier_free (void* barrier);

        /* `published_at_us` is recorded by the C module only on success and is
         * never compared by it; the store passes 0. */
        [CCode (
            cname = "atm_publication_barrier_record",
            cheader_filename = "publication_barrier.h"
        )]
        public static extern bool record (
            void* barrier,
            string conversation_id,
            bool succeeded,
            int64 published_at_us
        ) throws GLib.Error;

        [CCode (
            cname = "atm_publication_barrier_may_discard",
            cheader_filename = "publication_barrier.h"
        )]
        public static extern bool may_discard (
            void* barrier,
            string conversation_id
        ) throws GLib.Error;
    }

    /*
     * Thin wrapper over the C barrier. Both methods swallow the C error into a
     * warning and answer with the conservative value, because the caller is a
     * startup cleanup that must not throw: an unevaluable discard decision is
     * not permission to delete, so it returns false.
     */

    public class PublicationBarrier : Object {
        private void* handle = null;

        public PublicationBarrier () {
            handle = PublicationBarrierNative.barrier_new ();
        }

        ~PublicationBarrier () {
            if (handle != null) {
                PublicationBarrierNative.barrier_free (handle);
                handle = null;
            }
        }

        public void record (
            string conversation_id,
            bool succeeded
        ) {
            if (handle == null) {
                return;
            }

            try {
                PublicationBarrierNative.record (
                    handle,
                    conversation_id,
                    succeeded,
                    0
                );
            } catch (GLib.Error error) {
                warning (
                    "AtM: publication outcome could not be recorded for %s: %s",
                    conversation_id,
                    error.message
                );
            }
        }

        public bool may_discard (string conversation_id) {
            if (handle == null) {
                /* No barrier means no evidence of publication, which is not
                 * permission to discard. */
                return false;
            }

            try {
                return PublicationBarrierNative.may_discard (
                    handle,
                    conversation_id
                );
            } catch (GLib.Error error) {
                warning (
                    "AtM: discard decision could not be evaluated for %s: %s",
                    conversation_id,
                    error.message
                );
                return false;
            }
        }
    }
}
