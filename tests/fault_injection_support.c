#include "fault_injection_test_hook.h"
#include "fault_injection_support.h"

#include <glib.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>

static const char *selected_checkpoint = NULL;
static int selected_checkpoint_fd = -1;
static int selected_control_fd = -1;

static gboolean
write_all (
    int fd,
    const char *data,
    gsize length
)
{
    gsize offset = 0;

    while (offset < length) {
        ssize_t written = write (
            fd,
            data + offset,
            length - offset
        );

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }

            return FALSE;
        }

        offset += (gsize) written;
    }

    return TRUE;
}

void
atm_test_fault_configure (
    const char *target_checkpoint,
    int checkpoint_fd,
    int control_fd
)
{
    selected_checkpoint = target_checkpoint;
    selected_checkpoint_fd = checkpoint_fd;
    selected_control_fd = control_fd;
}

void
atm_test_fault_checkpoint (
    const char *checkpoint_id
)
{
    char control = '\0';

    if (selected_checkpoint == NULL ||
        checkpoint_id == NULL ||
        g_strcmp0 (
            selected_checkpoint,
            checkpoint_id
        ) != 0) {
        return;
    }

    if (selected_checkpoint_fd < 0 ||
        selected_control_fd < 0) {
        _exit (124);
    }

    if (!write_all (
            selected_checkpoint_fd,
            checkpoint_id,
            strlen (checkpoint_id)
        ) ||
        !write_all (
            selected_checkpoint_fd,
            "\n",
            1
        )) {
        _exit (125);
    }

    for (;;) {
        ssize_t result = read (
            selected_control_fd,
            &control,
            1
        );

        if (result < 0 && errno == EINTR) {
            continue;
        }

        if (result != 1 || control != 'C') {
            _exit (126);
        }

        break;
    }
}
