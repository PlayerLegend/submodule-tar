#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#define FLAT_INCLUDES
#include "../range/def.h"
#include "../window/def.h"
#include "../window/alloc.h"
#include "../window/printf.h"
#include "../convert/def.h"
#include "../keyargs/keyargs.h"
#include "common.h"
#include "read.h"
#include "../log/log.h"
#include "internal/spec.h"

void tar_restart(tar_state * state)
{
    state->type = TAR_ERROR;
    window_rewrite (state->path);
    window_rewrite (state->link.path);
}

static bool tar_get_size (size_t * size, const struct posix_header * header)
{
    char * endptr;

    if (header->size[0] & 128) // base 256 encoding, not sure of format
    {
	log_error ("base 256 not implemented");
	return false;
    }
    else // null terminated octal encoding
    {
	*size = strtol (header->size, &endptr, 8);
	if (!*header->size || *endptr)
	{
	    log_error ("could not parse size");
	    return false;
	}
    }

    return true;
}

static bool tar_get_mode (size_t * mode, const struct posix_header * header)
{
    char * endptr;
    
    *mode = strtol (header->mode, &endptr, 8);
    if (!*header->mode || *endptr)
    {
	log_error ("could not parse mode, error at byte %zu", endptr - header->mode);
	return false;
    }

    return true;
}

static bool block_is_zero (const range_const_unsigned_char * header)
{
    for (uint64_t *test = (void*) header->begin; (void*) test < (void*) header->end; test++)
    {
	if (*test != 0)
	{
	    return false;
	}
    }
    
    return true;
}

static void append_blocks (window_char * name, size_t file_size, range_const_unsigned_char * mem)
{
    size_t want_size = file_size - range_count (name->region);

    size_t want_full_blocks = want_size / TAR_BLOCK_SIZE;

    size_t have_full_blocks = range_count (*mem) / TAR_BLOCK_SIZE;

    size_t get_full_blocks = want_full_blocks < have_full_blocks ? want_full_blocks : have_full_blocks;

    if (get_full_blocks)
    {
	window_append_bytes ((window_unsigned_char*) name, mem->begin, get_full_blocks * TAR_BLOCK_SIZE);
	mem->begin += get_full_blocks * TAR_BLOCK_SIZE;
    }

    if (get_full_blocks != want_full_blocks)
    {
	assert (get_full_blocks < want_full_blocks);
	assert ((size_t)range_count(name->region) < file_size);
	return;
    }
    
    size_t want_remainder = want_size % TAR_BLOCK_SIZE;
    
    if (want_remainder)
    {
	if (range_count(*mem) >= TAR_BLOCK_SIZE)
	{
	    window_append_bytes ((window_unsigned_char*) name, mem->begin, want_remainder);
	    mem->begin += TAR_BLOCK_SIZE;
	}
	else
	{
	    assert ((size_t)range_count(name->region) < file_size);
	    return;
	}
    }
    
    *window_push (*name) = '\0';
    name->region.end--;
}

