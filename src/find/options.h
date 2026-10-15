/* Internal ASCII option layer shared by findui and fif, above frozen find.h.
 * The compiled core remains byte sensitive and has no option policy. */
#ifndef EDIT_FIND_OPTIONS_H
#define EDIT_FIND_OPTIONS_H
#include "find/find.h"
/* (E) Each ASCII literal expands to [aA], four bytes; a fully expanded byte
 * class adds at most twice the byte alphabet (2 * 256 = 512) scratch bytes.
 * This retains findui's existing prepaid expansion capacity. */
#define FIND_OPTIONS_PATTERN_BYTES (FIND_MAX_PATTERN * 4u + 512u)
static inline uint8_t find_options_fold(uint8_t byte)
{ return byte >= 'A' && byte <= 'Z' ? (uint8_t)(byte + ('a' - 'A')) : byte; }
static inline bool find_options_word(uint8_t byte)
{
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
           (byte >= '0' && byte <= '9') || byte == '_';
}
find_code find_options_compile(void *program, size_t program_bytes,
    const uint8_t *query, size_t query_length, bool match_case,
    uint8_t *pattern, size_t pattern_capacity, const work_ctx *context,
    find_regex **regex, size_t *error_offset);
#endif
