namespace AskTheModel {
    public class OptimizationPolicy : Object {
        public bool enabled { get; private set; default = false; }

        public signal void changed (bool enabled);

        public void set_enabled_for_session (bool enabled) {
            if (this.enabled == enabled) {
                return;
            }

            this.enabled = enabled;
            changed (enabled);
        }

        public bool snapshot_enabled () {
            return enabled;
        }
    }
}