bool tar_update_mem (tar_state * state, range_const_unsigned_char * mem)
{
    state->ready = false;

    bool longname = false;
    bool longlink = false;

    if (range_count (*mem) < TAR_BLOCK_SIZE)
    {
	goto notready;
    }

    if (state->type == TAR_LONGNAME)
    {
	if ((size_t) range_count(state->path.region) < state->file.size)
	{
	    append_blocks (&state->path, state->file.size, mem);
	    goto notready;
	}

	if ((size_t)range_count(state->path.region) != state->file.size)
	{
	    log_fatal ("tar longname path is oversized");
	}

	longname = true;
    }
    else if (state->type == TAR_LONGLINK)
    {
	if ((size_t) range_count(state->link.path.region) < state->file.size)
	{
	    append_blocks (&state->link.path, state->file.size, mem);
	    goto notready;
	}
	
	if ((size_t)range_count(state->link.path.region) != state->file.size)
	{
	    log_fatal ("tar longlink path is oversized");
	}

	longlink = true;
    }

    if (range_count (*mem) < TAR_BLOCK_SIZE)
    {
	goto notready;
    }
    
    const range_const_unsigned_char header_mem = { .begin = mem->begin, .end = mem->begin + TAR_BLOCK_SIZE };
    mem->begin += TAR_BLOCK_SIZE;

    const struct posix_header * header = (void*) header_mem.begin;
    assert (sizeof(*header) <= (size_t)range_count (header_mem));
    
    if (block_is_zero (&header_mem))
    {
	if (state->type == TAR_END)
	{
	    goto done;
	}
	else
	{
	    state->type = TAR_END;
	    goto notready;
	}
    }
    else if(header->typeflag == REGTYPE)
    {
	state->type = TAR_FILE;
    }
    else if (header->typeflag == DIRTYPE)
    {
	state->type = TAR_DIR;
    }
    else if (header->typeflag == SYMTYPE)
    {
	state->type = TAR_SYMLINK;
    }
    else if (header->typeflag == LNKTYPE)
    {
	state->type = LNKTYPE;
    }
    else
    {
	if (header->typeflag == GNUTYPE_LONGNAME)
	{
	    state->type = TAR_LONGNAME;
	    window_rewrite (state->path);
	}
	else if (header->typeflag == GNUTYPE_LONGLINK)
	{
	    state->type = TAR_LONGLINK;
	    window_rewrite (state->link.path);
	}
	else
	{
	    log_fatal ("Invalid typeflag in tar header");
	}

	goto notready;
    }
	
    if (!longname)
    {
	window_printf (&state->path, "%s", header->name);
    }
    
    if (state->type == TAR_HARDLINK || state->type == TAR_SYMLINK)
    {
	if (!longlink)
	{
	    window_printf (&state->link.path, "%s", header->linkname);
	}
    }
    else if (longlink)
    {
	log_fatal ("Longlink specified for non-link type (%d)", state->type);
    }
    
    if (state->type == TAR_FILE)
    {
	state->file.bytes_read = 0;
	if (!tar_get_size (&state->file.size, header))
	{
	    log_fatal ("Could not read tar file size");
	}
    }

    if (state->type == TAR_FILE || state->type == TAR_DIR || state->type == TAR_HARDLINK || state->type == TAR_SYMLINK)
    {
	if (!tar_get_mode (&state->mode, header))
	{
	    log_fatal ("Could not read tar file mode");
	}
    }
    
//ready:
    state->ready = true;
    assert (range_count (*mem) >= 0);
    return true;

fail:
    state->type = TAR_ERROR;
    state->ready = true;
    assert (range_count (*mem) >= 0);
    return false;
    
notready:
    state->ready = false;
    assert (range_count (*mem) >= 0);
    return true;

done:
    state->type = TAR_END;
    state->ready = true;
    assert (range_count (*mem) >= 0);
    return false;
}

bool tar_update (tar_state * state)
{
    bool error = false;
    
    while (true)
    {
	if (!convert_fill_minimum(&error, state->source, TAR_BLOCK_SIZE))
	{
	    log_fatal ("Input closed prematurely");
	}

	if (!tar_update_mem (state, &state->source->read_buffer->region.const_cast))
	{
	    goto done;
	}

	if (state->ready)
	{
	    goto newcontents;
	}
    }

fail:
    state->type = TAR_ERROR;
    state->ready = true;
    return false;

newcontents:
    assert (!error);
    return true;
    
done:
    assert (!error);
    return false;
}

inline static size_t size_to_blocks (size_t size)
{
    return size % TAR_BLOCK_SIZE == 0
	? size / TAR_BLOCK_SIZE
	: size / TAR_BLOCK_SIZE + 1;
}

bool tar_skip_file (tar_state * state)
{
    assert (state->type == TAR_FILE);

    size_t skip_size = size_to_blocks (state->file.size) * TAR_BLOCK_SIZE;

    bool error = false;

    if (convert_skip_bytes(&error, state->source, skip_size))
    {
	state->type = TAR_ERROR;
	return false;
    }
    else
    {
	return true;
    }
}

void tar_cleanup (tar_state * state)
{
    free (state->path.region.begin);
    window_rewrite (state->path);
    free (state->link.path.region.begin);
    window_rewrite (state->link.path);
}

bool tar_read_file_part (bool * error, range_const_unsigned_char * contents, tar_state * state)
{
    assert (!*error);
    
    size_t want_bytes = state->file.size - state->file.bytes_read;
    
    if (!want_bytes)
    {
	state->file.bytes_read = 0;

	size_t skip_bytes = TAR_BLOCK_SIZE - (state->file.size % TAR_BLOCK_SIZE);

	assert (skip_bytes <= TAR_BLOCK_SIZE);
	if (skip_bytes < TAR_BLOCK_SIZE && !convert_skip_bytes(error, state->source, skip_bytes))
	{
	    log_fatal ("Could not skip trailing file block bytes");
	}

	assert (!*error);
	return false;
    }
    else
    {
	if (!convert_pull_max(error, contents, state->source, want_bytes))
	{
	    log_fatal ("Tar file ended prematurely");
	}

	size_t got_bytes = range_count(*contents);
	state->file.bytes_read += got_bytes;

	assert (!*error);
	return true;
    }
    
fail:
    state->type = TAR_ERROR;
    return false;
}

bool tar_read_file_whole (window_unsigned_char * output, tar_state * state)
{
    bool error = false;
    
    range_const_unsigned_char file_part;
    
    while (tar_read_file_part (&error, &file_part, state))
    {
	window_append_bytes_range(output, &file_part);
    }

    if (error)
    {
	state->type = TAR_ERROR;
    }

    return state->type != TAR_ERROR;
}
