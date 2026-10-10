#include "findui/private.h"
#include "file/file.h"
#include <string.h>
#include <time.h>

static findui_impl *implementation(const findui_panel *panel)
{ return panel ? panel->private_ : NULL; }
static findui_range unset_range(void)
{ return (findui_range){FIND_UNSET, FIND_UNSET}; }
static uint64_t now_ns(void)
{
    struct timespec time;
    (void)clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * UINT64_C(1000000000) + (uint64_t)time.tv_nsec;
}
static bool intersects(findui_range range, uint64_t start, uint64_t end)
{
    return range.start == range.end ? range.start >= start && range.start <= end
                                   : start < end && range.start < end && range.end > start;
}
static bool slot_finished(const findui_impl *impl, const findui_slot *slot)
{ return work_handle_finished(impl->config.workers, slot->handle); }
static void reap(findui_impl *impl, bool maintenance)
{
    for (size_t i = 0; i < FINDUI_SLOT_COUNT; i++) {
        findui_slot *slot = &impl->slots[i];
        if (slot->snapshot && slot_finished(impl, slot) && (maintenance || !slot->lease.release)) {
            piece_snapshot_release(slot->snapshot); slot->snapshot = NULL;
            if (slot->lease.release) slot->lease.release(slot->lease.user);
            slot->lease = (findui_source_lease){0};
        }
    }
}
static void retire_source(findui_impl *impl)
{
    if (!impl->source) return;
    if (impl->source_lease.release) {
        EDIT_ASSERT(impl->retired_count < FINDUI_RETIRE_COUNT);
        impl->retired[impl->retired_count].snapshot = impl->source;
        impl->retired[impl->retired_count++].lease = impl->source_lease;
    } else piece_snapshot_release(impl->source);
    impl->source = NULL; impl->source_lease = (findui_source_lease){0};
}
findui_code findui_maintain(findui_panel *panel)
{
    findui_impl *impl = implementation(panel);
    if (!impl) return FINDUI_ERR_ARGUMENT;
    reap(impl, true);
    for (size_t i = 0; i < impl->retired_count; i++) {
        piece_snapshot_release(impl->retired[i].snapshot);
        impl->retired[i].lease.release(impl->retired[i].lease.user);
    }
    impl->retired_count = 0;
    return FINDUI_OK;
}
bool findui_source_retired(const findui_panel *panel, void *user, uint64_t identity)
{
    const findui_impl *impl = implementation(panel);
    if (!impl) return true;
    if (impl->source && impl->source_lease.user == user && impl->source_lease.identity == identity) return false;
    for (size_t i = 0; i < FINDUI_SLOT_COUNT; i++)
        if (impl->slots[i].snapshot && impl->slots[i].lease.user == user && impl->slots[i].lease.identity == identity) return false;
    for (size_t i = 0; i < impl->retired_count; i++)
        if (impl->retired[i].lease.user == user && impl->retired[i].lease.identity == identity) return false;
    return true;
}
static void cancel_jobs(findui_impl *impl)
{
    if (impl->request_active) impl->state.cancel_requests++;
    for (size_t i = 0; i < FINDUI_SLOT_COUNT; i++)
        work_cancel(impl->config.workers, impl->slots[i].handle);
    impl->request_active = false; impl->pending_submit = false;
}
static void clear_results(findui_impl *impl)
{
    impl->state.match_count = 0; impl->state.match_index = FINDUI_NO_INDEX;
    impl->state.selected = unset_range();
    impl->state.cached_matches = 0; impl->state.visible_matches = 0;
    impl->state.cache_overflow = false; impl->state.visible_overflow = false;
    impl->state.complete = false; impl->state.searching = false;
    impl->state.search_error = FIND_OK; impl->state.error_offset = 0;
}
static bool invalidate_source(findui_impl *impl)
{
    if (!file_snapshot_faulted(impl->source)) return false;
    cancel_jobs(impl);
    clear_results(impl);
    impl->state.search_error = FIND_CANCELLED;
    return true;
}
static findui_code editable(const findui_impl *impl)
{
    if (!impl || impl->disposing) return FINDUI_ERR_ARGUMENT;
    if (impl->state.replacing) return FINDUI_ERR_BUSY;
    if (impl->state.generation == UINT32_MAX) return FINDUI_ERR_LIMIT;
    return FINDUI_OK;
}
static void restart(findui_impl *impl, uint64_t desired)
{
    cancel_jobs(impl);
    impl->state.generation++;
    clear_results(impl); impl->desired_index = desired;
    impl->window_request = false; impl->window_dirty = false; impl->page_request = false;
    if (impl->source && impl->state.open) {
        if (!impl->state.query_length) impl->state.complete = true;
        else {
            impl->pending_submit = true; impl->request_active = true;
            impl->state.searching = true;
        }
    }
    findui_panel panel = {impl};
    (void)findui_service(&panel);
}
findui_code findui_init(findui_panel *panel, const findui_config *config)
{
    if (!panel || panel->private_ || !config || !config->arena || !config->workers ||
        work_pool_eventfd(config->workers) < 0 || !config->match_capacity || !config->visible_capacity ||
        config->match_capacity > FINDUI_RESULT_RANGES ||
        config->visible_capacity > FINDUI_RESULT_RANGES)
        return FINDUI_ERR_ARGUMENT;
    edit_arena_mark_t mark = edit_arena_mark(config->arena);
    findui_impl *impl = edit_arena_alloc(config->arena, sizeof *impl, _Alignof(findui_impl));
    if (!impl) return FINDUI_ERR_MEMORY;
    memset(impl, 0, sizeof *impl); impl->config = *config;
    impl->cache = edit_arena_alloc(config->arena, config->match_capacity * sizeof *impl->cache,
                                   _Alignof(findui_range));
    impl->visible = edit_arena_alloc(config->arena, config->visible_capacity * sizeof *impl->visible,
                                     _Alignof(findui_range));
    impl->replace_page = edit_arena_alloc(config->arena, config->match_capacity * sizeof *impl->replace_page,
                                          _Alignof(findui_range));
    bool memory_ok = impl->cache && impl->visible && impl->replace_page;
    for (size_t i = 0; i < FINDUI_SLOT_COUNT && memory_ok; i++) {
        findui_slot *slot = &impl->slots[i];
        slot->program = edit_arena_alloc(config->arena, find_regex_bytes(), _Alignof(max_align_t));
        slot->scratch = edit_arena_alloc(config->arena, FIND_MAX_SCRATCH_BYTES, _Alignof(max_align_t));
        slot->pattern = edit_arena_alloc(config->arena, FINDUI_PATTERN_BYTES, _Alignof(uint8_t));
        slot->outbox_capacity = config->match_capacity + config->visible_capacity + 2;
        slot->outbox = edit_arena_alloc(config->arena, slot->outbox_capacity * sizeof *slot->outbox,
                                        _Alignof(work_msg));
        memory_ok = slot->program && slot->scratch && slot->pattern && slot->outbox;
    }
    if (!memory_ok) { edit_arena_reset_to_mark(config->arena, mark); return FINDUI_ERR_MEMORY; }
    impl->state.query = impl->query; impl->state.replacement = impl->replacement;
    impl->state.generation = 1;
    clear_results(impl); panel->private_ = impl;
    return FINDUI_OK;
}
findui_code findui_service(findui_panel *panel)
{
    findui_impl *impl = implementation(panel);
    if (!impl) return FINDUI_ERR_ARGUMENT;
    (void)invalidate_source(impl);
    reap(impl, false);
    if (!impl->pending_submit) return FINDUI_OK;
    if (work_pool_eventfd(impl->config.workers) < 0) return FINDUI_ERR_BUSY;
    for (size_t i = 0; i < FINDUI_SLOT_COUNT; i++) {
        findui_slot *slot = &impl->slots[i];
        if (slot->snapshot) continue;
        slot->snapshot = piece_snapshot_retain(impl->source);
        slot->lease = impl->source_lease;
        if (slot->lease.retain) slot->lease.retain(slot->lease.user);
        slot->owner = (uint64_t)(uintptr_t)impl;
        slot->window_start = impl->window_start; slot->window_end = impl->window_end;
        slot->window_request = impl->window_request;
        slot->page_request = impl->page_request;
        slot->page_first = impl->page_first; slot->page_count = impl->page_count;
        slot->desired_index = impl->window_request ? FIND_UNSET : impl->desired_index;
        slot->cache_capacity = impl->window_request ? 0 : impl->config.match_capacity;
        slot->visible_capacity = impl->page_request ? 0 : impl->config.visible_capacity;
        if (impl->page_request) {
            slot->cache_capacity = impl->page_first + impl->page_count;
            slot->desired_index = FIND_UNSET; slot->window_start = 0; slot->window_end = 0;
        }
        slot->publishing = false; slot->outbox_count = 0; slot->outbox_next = 0;
        slot->query_length = impl->state.query_length;
        memcpy(slot->query, impl->query, slot->query_length);
        slot->options = impl->state.options; slot->hook = impl->config.scan_hook;
        slot->hook_user = impl->config.hook_user;
        slot->handle = work_submit(impl->config.workers,
            (work_job){findui_worker_run, slot, impl->state.generation, WORK_BULK});
        if (!slot->handle.epoch) {
            piece_snapshot_release(slot->snapshot); slot->snapshot = NULL;
            if (slot->lease.release) slot->lease.release(slot->lease.user);
            slot->lease = (findui_source_lease){0};
            return FINDUI_MORE;
        }
        impl->pending_submit = false; return FINDUI_OK;
    }
    return FINDUI_MORE;
}
findui_code findui_dispose(findui_panel *panel)
{
    findui_impl *impl = implementation(panel);
    if (!panel) return FINDUI_ERR_ARGUMENT;
    if (!impl) return FINDUI_OK;
    if (impl->state.replacing) return FINDUI_ERR_BUSY;
    impl->disposing = true; cancel_jobs(impl); (void)findui_maintain(panel);
    for (size_t i = 0; i < FINDUI_SLOT_COUNT; i++) if (impl->slots[i].snapshot) return FINDUI_MORE;
    if (impl->source) piece_snapshot_release(impl->source);
    if (impl->source_lease.release) impl->source_lease.release(impl->source_lease.user);
    panel->private_ = NULL;
    return FINDUI_OK;
}
findui_state findui_get_state(const findui_panel *panel)
{
    findui_impl *impl = implementation(panel);
    if (impl) (void)invalidate_source(impl);
    if (impl) return impl->state;
    return (findui_state){.match_index = FINDUI_NO_INDEX, .selected = {FIND_UNSET, FIND_UNSET}};
}
static void window_request(findui_impl *impl)
{
    cancel_jobs(impl); impl->state.generation++;
    impl->state.visible_matches = 0; impl->state.visible_overflow = false;
    impl->window_request = true; impl->window_dirty = false;
    if (impl->source && impl->state.open && impl->state.query_length) {
        impl->pending_submit = true; impl->request_active = true; impl->state.searching = true;
    }
    findui_panel panel = {impl}; (void)findui_service(&panel);
}
bool findui_accept(findui_panel *panel, const work_msg *message)
{
    findui_impl *impl = implementation(panel);
    if (!impl || !message || (message->kind != FINDUI_MSG_ONE && message->kind != FINDUI_MSG_TWO &&
                              message->kind != FINDUI_MSG_DONE)) return false;
    uint64_t owner;
    memcpy(&owner, message->data, sizeof owner);
    if (owner != (uint64_t)(uintptr_t)impl) return false;
    if (invalidate_source(impl)) return true;
    if (impl->disposing || (impl->state.replacing && !impl->page_request) || message->generation != impl->state.generation ||
        (!impl->state.searching && !impl->page_request)) return true;
    if (impl->state.replacing && impl->page_request) {
        if (message->kind == FINDUI_MSG_DONE) {
            findui_done done; memcpy(&done, message->data, sizeof done);
            impl->page_error = (find_code)done.code; impl->page_ready = true;
        } else {
            findui_batch batch; memcpy(&batch, message->data, sizeof batch);
            size_t count = message->kind == FINDUI_MSG_TWO ? 2u : 1u;
            for (size_t i = 0; i < count; i++) {
                uint64_t ordinal = batch.ordinal + i;
                if (ordinal >= impl->page_first && ordinal - impl->page_first < impl->page_count)
                    impl->replace_page[(size_t)(ordinal - impl->page_first)] = batch.ranges[i];
            }
        }
        return true;
    }
    if (message->kind == FINDUI_MSG_DONE) {
        findui_done done;
        memcpy(&done, message->data, sizeof done);
        impl->state.searching = false;
        if (!impl->window_request) impl->state.complete = done.code == FIND_OK;
        impl->state.search_error = (find_code)done.code;
        impl->state.error_offset = (size_t)done.error_offset;
        if (done.code != FIND_OK) {
            find_code code = impl->state.search_error; size_t error = impl->state.error_offset;
            if (!impl->window_request) clear_results(impl);
            else { impl->state.visible_matches = 0; impl->state.visible_overflow = false; }
            impl->state.search_error = code; impl->state.error_offset = error;
        } else {
            if (!impl->window_request) {
                impl->state.match_count = done.count;
                impl->state.cache_overflow = done.count > impl->config.match_capacity;
            }
            if (done.visible != FIND_UNSET) {
                impl->state.visible_matches = done.visible>SIZE_MAX ? SIZE_MAX : (size_t)done.visible;
                impl->state.visible_overflow = done.visible > impl->config.visible_capacity;
            }
            if (!impl->window_request && !done.count) { impl->state.match_index = FINDUI_NO_INDEX; impl->state.selected = unset_range(); }
        }
        if (impl->window_dirty && impl->state.complete) window_request(impl);
        return true;
    }
    findui_batch batch;
    memcpy(&batch, message->data, sizeof batch);
    size_t count = message->kind == FINDUI_MSG_TWO ? 2u : 1u;
    for (size_t i = 0; i < count; i++) {
        uint64_t ordinal = batch.ordinal + i;
        findui_range range = batch.ranges[i];
        if (!impl->window_request && ordinal < impl->config.match_capacity) {
            impl->cache[(size_t)ordinal] = range;
            impl->state.cached_matches = (size_t)ordinal + 1;
        }
        if (!impl->window_dirty && intersects(range, impl->window_start, impl->window_end)) {
            if (impl->state.visible_matches < impl->config.visible_capacity)
                impl->visible[impl->state.visible_matches] = range;
            else impl->state.visible_overflow = true;
            impl->state.visible_matches++;
        }
        if (!impl->window_request && ordinal == impl->desired_index) {
            impl->state.match_index = ordinal; impl->state.selected = range;
        }
        if (!impl->window_request && ordinal >= impl->state.match_count) impl->state.match_count = ordinal + 1;
    }
    impl->state.cache_overflow = impl->state.match_count > impl->config.match_capacity;
    return true;
}
findui_code findui_set_source_leased(findui_panel *panel, piece_snapshot *snapshot, uint64_t revision,
                                    const findui_source_lease *lease)
{
    findui_impl *impl = implementation(panel); findui_code code = editable(impl);
    if (code != FINDUI_OK) return code;
    if (lease && (!snapshot || !lease->identity || !lease->retain || !lease->release)) return FINDUI_ERR_ARGUMENT;
    if (impl->source_lease.release && impl->retired_count >= FINDUI_RETIRE_COUNT - 1) return FINDUI_ERR_BUSY;
    if (snapshot) piece_snapshot_retain(snapshot);
    if (lease) lease->retain(lease->user);
    retire_source(impl);
    impl->source = snapshot; impl->source_lease = lease ? *lease : (findui_source_lease){0};
    impl->state.revision = revision;
    restart(impl, 0); return FINDUI_OK;
}
findui_code findui_set_source(findui_panel *panel, piece_snapshot *snapshot, uint64_t revision)
{ return findui_set_source_leased(panel, snapshot, revision, NULL); }
findui_code findui_set_window(findui_panel *panel, uint64_t start, uint64_t end)
{
    findui_impl *impl = implementation(panel); findui_code code = editable(impl);
    if (code != FINDUI_OK) return code;
    if (start > end) return FINDUI_ERR_ARGUMENT;
    if (start == impl->window_start && end == impl->window_end) return FINDUI_OK;
    impl->window_start = start; impl->window_end = end;
    impl->state.visible_matches = 0; impl->state.visible_overflow = false;
    if (impl->state.complete) window_request(impl);
    else if (impl->state.searching) impl->window_dirty = true;
    return FINDUI_OK;
}
findui_code findui_show(findui_panel *panel, bool open, bool replace_mode)
{
    findui_impl *impl = implementation(panel); findui_code code = editable(impl);
    if (code != FINDUI_OK) return code;
    bool changed = impl->state.open != open;
    impl->state.open = open; impl->state.replace_mode = replace_mode;
    if (!replace_mode) impl->state.replacement_focus = false;
    if (changed) restart(impl, 0);
    return FINDUI_OK;
}
findui_code findui_set_options(findui_panel *panel, findui_options options)
{
    findui_impl *impl = implementation(panel); findui_code code = editable(impl);
    if (code != FINDUI_OK) return code;
    findui_options old = impl->state.options;
    if (old.regex == options.regex && old.match_case == options.match_case && old.whole_word == options.whole_word)
        return FINDUI_OK;
    impl->state.options = options; restart(impl, 0); return FINDUI_OK;
}
findui_code findui_edit_query(findui_panel *panel, size_t at, size_t erase,
                            const uint8_t *bytes, size_t length)
{
    findui_impl *impl = implementation(panel); findui_code code = editable(impl);
    if (code != FINDUI_OK) return code;
    size_t old_length = impl->state.query_length;
    if (at > old_length || erase > old_length - at || (length && !bytes)) return FINDUI_ERR_ARGUMENT;
    if (length > FINDUI_QUERY_BYTES - (old_length - erase)) return FINDUI_ERR_LIMIT;
    /* Accept aliasing with the borrowed query state without allocation. */
    uint8_t copied[FINDUI_QUERY_BYTES];
    if (length) memcpy(copied, bytes, length);
    memmove(impl->query + at + length, impl->query + at + erase, old_length - at - erase);
    if (length) memcpy(impl->query + at, copied, length);
    impl->state.query_length = old_length - erase + length; impl->state.query_cursor = at + length;
    restart(impl, 0); return FINDUI_OK;
}
findui_code findui_set_query(findui_panel *panel, const uint8_t *bytes, size_t length)
{
    const findui_impl *impl = implementation(panel);
    if (!impl) return FINDUI_ERR_ARGUMENT;
    return findui_edit_query(panel, 0, impl->state.query_length, bytes, length);
}
findui_code findui_set_query_cursor(findui_panel *panel, size_t at)
{
    findui_impl *impl = implementation(panel);
    if (!impl || impl->disposing || at > impl->state.query_length) return FINDUI_ERR_ARGUMENT;
    impl->state.query_cursor = at; return FINDUI_OK;
}
findui_code findui_set_replacement(findui_panel *panel, const uint8_t *bytes, size_t length, size_t cursor)
{
    findui_impl *impl = implementation(panel); findui_code code = editable(impl);
    if (code != FINDUI_OK) return code;
    if ((length && !bytes) || cursor > length) return FINDUI_ERR_ARGUMENT;
    if (length > FINDUI_QUERY_BYTES) return FINDUI_ERR_LIMIT;
    if (length) memmove(impl->replacement, bytes, length);
    impl->state.replacement_length = length; impl->state.replacement_cursor = cursor;
    return FINDUI_OK;
}
findui_code findui_focus_replacement(findui_panel *panel, bool focus)
{
    findui_impl *impl = implementation(panel);
    if (!impl || impl->disposing || (focus && !impl->state.replace_mode)) return FINDUI_ERR_ARGUMENT;
    impl->state.replacement_focus = focus; return FINDUI_OK;
}
findui_code findui_next(findui_panel *panel, int direction, findui_range *selected)
{
    findui_impl *impl = implementation(panel); findui_code code = editable(impl);
    if (impl && invalidate_source(impl)) return FINDUI_ERR_STALE;
    if (code != FINDUI_OK) return code;
    if (!selected || (direction != -1 && direction != 1)) return FINDUI_ERR_ARGUMENT;
    if (!impl->state.complete) return impl->state.searching ? FINDUI_ERR_BUSY : FINDUI_ERR_FIND;
    uint64_t count = impl->state.match_count;
    if (!count) { *selected = unset_range(); return FINDUI_OK; }
    uint64_t index = impl->state.match_index;
    if (index == FINDUI_NO_INDEX) index = direction == 1 ? 0 : count - 1;
    else if (direction == 1) index = index == count - 1 ? 0 : index + 1;
    else index = index == 0 ? count - 1 : index - 1;
    if (index < impl->state.cached_matches) {
        impl->state.match_index = index; impl->state.selected = impl->cache[(size_t)index];
        *selected = impl->state.selected; return FINDUI_OK;
    }
    *selected = unset_range(); restart(impl, index); return FINDUI_MORE;
}
findui_code findui_highlights(const findui_panel *panel, uint64_t start, uint64_t end,
                             findui_range *ranges, size_t capacity, size_t *count)
{
    findui_impl *impl = implementation(panel);
    if (!impl || !count || start > end || (capacity && !ranges)) return FINDUI_ERR_ARGUMENT;
    if (invalidate_source(impl)) { *count = 0; return FINDUI_ERR_STALE; }
    if (impl->state.replacing) return FINDUI_ERR_BUSY;
    if (start < impl->window_start || end > impl->window_end) return FINDUI_ERR_STALE;
    if (impl->state.visible_overflow) { *count = impl->state.visible_matches; return FINDUI_ERR_LIMIT; }
    size_t required = 0;
    for (size_t i = 0; i < impl->state.visible_matches; i++) if (intersects(impl->visible[i], start, end)) required++;
    *count = required;
    if (required > capacity) return FINDUI_ERR_LIMIT;
    size_t output = 0;
    for (size_t i = 0; i < impl->state.visible_matches; i++) {
        findui_range range = impl->visible[i];
        if (!intersects(range, start, end)) continue;
        if (range.start < start) range.start = start;
        if (range.end > end) range.end = end;
        ranges[output++] = range;
    }
    return FINDUI_OK;
}
findui_code findui_set_mutation_host(findui_panel *panel, const findui_mutation_host *host)
{
    findui_impl *impl = implementation(panel); findui_code code = editable(impl);
    if (code != FINDUI_OK) return code;
    if (host && (!host->preflight || !host->delete_ || !host->insert)) return FINDUI_ERR_ARGUMENT;
    impl->host = host ? *host : (findui_mutation_host){0};
    return FINDUI_OK;
}
static findui_code replacement_page(findui_impl *impl)
{
    if (impl->state.generation == UINT32_MAX) return FINDUI_ERR_LIMIT;
    cancel_jobs(impl); impl->state.generation++;
    impl->page_first = ((impl->replace_remaining - 1) / impl->config.match_capacity) * impl->config.match_capacity;
    impl->page_count = impl->replace_remaining - impl->page_first;
    impl->page_request = true; impl->page_ready = false; impl->page_error = FIND_OK;
    impl->pending_submit = true; impl->request_active = true;
    impl->window_request = false;
    findui_panel panel = {impl}; (void)findui_service(&panel);
    return FINDUI_OK;
}
static findui_code replace_begin(findui_panel *panel, undo_log *undo, uint64_t revision,
                                 size_t record_limit, uint64_t time_ns, const undo_state *before,
                                 const undo_state *after, bool all)
{
    findui_impl *impl = implementation(panel); findui_code code = editable(impl);
    if (code != FINDUI_OK) return code;
    if (invalidate_source(impl)) return FINDUI_ERR_STALE;
    if (!undo || !before || !after) return FINDUI_ERR_ARGUMENT;
    if (!impl->source || revision != impl->state.revision) return FINDUI_ERR_STALE;
    if (!impl->state.complete) return impl->state.searching ? FINDUI_ERR_BUSY : FINDUI_ERR_FIND;
    if (!impl->state.match_count) return FINDUI_OK;
    if (!all && impl->state.match_index == FINDUI_NO_INDEX) return FINDUI_ERR_BUSY;
    size_t count = all ? (size_t)impl->state.match_count : 1;
    undo_stats stats = undo_get_stats(undo);
    uint64_t extra = impl->state.options.regex ? piece_snapshot_len(impl->source) / FINDUI_REPLACE_BYTES : 0;
    if (extra > SIZE_MAX - count) return FINDUI_ERR_LIMIT;
    size_t slices = count + (size_t)extra;
    if (slices > (SIZE_MAX - count) / PIECE_REF_SPANS || stats.records > record_limit ||
        slices * PIECE_REF_SPANS + count > record_limit - stats.records) return FINDUI_ERR_LIMIT;
    if (impl->host.preflight) {
        int admission = impl->host.preflight(impl->host.user, slices + count,
                                             piece_snapshot_len(impl->source), impl->state.replacement_length);
        if (admission) { impl->state.undo_error = admission; return FINDUI_ERR_UNDO; }
    }
    int error = undo_group_begin(undo, before);
    impl->state.undo_error = error;
    if (error) return FINDUI_ERR_UNDO;
    cancel_jobs(impl); impl->state.searching = false;
    impl->undo = undo; impl->before = *before; impl->after = *after; impl->replace_time = time_ns;
    impl->replace_remaining = count; impl->replace_single = !all; impl->replace_changed = false;
    impl->replace_selected = impl->state.selected; impl->state.replacing = true;
    impl->replace_match_active = false;
    impl->replace_paged = all && impl->state.cache_overflow;
    impl->page_request = false;
    if (impl->replace_paged) return replacement_page(impl);
    return FINDUI_OK;
}
findui_code findui_replace_one(findui_panel *panel, undo_log *undo, uint64_t revision,
                              size_t record_limit, uint64_t time_ns, const undo_state *before,
                              const undo_state *after)
{ return replace_begin(panel, undo, revision, record_limit, time_ns, before, after, false); }
findui_code findui_replace_all(findui_panel *panel, undo_log *undo, uint64_t revision,
                              size_t record_limit, uint64_t time_ns, const undo_state *before,
                              const undo_state *after)
{ return replace_begin(panel, undo, revision, record_limit, time_ns, before, after, true); }
static findui_code replace_end(findui_impl *impl)
{
    cancel_jobs(impl);
    int error = undo_group_end(impl->undo, &impl->after);
    impl->page_request = false;
    impl->state.replacing = false; impl->undo = NULL; impl->replace_remaining = 0;
    if (impl->replace_changed) {
        retire_source(impl); clear_results(impl);
    }
    if (error) { impl->state.undo_error = error; return FINDUI_ERR_UNDO; }
    return FINDUI_OK;
}
findui_code findui_replace_step(findui_panel *panel, size_t match_budget, uint64_t deadline_ns, size_t *replaced)
{
    findui_impl *impl = implementation(panel);
    if (!impl || !replaced) return FINDUI_ERR_ARGUMENT;
    *replaced = 0;
    if (!impl->state.replacing) return FINDUI_OK;
    if (invalidate_source(impl)) {
        (void)replace_end(impl); impl->state.search_error = FIND_CANCELLED;
        return FINDUI_ERR_STALE;
    }
    while (impl->replace_remaining && *replaced < match_budget) {
        if (impl->replace_paged && !impl->page_ready) { (void)findui_service(panel); return FINDUI_MORE; }
        if (impl->replace_paged && impl->page_error != FIND_OK) { (void)replace_end(impl); return FINDUI_ERR_FIND; }
        if (deadline_ns && now_ns() >= deadline_ns) return FINDUI_MORE;
        if (!impl->replace_match_active) {
            impl->replace_range = impl->replace_single ? impl->replace_selected : impl->replace_paged ?
                impl->replace_page[impl->replace_remaining - 1 - impl->page_first] : impl->cache[impl->replace_remaining - 1];
            impl->replace_delete_remaining = impl->replace_range.end - impl->replace_range.start;
            impl->replace_match_active = true;
        }
        findui_range range = impl->replace_range;
        int error = 0;
        if (impl->replace_delete_remaining) {
            uint64_t bytes = impl->replace_delete_remaining < FINDUI_REPLACE_BYTES ?
                             impl->replace_delete_remaining : FINDUI_REPLACE_BYTES;
            uint64_t off = range.start + impl->replace_delete_remaining - bytes;
            findui_mutation_result result;
            if (impl->host.delete_)
                result = impl->host.delete_(impl->host.user, impl->undo, off, bytes,
                                            impl->replace_time, &impl->before, &impl->after);
            else {
                result.error = undo_delete(impl->undo, off, bytes, UNDO_DELETE, impl->replace_time,
                                           &impl->before, &impl->after);
                result.changed = result.error == 0;
            }
            error = result.error; impl->replace_changed |= result.changed;
            if (!error) {
                impl->replace_delete_remaining -= bytes;
                if (impl->replace_delete_remaining) return FINDUI_MORE;
            }
        }
        if (!error && impl->state.replacement_length) {
            if (deadline_ns && now_ns() >= deadline_ns) return FINDUI_MORE;
            findui_mutation_result result;
            if (impl->host.insert)
                result = impl->host.insert(impl->host.user, impl->undo, range.start, impl->replacement,
                                          impl->state.replacement_length, impl->replace_time, &impl->before, &impl->after);
            else {
                result.error = undo_insert(impl->undo, range.start, impl->replacement, impl->state.replacement_length,
                                           impl->replace_time, &impl->before, &impl->after);
                result.changed = result.error == 0;
            }
            error = result.error; impl->replace_changed |= result.changed;
        }
        if (error) {
            impl->state.undo_error = error;
            (void)replace_end(impl); return FINDUI_ERR_UNDO;
        }
        impl->replace_match_active = false;
        impl->replace_remaining--; (*replaced)++;
        if (impl->replace_paged && impl->replace_remaining && impl->replace_remaining == impl->page_first) {
            findui_code code = replacement_page(impl);
            if (code != FINDUI_OK) { (void)replace_end(impl); return code; }
            return FINDUI_MORE;
        }
    }
    return impl->replace_remaining ? FINDUI_MORE : replace_end(impl);
}
findui_code findui_replace_cancel(findui_panel *panel)
{
    findui_impl *impl = implementation(panel);
    if (!impl) return FINDUI_ERR_ARGUMENT;
    return impl->state.replacing ? replace_end(impl) : FINDUI_OK;
}
