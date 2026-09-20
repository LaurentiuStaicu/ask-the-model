namespace AskTheModel.Tests {
    public static int main (string[] args) {
        RepositoryDescriptor[] repositories = RepositoryCatalog.all ();

        assert (repositories.length == 3);

        assert (repositories[0].id == "ewd");
        assert (repositories[0].acronym == "EWD");
        assert (
            repositories[0].display_name ==
            "Empirical World3 Dynamics"
        );
        assert (repositories[0].supported_version == "0.1.0");
        assert (
            repositories[0].selector_label () ==
            "EWD (Empirical World3 Dynamics) v0.1.0"
        );

        assert (repositories[1].id == "cbd");
        assert (repositories[1].acronym == "CBD");
        assert (
            repositories[1].display_name ==
            "Cognitive Belief Dynamics"
        );
        assert (repositories[1].supported_version == "0.1.0");
        assert (
            repositories[1].selector_label () ==
            "CBD (Cognitive Belief Dynamics) v0.1.0"
        );

        assert (repositories[2].id == "rmd");
        assert (repositories[2].acronym == "RMD");
        assert (
            repositories[2].display_name ==
            "Romanian Monetary Dynamics"
        );
        assert (repositories[2].supported_version == "0.1.0");
        assert (
            repositories[2].selector_label () ==
            "RMD (Romanian Monetary Dynamics) v0.1.0"
        );

        foreach (RepositoryDescriptor descriptor in repositories) {
            assert (descriptor.owner == "LaurentiuStaicu");
            assert (descriptor.tracked_branch == "main");
        }

        assert (
            repositories[0].branch_api_url () ==
            "https://api.github.com/repos/LaurentiuStaicu/" +
            "empirical-world3-dynamics/branches/main"
        );

        string sha = "0123456789abcdef0123456789abcdef01234567";

        assert (
            repositories[2].archive_api_url (sha) ==
            "https://api.github.com/repos/LaurentiuStaicu/" +
            "romanian-monetary-dynamics/tarball/" + sha
        );

        try {
            assert (
                repositories[0].immutable_file_permalink (
                    sha,
                    "STATUS.md",
                    "lines:3-8"
                ) ==
                "https://github.com/LaurentiuStaicu/" +
                "empirical-world3-dynamics/blob/" + sha +
                "/STATUS.md?plain=1#L3-L8"
            );

            assert (
                repositories[1].immutable_file_permalink (
                    sha,
                    "model/variables.json",
                    "json:/3"
                ) ==
                "https://github.com/LaurentiuStaicu/" +
                "cognitive-belief-dynamics/blob/" + sha +
                "/model/variables.json"
            );

            assert (
                repositories[2].immutable_file_permalink (
                    sha,
                    "model/a file #1.json",
                    "json:/value"
                ) ==
                "https://github.com/LaurentiuStaicu/" +
                "romanian-monetary-dynamics/blob/" + sha +
                "/model/a%20file%20%231.json"
            );

            assert (
                repositories[2].immutable_file_permalink (
                    sha,
                    "data/example.csv",
                    "lines:42-42"
                ).has_suffix (
                    "/data/example.csv#L42"
                )
            );
        } catch (CitationError error) {
            assert_not_reached ();
        }

        bool rejected = false;

        try {
            repositories[0].immutable_file_permalink (
                sha.up (),
                "STATUS.md",
                "lines:1-1"
            );
        } catch (CitationError.INVALID_SHA error) {
            rejected = true;
        } catch (CitationError error) {
            assert_not_reached ();
        }

        assert (rejected);
        rejected = false;

        try {
            repositories[0].immutable_file_permalink (
                sha,
                "../STATUS.md",
                "lines:1-1"
            );
        } catch (CitationError.INVALID_SOURCE error) {
            rejected = true;
        } catch (CitationError error) {
            assert_not_reached ();
        }

        assert (rejected);
        rejected = false;

        try {
            repositories[0].immutable_file_permalink (
                sha,
                "STATUS.md",
                "lines:8-3"
            );
        } catch (CitationError.INVALID_LOCATOR error) {
            rejected = true;
        } catch (CitationError error) {
            assert_not_reached ();
        }

        assert (rejected);

        assert (
            RepositoryClient.MAX_ARCHIVE_BYTES ==
            128 * 1024 * 1024
        );

        assert (
            RepositoryClient.staging_archive_path (
                repositories[0],
                sha,
                true
            ).has_suffix (
                "/repository-staging/ewd/" + sha + ".tar.gz.part"
            )
        );

        assert (
            RepositoryClient.staging_archive_path (
                repositories[2],
                sha
            ).has_suffix (
                "/repository-staging/rmd/" + sha + ".tar.gz"
            )
        );

        return 0;
    }
}
