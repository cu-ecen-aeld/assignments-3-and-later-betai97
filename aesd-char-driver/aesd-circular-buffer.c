/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/printk.h>
#define DEBUG(...) printk(__VA_ARGS__)
#else
#include <string.h>
#include <stdio.h>
#define DEBUG(...) printf(__VA_ARGS__)
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    uint8_t cur_buf_size = 0, index = 0;
    struct aesd_buffer_entry *cur;

    // null pointer checks
    if(buffer == NULL || entry_offset_byte_rtn == NULL) {
        DEBUG("Passed NULL!\n");
        return NULL;
    }

    index = buffer->out_offs;
    cur = &buffer->entry[index];
    do {
        // DEBUG("Iter [%d] of aesd_circular_buffer_find_entry_offset_for_fpos\n", index);
        // DEBUG("char_offset: %d\n", char_offset);
        // DEBUG("cur_buf_size: %d\n", cur_buf_size);
        // DEBUG("cur->size: %d\n", cur->size);
        // DEBUG("char_offset: %d\n", char_offset);
        if(char_offset >= cur_buf_size && char_offset < (cur_buf_size + cur->size)) {
            if((char_offset - cur_buf_size) >= cur->size) {
                DEBUG("Couldn't find requested global offset %d\n", char_offset);
                return NULL;
            }
            *entry_offset_byte_rtn = char_offset - cur_buf_size;
            DEBUG("For global offset %d, found offset %d\n", (int)char_offset, (int)*entry_offset_byte_rtn);
            DEBUG("Inside of string: %.*s\n", cur->size, cur->buffptr);
            return cur;
        }

        // Update buffer size
        if(cur->buffptr != NULL) {
            cur_buf_size += cur->size;
        }

        // Increment index and cur
        index++;
        if(index >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
            index = 0;
        }
        cur = &buffer->entry[index];
        
    } while(index != buffer->out_offs);

    // if not found, return null
    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    // null pointer checks
    if(buffer == NULL || add_entry == NULL) {
        DEBUG("Error: null pointer passed!\n");
        return;
    }

    // Add new entry
    buffer->entry[buffer->in_offs].size = add_entry->size;
    buffer->entry[buffer->in_offs].buffptr = add_entry->buffptr;

    // Move up in_offs
    if(buffer->in_offs + 1 >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
        buffer->in_offs = 0;
    } else {
        buffer->in_offs++;
    }

    // Check for buffer full
    if(buffer->in_offs == buffer->out_offs || buffer->full == true) {
        buffer->full = true;

        // Move up out_offs
        buffer->out_offs = buffer->in_offs;
    }


    DEBUG("buffer: {\n");
    int i;
    for(i=0; i<10; i++) {
        if(buffer->entry[i].buffptr != NULL) {
            DEBUG("%c%c[%d] %.*s\n", (i==buffer->in_offs)?'i':' ', \
                                (i==buffer->out_offs)?'o':' ', \
                                i, buffer->entry[i].size, buffer->entry[i].buffptr);
        }
    }
    DEBUG("}\n");

}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
