/* Model-based fuzzer for the fault registry (docs/decisions/
 * wave1-os-fault-lease.md): register, poll, retire and recover over page
 * extents of one private view, checked against a model of live and retired
 * ids. The rest of os forwards opaque bytes to Linux (v2-0fv.1.md, Law 1
 * exemption); the registry is the module's only mutable state. */
#include "os/os.h"

enum operation { REGISTER, RETIRE, RETIRE_STALE, POLL, POLL_STALE, TRIGGER, FORGE, MISALIGN };

/* Each byte gives three opcode bits and five argument bits. Three argument
 * bits select a start page and two a span of one to four pages; eight pages
 * also bound the live and retired sets (P: at most eight disjoint extents). */
enum { PAGES = 8, KEEP = 8 };

typedef struct live { os_fault_id id; size_t first, span; uint64_t epoch; } live;

static os_map view;
static uint64_t last_gen; /* Generations only grow, across inputs too. */

static void map_once(size_t ps)
{
    if (view.addr) return;
    os_file f = OS_FILE_INIT;
    ASSERT(os_file_open(&f, "/proc/self/exe", OS_OPEN_READ) == OS_OK);
    ASSERT(os_map_file(&view, &f, 0, PAGES * ps) == OS_OK);
    os_file_close(&f);
}

static bool expect_noent(os_fault_id id)
{
    uint64_t e = UINT64_C(0x5a5a5a5a5a5a5a5a);
    return os_fault_poll(id, &e) == OS_E_NOENT && e == UINT64_C(0x5a5a5a5a5a5a5a5a);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t len)
{
    size_t ps = os_page_size();
    map_once(ps);
    const unsigned char *base = view.addr;
    live lv[PAGES];
    size_t nlive = 0, nstale = 0;
    os_fault_id stale[KEEP];

    for (size_t i = 0; i < len; i++) {
        unsigned arg = data[i] & 31u;
        switch ((enum operation)(data[i] >> 5)) {
        case REGISTER: {
            size_t first = arg % PAGES, span = 1u + arg / PAGES;
            if (span > PAGES - first) span = PAGES - first;
            bool overlap = false;
            for (size_t k = 0; k < nlive; k++)
                overlap |= first < lv[k].first + lv[k].span && lv[k].first < first + span;
            os_map m = {base + first * ps, span * ps};
            os_fault_id id = {7, 7};
            os_err rc = os_fault_register(&m, &id);
            if (overlap) {
                ASSERT(rc == OS_E_EXIST && id.slot == 0 && id.gen == 0);
                break;
            }
            ASSERT(rc == OS_OK && id.gen > last_gen && id.slot < OS_FAULT_SLOTS);
            for (size_t k = 0; k < nlive; k++) ASSERT(lv[k].id.slot != id.slot);
            last_gen = id.gen;
            lv[nlive++] = (live){id, first, span, 0};
            break;
        }
        case RETIRE:
            if (!nlive) break;
            {
                size_t k = arg % nlive;
                ASSERT(os_fault_retire(lv[k].id) == OS_OK);
                stale[nstale++ % KEEP] = lv[k].id;
                lv[k] = lv[--nlive];
            }
            break;
        case RETIRE_STALE:
            if (nstale) ASSERT(os_fault_retire(stale[arg % (nstale < KEEP ? nstale : KEEP)]) == OS_E_NOENT);
            break;
        case POLL:
            if (nlive) {
                live *l = &lv[arg % nlive];
                uint64_t e = 0;
                ASSERT(os_fault_poll(l->id, &e) == OS_OK && e == l->epoch);
            }
            break;
        case POLL_STALE:
            if (nstale) ASSERT(expect_noent(stale[arg % (nstale < KEEP ? nstale : KEEP)]));
            break;
        case TRIGGER:
            if (nlive) {
                live *l = &lv[arg % nlive];
                size_t page = l->first + arg % l->span;
                os_map m = {base + l->first * ps, l->span * ps};
                ASSERT(os_fault_test_trigger(&m, (page - l->first) * ps + arg) == OS_OK);
                l->epoch++;
                const volatile unsigned char *v = base;
                for (size_t b = page * ps; b < (l->first + l->span) * ps; b++) ASSERT(v[b] == 0);
                uint64_t e = 0;
                ASSERT(os_fault_poll(l->id, &e) == OS_OK && e == l->epoch);
            }
            break;
        case FORGE: {
            /* A never-issued generation in any slot. */
            os_fault_id id = {arg * (OS_FAULT_SLOTS / 32u), last_gen + 1u + arg};
            ASSERT(expect_noent(id) && os_fault_retire(id) == OS_E_NOENT);
            break;
        }
        case MISALIGN: {
            os_map m = {base + 1u + arg, ps};
            os_fault_id id = {7, 7};
            ASSERT(os_fault_register(&m, &id) == OS_E_INVAL && id.gen == 0);
            break;
        }
        }
    }
    while (nlive) ASSERT(os_fault_retire(lv[--nlive].id) == OS_OK);
    return 0;
}
