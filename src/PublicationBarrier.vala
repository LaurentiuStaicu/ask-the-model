namespace AskTheModel {
    /*
     * Vala binding for the managed-export reconciliation barrier
     * (src/publication_barrier.h). The original type/file name is retained
     * within this patch.
     *
     * An unarchived working row may be discarded only after its stale managed
     * JSON export was removed or confirmed absent. An archived row's export is
     * synchronized, but archived rows are not cleanup candidates. Failures must
     * reach the startup cleanup gate rather than being swallowed as warnings.
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
     * Thin wrapper over the C barrier. An unevaluable discard decision is not
     * permission to delete, so may_discard returns false.
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
                    "AtM: export-reconciliation outcome could not be recorded for %s: %s",
                    conversation_id,
                    error.message
                );
            }
        }

        public bool may_discard (string conversation_id) {
            if (handle == null) {
                /* No barrier means no evidence of successful reconciliation, which
                 * is not permission to discard. */
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
