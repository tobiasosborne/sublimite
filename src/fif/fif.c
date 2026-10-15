#include "fif/private.h"
#include <string.h>

_Static_assert(sizeof(fif_hit) <= WORK_MSG_DATA, "HIT fits the mailbox");
_Static_assert(sizeof(fif_file_msg) <= WORK_MSG_DATA, "FILE fits the mailbox");
_Static_assert(sizeof(fif_done) <= WORK_MSG_DATA, "DONE fits the mailbox");
static bool add_bytes(size_t *total, size_t n, size_t width)
{
    /* (P) Each arena allocation may round up by max_align_t - one. */
    size_t padding = _Alignof(max_align_t) - 1u;
    if (n > (SIZE_MAX - padding) / width || *total > SIZE_MAX - n * width - padding) return false;
    *total += n * width + padding; return true;
}
static void *reserve(fif_impl *impl, size_t n, size_t width)
{ return edit_arena_alloc(&impl->arena, n * width, _Alignof(max_align_t)); }
fif_code fif_job_init(fif_job *job, const fif_request *request)
{
    if (!job || job->impl || !request || !request->root ||
        (!request->query && request->query_length) || request->query_length > FIND_MAX_PATTERN)
        return FIF_ERR_ARGUMENT;
    size_t root_length = strnlen(request->root, FIF_PATH_BYTES);
    if (!root_length || root_length == FIF_PATH_BYTES) return FIF_ERR_ARGUMENT;
    fif_limits limits = request->limits;
    if (!limits.max_files) limits.max_files = FIF_DEFAULT_FILES;
    if (!limits.max_entries) limits.max_entries = FIF_DEFAULT_ENTRIES;
    if (!limits.max_hits) limits.max_hits = FIF_DEFAULT_HITS;
    if (!limits.max_file_bytes) limits.max_file_bytes = FIF_DEFAULT_FILE_BYTES;
    if (!limits.context_columns) limits.context_columns = FIF_DEFAULT_CONTEXT;
    if (!limits.max_depth) limits.max_depth = FIF_DEFAULT_DEPTH;
    if (limits.max_files > UINT32_MAX || limits.max_entries >= UINT32_MAX ||
        limits.max_hits > UINT32_MAX || limits.max_depth > FIF_DEFAULT_DEPTH ||
        limits.context_columns == SIZE_MAX || limits.max_file_bytes == SIZE_MAX)
        return FIF_ERR_ARGUMENT;
    size_t total = 0;
    if (!add_bytes(&total, 1, sizeof(fif_impl)) ||
        !add_bytes(&total, limits.max_entries, sizeof(fif_node)) ||
        !add_bytes(&total, limits.max_entries + 1u, sizeof(fif_identity)) ||
        !add_bytes(&total, limits.max_files, sizeof(fif_file_record)) ||
        !add_bytes(&total, limits.max_hits, sizeof(fif_hit_record)) ||
        !add_bytes(&total, limits.max_hits, limits.context_columns + 1u) ||
        !add_bytes(&total, limits.max_file_bytes + 1u, sizeof(uint8_t)) ||
        (!request->options.match_case && !request->options.regex &&
            !add_bytes(&total, limits.max_file_bytes + 1u, sizeof(uint8_t))) ||
        !add_bytes(&total, FIND_OPTIONS_PATTERN_BYTES, sizeof(uint8_t)) ||
        !add_bytes(&total, find_regex_bytes(), sizeof(uint8_t)) ||
        !add_bytes(&total, FIND_MAX_SCRATCH_BYTES, sizeof(uint8_t))) return FIF_ERR_ARGUMENT;
    edit_arena arena;
    if (edit_arena_init(&arena, total)) return FIF_ERR_MEMORY;
    fif_impl *impl = edit_arena_alloc(&arena, sizeof *impl, _Alignof(max_align_t));
    if (!impl) { edit_arena_free(&arena); return FIF_ERR_MEMORY; }
    memset(impl, 0, sizeof *impl); impl->arena = arena; impl->limits = limits;
    impl->options = request->options; impl->query_length = request->query_length;
    memcpy(impl->root, request->root, root_length + 1u);
    while (root_length > 1u && impl->root[root_length - 1u] == '/') impl->root[--root_length] = 0;
    impl->root_length = root_length;
    if (request->query_length) memcpy(impl->query, request->query, request->query_length);
    fif_code code = fif_filter_parse(&impl->filter, request->include, request->exclude);
    if (code != FIF_OK) { arena = impl->arena; edit_arena_free(&arena); return code; }
    for (size_t i = 0; i < impl->filter.count; i++) {
        fif_rule *rule = &impl->filter.rules[i]; char *text = impl->filter.text + rule->offset;
        if (text[0] == '/' && !strncmp(text, impl->root, root_length) &&
            (root_length == 1u || text[root_length] == '/' || !text[root_length])) {
            const char *suffix = text + root_length;
            if (*suffix == '/') suffix++;
            if (*suffix) memmove(text, suffix, strlen(suffix) + 1u);
            else strcpy(text, ".");
            rule->slash = strchr(text, '/') != NULL;
        }
    }
    impl->nodes = reserve(impl, limits.max_entries, sizeof *impl->nodes);
    impl->seen = reserve(impl, limits.max_entries + 1u, sizeof *impl->seen);
    impl->files = reserve(impl, limits.max_files, sizeof *impl->files);
    impl->hits = reserve(impl, limits.max_hits, sizeof *impl->hits);
    impl->contexts = reserve(impl, limits.max_hits, limits.context_columns + 1u);
    impl->bytes = reserve(impl, limits.max_file_bytes + 1u, sizeof *impl->bytes);
    if (!request->options.match_case && !request->options.regex)
        impl->folded = reserve(impl, limits.max_file_bytes + 1u, sizeof *impl->folded);
    impl->pattern = reserve(impl, FIND_OPTIONS_PATTERN_BYTES, sizeof *impl->pattern);
    impl->program = reserve(impl, find_regex_bytes(), sizeof(uint8_t));
    impl->scratch = reserve(impl, FIND_MAX_SCRATCH_BYTES, sizeof(uint8_t));
    if (!impl->nodes || !impl->seen || !impl->files || !impl->hits || !impl->contexts ||
        !impl->bytes || (!request->options.match_case && !request->options.regex && !impl->folded) ||
        !impl->pattern || !impl->program || !impl->scratch) {
        arena = impl->arena; edit_arena_free(&arena); return FIF_ERR_MEMORY;
    }
    for (size_t i = 0; i < limits.max_files; i++) atomic_init(&impl->files[i].ready, false);
    for (size_t i = 0; i < limits.max_hits; i++) atomic_init(&impl->hits[i].ready, false);
    if (impl->folded)
        for (size_t i = 0; i < impl->query_length; i++) impl->query[i] = find_options_fold(impl->query[i]);
    job->impl = impl; return FIF_OK;
}
work_handle fif_submit(work_pool *pool, fif_job *job, uint32_t generation)
{
    if (!pool || !job || !job->impl || job->impl->pool) return (work_handle){0};
    fif_impl *impl = job->impl;
    work_job jobs[] = {{fif_worker, impl, generation, WORK_BULK},
                       {fif_notify, impl, generation, WORK_BULK}};
    /* Batch fills handles before unlocking the pool for either worker. */
    if (work_submit_batch(pool, jobs, sizeof jobs / sizeof *jobs, impl->handles)) return (work_handle){0};
    impl->pool = pool; return impl->handles[0];
}
work_handle fif_completion_handle(const fif_job *job)
{ return job && job->impl ? job->impl->handles[1] : (work_handle){0}; }
bool fif_job_finished(const fif_job *job)
{
    if (!job || !job->impl || !job->impl->pool) return true;
    const fif_impl *impl = job->impl;
    return work_handle_finished(impl->pool, impl->handles[0]) &&
           work_handle_finished(impl->pool, impl->handles[1]);
}
fif_code fif_job_dispose(fif_job *job)
{
    if (!job) return FIF_ERR_ARGUMENT;
    if (!job->impl) return FIF_OK;
    if (!fif_job_finished(job)) return FIF_ERR_BUSY;
    fif_impl *impl = job->impl;
    if (impl->pool) {
        for (size_t i = 0; i < sizeof impl->handles / sizeof *impl->handles; i++) {
            work_slot *slot = &impl->pool->slots[impl->handles[i].slot];
            if (atomic_load_explicit(&slot->pending, memory_order_acquire) &&
                atomic_load_explicit(&slot->epoch, memory_order_acquire) == impl->handles[i].epoch)
                return FIF_ERR_BUSY;
        }
    }
    edit_arena arena = impl->arena; job->impl = NULL; edit_arena_free(&arena); return FIF_OK;
}
fif_code fif_results(const fif_job *job, uint32_t index, fif_result_view *view)
{
    if (!job || !job->impl || !view || index >= job->impl->limits.max_hits) return FIF_ERR_ARGUMENT;
    const fif_impl *impl = job->impl; const fif_hit_record *hit = &impl->hits[index];
    if (!atomic_load_explicit(&hit->ready, memory_order_acquire)) return FIF_ERR_ARGUMENT;
    *view = (fif_result_view){.hit = hit->hit, .path = impl->files[hit->hit.file_index].path,
        .context = impl->contexts + (size_t)index * (impl->limits.context_columns + 1u),
        .context_length = hit->length, .context_truncated = hit->truncated};
    return FIF_OK;
}
const char *fif_results_path(const fif_job *job, uint32_t index)
{
    if (!job || !job->impl || index >= job->impl->limits.max_files ||
        !atomic_load_explicit(&job->impl->files[index].ready, memory_order_acquire)) return NULL;
    return job->impl->files[index].path;
}
size_t fif_context_cut(const uint8_t *line, size_t length, size_t cap)
{
    if (!line || length <= cap) return length;
    /* (P) A UTF-8 scalar is at most four bytes: at most three continuation
     * bytes (10xxxxxx) follow its lead. Longer malformed runs cut in place. */
    size_t cut = cap;
    for (unsigned back = 0; cut && back < 3u && (line[cut] & 0xc0u) == 0x80u; back++) cut--;
    return cut;
}
