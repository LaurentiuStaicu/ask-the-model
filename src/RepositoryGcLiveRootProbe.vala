namespace AskTheModel {
    namespace RepositoryGcLiveRootLeaseNative {
        [CCode (
            cname = "atm_repository_generation_lease_try_acquire_exclusive",
            cheader_filename = "repository_generation_lease.h"
        )]
        public static extern bool try_acquire_exclusive (
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

    public class RepositoryGcLiveRootProbe : Object {
        public static bool generation_is_live (
            string state_root,
            int64 generation_id
        ) throws GLib.Error {
            if (state_root.length == 0 ||
                generation_id <= 0) {
                throw new GLib.IOError.INVALID_ARGUMENT (
                    "GC live-generation probing requires a state root and positive generation identifier."
                );
            }

            int lease_fd;
            bool contended;
            bool result =
                RepositoryGcLiveRootLeaseNative.
                    try_acquire_exclusive (
                        state_root,
                        generation_id,
                        out lease_fd,
                        out contended
                    );

            if (!result) {
                throw new GLib.IOError.FAILED (
                    "GC live-generation exclusive probe returned no result."
                );
            }

            if (contended) {
                if (lease_fd >= 0) {
                    RepositoryGcLiveRootLeaseNative.release (
                        lease_fd
                    );
                    throw new GLib.IOError.INVALID_DATA (
                        "GC live-generation probe reported contention while returning a lease descriptor."
                    );
                }

                return true;
            }

            if (lease_fd < 0) {
                throw new GLib.IOError.INVALID_DATA (
                    "GC live-generation probe acquired no lease and reported no contention."
                );
            }

            RepositoryGcLiveRootLeaseNative.release (
                lease_fd
            );
            return false;
        }
    }
}
