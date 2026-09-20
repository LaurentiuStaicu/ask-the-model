namespace AskTheModel {
    public class RepositorySelection : Object {
        private string[] selected_ids = {};

        private static bool catalog_contains (string repository_id) {
            foreach (RepositoryDescriptor descriptor in RepositoryCatalog.all ()) {
                if (descriptor.id == repository_id) {
                    return true;
                }
            }

            return false;
        }

        public bool is_selected (string repository_id) {
            foreach (string selected_id in selected_ids) {
                if (selected_id == repository_id) {
                    return true;
                }
            }

            return false;
        }

        public bool set_selected (
            string repository_id,
            bool selected
        ) {
            if (!catalog_contains (repository_id)) {
                return false;
            }

            if (selected == is_selected (repository_id)) {
                return true;
            }

            string[] updated = {};

            if (selected) {
                foreach (string selected_id in selected_ids) {
                    updated += selected_id;
                }

                updated += repository_id;
            } else {
                foreach (string selected_id in selected_ids) {
                    if (selected_id != repository_id) {
                        updated += selected_id;
                    }
                }
            }

            selected_ids = updated;
            return true;
        }

        public RepositoryDescriptor[] selected_repositories () {
            RepositoryDescriptor[] selected = {};

            foreach (
                RepositoryDescriptor descriptor
                in RepositoryCatalog.all ()
            ) {
                if (is_selected (descriptor.id)) {
                    selected += descriptor;
                }
            }

            return selected;
        }

        public string summary () {
            string[] acronyms = {};

            foreach (RepositoryDescriptor descriptor in RepositoryCatalog.all ()) {
                if (is_selected (descriptor.id)) {
                    acronyms += descriptor.acronym;
                }
            }

            if (acronyms.length == 0) {
                return "Repositories";
            }

            return string.joinv (" + ", acronyms);
        }
    }
}
