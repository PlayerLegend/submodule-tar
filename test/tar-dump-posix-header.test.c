#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define FLAT_INCLUDES
#include "../../keyargs/keyargs.h"
#include "../../range/def.h"
#include "../../window/def.h"
#include "../../convert/source.h"
#include "../../convert/fd/source.h"
#include "../internal/spec.h"
#include "../../log/log.h"

#define print_string_field(field) printf ("%.*s\n", (int) sizeof (field), field)

size_t read_size (struct posix_header * header)
{
    char * endptr;
    size_t retval = 0;

    if (header->size[0] & 128) // base 256 encoding
    {
	log_error ("base 256 not implemented");
	assert (0);
    }
    else // null terminated octal encoding
    {
	retval = strtol (header->size, &endptr, 8);
	if (!*header->size || *endptr)
	{
	    log_error ("could not parse size");
	    assert (0);
	}
    }

    return retval;
}

void print_header (struct posix_header * header)
{
    print_string_field (header->name);
    print_string_field (header->magic);
    printf ("%zu\n", read_size (header));
}

int main()
{
    window_unsigned_char read_buffer = {0};
    
    fd_source fd_read = fd_source_init(.fd = STDIN_FILENO, .contents = &read_buffer);

    bool error = false;

    if (!convert_fill_minimum(&error, &fd_read.source, sizeof(struct posix_header)))
    {
	log_fatal ("Could not read whole header");
    }

    struct posix_header * header = (struct posix_header*) read_buffer.region.begin;

    print_header (header);

    return 0;

fail:
    return 1;
}
