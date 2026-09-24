namespace AskTheModel {
    namespace RepositoryGenerationLeaseNative {
        [CCode (
            cname = "atm_repository_generation_lease_try_acquire_shared",
            cheader_filename = "repository_generation_lease.h"
        )]
        public static extern bool try_acquire_shared (
            string state_root,
            int64 generation_id,
            out int lease_fd,
            out bool contended
        ) throws GLib.Error;

        [CCode (
            cname = "atm_repository_generation_lease_release",
            cheader_filename = "repository_generation_lease.h"
        )]
        public static extern void release (
            int lease_fd
        );
    }

    public class RepositoryGenerationLease : Object {
        private int lease_fd = -1;

        public int64 generation_id {
            get;
            construct;
        }

        private RepositoryGenerationLease (
            int64 generation_id,
            int lease_fd
        ) {
            Object (
                generation_id: generation_id
            );
            this.lease_fd = lease_fd;
        }

        public static RepositoryGenerationLease? try_acquire_shared (
            string state_root,
            int64 generation_id,
            out bool contended
        ) throws GLib.Error {
            int lease_fd;
            contended = false;

            bool acquired =
                RepositoryGenerationLeaseNative.try_acquire_shared (
                    state_root,
                    generation_id,
                    out lease_fd,
                    out contended
                );

            if (!acquired) {
                throw new GLib.IOError.FAILED (
                    "Repository generation coordination could not be established."
                );
            }

            if (contended) {
                return null;
            }

            if (lease_fd < 0) {
                throw new GLib.IOError.FAILED (
                    "Repository generation coordination returned no lease."
                );
            }

            return new RepositoryGenerationLease (
                generation_id,
                lease_fd
            );
        }

        ~RepositoryGenerationLease () {
            if (lease_fd >= 0) {
                RepositoryGenerationLeaseNative.release (
                    lease_fd
                );
                lease_fd = -1;
            }
        }
    }
}
