#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#define FLAT_INCLUDES
#include "../../range/def.h"
#include "../../window/def.h"
#include "../../window/alloc.h"
#include "../../keyargs/keyargs.h"
#include "../../convert/def.h"
#include "../../convert/fd.h"
#include "../internal/spec.h"
#include "../../log/log.h"
#include "../common.h"
#include "../read.h"

int main()
{
    window_unsigned_char buffer = {0};
    fd_interface fd_read = fd_interface_init(.fd = STDIN_FILENO, .read_buffer = &buffer);
    tar_state state = { .source = &fd_read.interface };
    window_unsigned_char file_contents = {0};;

    while (tar_update (&state))
    {
	switch (state.type)
	{
	case TAR_FILE:
	    log_normal ("file: %s", state.path.region.begin);

	    window_rewrite (file_contents);

	    assert (tar_read_file_whole (&file_contents, &state));
	    
	    log_normal ("\tcontents(%zu): [%.*s]", range_count (file_contents.region), (int)range_count (file_contents.region), file_contents.region.begin);

	    assert (state.type != TAR_ERROR);
	    assert ((size_t)range_count (file_contents.region) == state.file.size);
	    
	    break;

	case TAR_DIR:
	    log_normal ("directory: %s", state.path.region.begin);
	    break;

	case TAR_SYMLINK:
	    log_normal ("symlink: %s -> %s", state.path.region.begin, state.link.path.region.begin);
	    break;

	default:
	    log_fatal ("Bad type for this test");
	}
    }

    assert (state.type == TAR_END);

    tar_cleanup (&state);
    window_clear (file_contents);
    window_clear (buffer);

    return 0;

fail:
    return 1;
}
