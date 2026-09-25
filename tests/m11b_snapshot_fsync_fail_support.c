#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static unsigned long fsync_calls = 0;

static unsigned long
target_call (void)
{
    const char *value =
        getenv ("ATM_M11B_FAIL_FSYNC_CALL");

    if (value == NULL || value[0] == '\0') {
        return 0;
    }

    char *end = NULL;
    unsigned long result =
        strtoul (value, &end, 10);

    if (end == value || *end != '\0') {
        return 0;
    }

    return result;
}

static void
write_marker (
    unsigned long call
)
{
    const char *path =
        getenv ("ATM_M11B_FSYNC_MARKER");

    if (path == NULL || path[0] == '\0') {
        return;
    }

    FILE *stream = fopen (path, "w");

    if (stream == NULL) {
        return;
    }

    fprintf (stream, "%lu\n", call);
    fclose (stream);
}

int
atm_m11b_snapshot_fsync (
    int fd
)
{
    fsync_calls++;

    unsigned long target = target_call ();

    if (target > 0 &&
        fsync_calls == target) {
        write_marker (fsync_calls);
        errno = EIO;
        return -1;
    }

    return fsync (fd);
}
