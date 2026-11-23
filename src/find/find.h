/* Byte search over a flat range or piece view. */
#ifndef EDIT_FIND_H
#define EDIT_FIND_H
#include "piece/piece.h"
#include "work/work.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* (G) Inherited v1 byte-search contract limits. FIND_MAX_OFFSETS bounds
 * result storage. The other maxima prepay parser, program, regex states
 * and scratch. FIND_POLL_UNITS is the inherited cancellation quantum. */
#define FIND_MAX_OFFSETS 4096u
#define FIND_MAX_GROUPS 8u
#define FIND_MAX_STATES 256u
#define FIND_MAX_PATTERN 4096u
#define FIND_MAX_DEPTH 32u
#define FIND_POLL_UNITS 1024u
#define FIND_MAX_PROGRAM_BYTES (64u * 1024u)
#define FIND_MAX_SCRATCH_BYTES (256u * 1024u)
#define FIND_UNSET UINT64_MAX

typedef enum find_code {
    FIND_OK = 0, FIND_CANCELLED, FIND_ERR_ARGUMENT, FIND_ERR_MEMORY,
    FIND_ERR_SYNTAX, FIND_ERR_UNSUPPORTED, FIND_ERR_LIMIT
} find_code;

/* A whole byte range or piece view. Offsets are relative to its
 * beginning. NULL bytes is valid only at length zero. For a view, bytes
 * and length must be zero. Keep the view and caller storage alive
 * throughout the search. Use a live view on the UI thread within one
 * revision. The caller owns any snapshot whose view is used. Mapped views
 * must have a pristine backing. Recovery or a failed fault-id poll cancels the request. */
typedef struct find_source {
    const uint8_t *bytes;
    size_t length;
    const piece_view *view;
} find_source;

/* Optional cancellation inputs, ORed. work calls work_should_stop; generation
 * is stale when != expected_generation. All pointed-to objects must stay alive.
 * Poll before/after the search and at bounded work intervals, including within
 * long verifications and epsilon closure. At most FIND_POLL_UNITS work units
 * between polls, combined across loops: one literal/prefix byte comparison,
 * one NFA state visit (including its byte/anchor predicate), or one candidate
 * advance is a unit. A bulk operation may process at most FIND_POLL_UNITS bytes
 * before polling. This bound includes preprocessing a literal needle.
 * (G) v1 G6c tracks worker CPU intervals up to 5 ms.
 * This API returns privately. For a work job, work_cancel + work_publish/drain
 * suppress publication stale by work epoch, including cancellation after
 * return. Flag/generation changes alone do not invalidate work mailboxes:
 * caller must recheck/drop them at publication/consumption, or also work_cancel
 * the job. Never publish a partial count as exact or a cancelled result;
 * complete single matches from next() may be streamed. */
typedef struct find_control {
    const work_ctx *work;
    const atomic_bool *cancel;
    const _Atomic uint32_t *generation;
    uint32_t expected_generation;
} find_control;

/* (G) v1 perf §2.7 result bound is 64 KiB. FIND_OK supplies the exact
 * total and the first FIND_MAX_OFFSETS starts in ascending order. Other
 * codes clear total/stored. The array tail is unspecified.
 * Literal search returns leftmost non-overlapping matches and an empty
 * needle gives zero matches. NUL and high bytes are valid. All search and
 * capture calls use caller storage and the stack. Literal kernels have worst-case linear
 * work in source + needle length. SIMD verification has a linear work
 * budget and can fall back to Two-Way. */
typedef struct find_result {
    uint64_t total;
    size_t stored;
    uint64_t offsets[FIND_MAX_OFFSETS];
} find_result;
/* fold_ascii compares A-Z and a-z by their lowercase byte. All other bytes
 * compare unchanged. whole_word requires that neither adjacent byte belongs
 * to [A-Za-z0-9_]. Source edges qualify as boundaries. Rejected candidates
 * leave overlapping starts eligible, while accepted matches advance to end. */
typedef struct find_literal_options {
    bool fold_ascii;
    bool whole_word;
} find_literal_options;
find_code find_literal(const find_source *source, const uint8_t *needle,
                       size_t needle_length, find_literal_options options,
                       const find_control *control,
                       find_result *result);

