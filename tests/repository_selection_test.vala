namespace AskTheModel.Tests {
    public static int main (string[] args) {
        var selection = new RepositorySelection ();

        assert (selection.summary () == "Repositories");
        assert (!selection.is_selected ("ewd"));

        assert (selection.set_selected ("rmd", true));
        assert (selection.summary () == "RMD");

        assert (selection.set_selected ("ewd", true));
        assert (selection.summary () == "EWD + RMD");

        assert (selection.set_selected ("cbd", true));
        assert (selection.summary () == "EWD + CBD + RMD");

        RepositoryDescriptor[] selected =
            selection.selected_repositories ();
        assert (selected.length == 3);
        assert (selected[0].id == "ewd");
        assert (selected[1].id == "cbd");
        assert (selected[2].id == "rmd");

        assert (selection.set_selected ("rmd", false));
        assert (selection.summary () == "EWD + CBD");

        assert (!selection.set_selected ("unknown", true));
        assert (selection.summary () == "EWD + CBD");

        return 0;
    }
}
