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

        assert (repositories[1].id == "cbd");
        assert (repositories[1].acronym == "CBD");
        assert (
            repositories[1].display_name ==
            "Cognitive Belief Dynamics"
        );

        assert (repositories[2].id == "rmd");
        assert (repositories[2].acronym == "RMD");
        assert (
            repositories[2].display_name ==
            "Romanian Monetary Dynamics"
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
