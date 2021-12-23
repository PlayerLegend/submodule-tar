#ifndef FLAT_INCLUDES
#include <stdio.h>
#include <stdbool.h>
#define FLAT_INCLUDES
#include "../keyargs/keyargs.h"
#include "../range/def.h"
#include "../window/def.h"
#include "../convert/source.h"
#include "common.h"
#endif

/**
   @file tar/read.h
   Describes the public interface for the reading portion of the tar library.
   In order to read through a tar with this library, first allocate and zero a tar_state structure. Then feed it tar sectors using tar_update_fd or tar_update_mem according to your needs. The metadata pertaining to the current file/directory/link/etc can be directly read from the tar_state after it has been updated. If the tar is being read from a stream, the contents of a file must be read before the tar state is updated again. To do this, use either tar_read_region or tar_skip_file.

   \todo Add chain-io functionality to the tar library
   \todo Consider adding a mainpage to the tar library
   \todo Update docs after the buffer to window change
*/

typedef struct tar_state tar_state;
struct tar_state {
    bool ready; ///< True if the state is ready for use
    
    tar_type type; ///< The current item type
    window_char path; ///< The path of the current item

    size_t mode; ///< The mode of the current item

    struct tar_state_link ///< tar_state information that is specific to links
    {
	window_char path; ///< If the current item is a hardlink or symlink, this is its target path
    }
	link; ///< Contains information specific to hardlinks and symlinks

    struct tar_state_file ///< tar_state information that is specific to files
    {
	size_t size; ///< If the current item is a file, this is its size
	size_t bytes_read;
    }
	file; ///< Contains information specific to files
    
    convert_source * source;
};

bool tar_read_file_part (bool * error, range_const_unsigned_char * contents, tar_state * state);

/**< @struct tar_state
   Gives the header information for the current file in a tar
*/

void tar_restart(tar_state * state);
/**<
   @brief Restarts the given state so that it may be used to read a different tar file.
   @param state The state to be restarted
*/

bool tar_update (tar_state * state);
/**<
   @brief Reads the first or next item from the convert interface into the given state
   @return True if successful, false otherwise
   @param state The state to be updated
   @param interface The interface to read from
*/

bool tar_update_mem (tar_state * state, range_const_unsigned_char * mem);
/**<
   @brief Consumes chunks as needed from mem to update the given tar_state
   @return True if successful, false otherwise
   @param state The state to be updated
   @param rest A range of bytes that will be pointed to the portion of mem following the chunk read by this function. Pass NULL here if you do not need that.
   @param mem A range of input bytes
*/

void tar_cleanup (tar_state * state);
/**<
   @brief Frees all memory allocated to the given state, but not the state itself.
   @param state The state to be cleaned
*/

bool tar_skip_file (tar_state * state);
/**<
   @brief Skips the file in a tar stream currently described by 'state'. If 'state' is not currently indicating a file, then the behavior of this function is undefined.
   @return True if successful, false otherwise
   @param state The state describing the file to skip
   @param fd The file descriptor to read from
*/

bool tar_read_file_whole (window_unsigned_char * output, tar_state * state);
