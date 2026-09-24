using GLib;

private void test_default_off () {
    var policy = new AskTheModel.OptimizationPolicy ();

    assert (!policy.enabled);
    assert (!policy.snapshot_enabled ());
}

private void test_toggle_and_snapshot () {
    var policy = new AskTheModel.OptimizationPolicy ();
    int change_count = 0;
    bool last_value = false;

    policy.changed.connect ((enabled) => {
        change_count++;
        last_value = enabled;
    });

    policy.set_enabled (true);

    assert (policy.enabled);
    assert (policy.snapshot_enabled ());
    assert (change_count == 1);
    assert (last_value);

    policy.set_enabled (true);
    assert (change_count == 1);

    policy.set_enabled (false);

    assert (!policy.enabled);
    assert (!policy.snapshot_enabled ());
    assert (change_count == 2);
    assert (!last_value);
}

int main (string[] args) {
    Test.init (ref args);

    Test.add_func (
        "/optimization-policy/default-off",
        test_default_off
    );
    Test.add_func (
        "/optimization-policy/toggle-and-snapshot",
        test_toggle_and_snapshot
    );

    return Test.run ();
}