/* ERE-lite: concatenation, |, (), [], [^], inclusive byte ranges, ., *, +, ?,
 * ^ and $. Anchors are multiline: ^ at start/after LF, $ before LF/at EOF.
 * Dot excludes LF; negated classes include LF. Backslash quotes ONE byte;
 * \n is literal n (use an actual LF). ASCII/C-locale byte semantics.
 * Empty expression/branch/group permitted. Reject repeated quantifiers,
 * quantifiers directly on anchors, unclosed delimiters, backwards/empty classes.
 * Backreferences (\0..\9), { } counted repeats and POSIX named classes are
 * unsupported, not silently reinterpreted. Escape metacharacters for literals.
 * Limits: FIND_MAX_PATTERN pattern bytes, FIND_MAX_DEPTH parenthesis nesting,
 * FIND_MAX_STATES NFA states, FIND_MAX_GROUPS capturing groups (opening
 * parenthesis order). Exceeding any limit returns FIND_ERR_LIMIT; the representation's state budget may
 * reject a pattern earlier than its byte/depth/group limits.
 * Compile takes max_align_t-aligned caller storage; the returned immutable
 * program owns no pointers into pattern. Failed compile sets *out=NULL and
 * error_offset (optional) to a byte position <=pattern_length. Size query is
 * an upper bound independent of pattern; memory may come from base_arena.
 * Program query <= FIND_MAX_PROGRAM_BYTES; scratch query for a live program
 * is positive and <= FIND_MAX_SCRATCH_BYTES (storage alignment padding extra).
 * Invalid pointers/alignment return ARGUMENT, insufficient storage MEMORY,
 * malformed supported syntax SYNTAX, reserved constructs UNSUPPORTED. A class
 * hyphen is literal first/last or escaped; a literal hyphen may be a range
 * endpoint (e.g. [--0] includes '-', '.', '/', '0'). ']' must be escaped
 * inside classes.
 * A backslash before EOF is SYNTAX. Outside classes unescaped { or } and
 * escaped digits are UNSUPPORTED. POSIX [:name:], [.collating.] and [=equiv=]
 * inside classes are UNSUPPORTED. Other bytes, including NUL/high bytes, literal.
 * Success sets error_offset=0. Storage must not alias pattern or any input.
 * Compile also allocates nothing. Queries require a successful live program;
 * NULL queries return zero/NULL. Prefix need only be sound, not maximal. A
 * pattern beginning with unquantified literal bytes, without a top-level
 * alternation, must expose a nonempty prefix; more extraction is optional. */
typedef struct find_regex find_regex;
size_t find_regex_bytes(void);
find_code find_regex_compile(void *memory, size_t memory_size,
                             const uint8_t *pattern, size_t pattern_length,
                             find_regex **out, size_t *error_offset);
/* (P) Four bytes per expanded ASCII literal plus two alphabet widths
 * of class working room, the v1 expansion buffer. Expanded syntax is
 * subject to FIND_MAX_PATTERN. */
#define FIND_REGEX_FOLD_BUFFER_BYTES (FIND_MAX_PATTERN * 4u + 2u * 256u)

/* Optional ASCII-insensitive regex compile. Validate the unexpanded query
 * with the normal grammar, then expand letter/class membership before
 * compiling the byte-sensitive NFA. On any failure *out is NULL. Expansion
 * requires a separate caller buffer disjoint from the query and program.
 * Query error offsets are preserved. Expansion limits report query_length.
 * The optional worker is polled through preprocessing. No allocation. */
find_code find_regex_compile_options(void *program, size_t program_bytes,
    const uint8_t *query, size_t query_length, bool fold_ascii,
    uint8_t *pattern, size_t pattern_capacity, const work_ctx *context,
    find_regex **out, size_t *error_offset);

/* Common mandatory literal prefix (possibly empty); borrowed from program.
 * A filter may reject starts only if this prefix fails at that same start. */
const uint8_t *find_regex_prefix(const find_regex *regex, size_t *length);
size_t find_regex_group_count(const find_regex *regex);
size_t find_regex_scratch_bytes(const find_regex *regex);

/* Leftmost-longest whole matches, non-overlapping. Empty matches are emitted
 * once per byte boundary, including EOF, then advance one byte (EOF ends).
 * For equal endpoints, captures follow ordered Thompson paths: earlier | arm
 * first; repetition's consuming arm first. First visit to a state at a given
 * byte boundary wins. This is explicit subgroup tie policy, not full POSIX
 * subgroup ordering. Scratch is max_align_t-aligned, exclusive to one call;
 * program may be shared. No backtracking or recursive path enumeration.
 * Inputs/storage/results must not alias. NULL or misaligned scratch ARGUMENT;
 * too-small scratch MEMORY. Result is required, including on empty input. */
find_code find_regex_search(const find_source *source, const find_regex *regex,
                            void *scratch, size_t scratch_size,
                            const find_control *control, find_result *result);
typedef struct find_capture { uint64_t start, end; } find_capture;
typedef struct find_match {
    bool matched;
    size_t groups;
    find_capture whole;
    find_capture captures[FIND_MAX_GROUPS]; /* never participating = UNSET pair;
        repeated group = last participating iteration (earlier nested captures
        survive an iteration skipping that nested group); empty = {pos,pos}.
        Unused slots are UNSET pairs even on successful matches. */
} find_match;
/* Re-run anchored at an offset from result, returning whole range and captures
 * for replacement without bloating bounded find_result. off <= source length.
 * No match returns FIND_OK with matched=false, ranges unset. */
find_code find_regex_captures(const find_source *source, const find_regex *regex,
                              uint64_t off, void *scratch, size_t scratch_size,
                              const find_control *control, find_match *match);
/* Single-match streaming/pagination, starting at off <= source length. Find
 * the next leftmost match at or after off, with absolute source offsets.
 * Literal empty needle has no match. Regex includes an empty EOF match.
 * Advance to whole.end after a nonempty match; after an empty match advance
 * one byte, stopping at EOF. This is also search's enumeration policy.
 * Regex captures() is anchored at off, whereas next() may scan past off.
 * On any error/cancel, match is cleared: matched=false, groups=0, all ranges
 * UNSET. Successful no-match has groups=program group count (literal zero).
 * A literal match has no groups. All searches check argument/storage errors
 * before cancellation, then poll even for an empty needle/source. No-match
 * and failed searches clear counts; unused offset tails remain unspecified.
 * Stream these private matches from a work job with work_publish; its mailbox
 * carries small batches/handles, never a whole find_result. Keep snapshot,
 * program, scratch and result alive through acknowledgement. Replace-all
 * gathers edits against this immutable snapshot, then the UI applies them
 * right-to-left in one undo group. find itself mutates/publishes nothing. */
find_code find_literal_next(const find_source *source, const uint8_t *needle,
                            size_t needle_length, find_literal_options options, uint64_t off,
                            const find_control *control, find_match *match);
find_code find_regex_next(const find_source *source, const find_regex *regex,
                          uint64_t off, void *scratch, size_t scratch_size,
                          const find_control *control, find_match *match);

/* Count the entire source. Emit a union of the bounded prefix, the first
 * visible_capacity matches intersecting [window_start, window_end), and the
 * zero-based wanted ordinal (FIND_UNSET disables it). Empty matches include
 * both window boundaries. Each selected ordinal is delivered once in order.
 * A false callback cancels. Counts clear on failure. Keep callback effects
 * private until successful completion, and discard them on failure. */
typedef struct find_visit {
    size_t prefix_capacity, visible_capacity;
    uint64_t wanted, window_start, window_end;
    bool (*emit)(void *user,uint64_t ordinal,find_capture range);
    void *user;
    uint64_t total, visible;
    /* For a literal byte mask, bit k denotes [base+k, base+k+1) and ordinal
     * is the rank of the first set bit. This callback handles selection and
     * updates visible itself. Individual tail matches still call emit. */
    bool (*emit_mask)(void *user,uint64_t mask,uint64_t base,uint64_t ordinal);
} find_visit;
find_code find_literal_visit(const find_source *source,const uint8_t *needle,size_t n,
                             find_literal_options options, const find_control *control, find_visit *visit);
find_code find_regex_visit(const find_source *source,const find_regex *regex,void *scratch,
                           size_t size,const find_control *control,find_visit *visit);
/* ASCII word boundaries share the request's readers, prefix plan and retry
 * budget. Rejecting a candidate retries at start + one byte; accepting one
 * retains the ordinary leftmost non-overlap policy, including empty matches. */
find_code find_regex_word_visit(const find_source *source,const find_regex *regex,void *scratch,
                                size_t size,const find_control *control,find_visit *visit);
/* Worker-owned continuation. The size query returns zero for NULL. Begin
 * takes exclusive aligned scratch and copies the source descriptor. Keep its
 * view/bytes and immutable program alive. A live view must keep the same
 * revision through all steps. Step yields at NFA byte boundaries around the
 * polling quantum. Use the same visitor, settings and output storage for
 * every step. Counts and emitted ranges stay private until done is true.
 * Failure clears counts and ends the continuation. Begin before stepping a
 * new request. Exact match, boundary and empty policies match the sync API. */
size_t find_regex_stream_scratch_bytes(const find_regex *regex);
find_code find_regex_stream_begin(const find_source *source,const find_regex *regex,
                                  void *scratch,size_t size,bool whole_word);
find_code find_regex_stream_step(void *scratch,const find_control *control,
                                 find_visit *visit,bool *done);
/* Filtered consumers share a request budget across overlapping retries,
 * including successful but rejected candidates. */
uint64_t find_regex_work_budget(const find_regex *regex,uint64_t length);
find_code find_regex_next_budget(const find_source *source,const find_regex *regex,uint64_t off,
                                void *scratch,size_t size,const find_control *control,
                                uint64_t *budget,find_match *match);
#endif
