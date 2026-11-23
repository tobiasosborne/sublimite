/* Model-based fuzzer for the fault registry (docs/decisions/
 * wave1-os-fault-lease.md): register, poll, retire and real faults over page
 * extents of one private view of an empty file, checked against a model of
 * live and retired ids and of the pages already recovered. The rest of os
 * forwards opaque bytes to Linux (v2-0fv.1.md, Law 1 exemption); the
 * registry is the module's only mutable state. Misuse is an ASSERT inside
 * os (A15), so the model issues only valid calls. */
#include "os/os.h"

enum operation { REGISTER, RETIRE, POLL, POLL_STALE, FAULT, OPERATIONS };

/* Each byte gives three opcode bits (taken modulo OPERATIONS) and five
 * argument bits. Three argument bits select a start page and two a span of
 * one to four pages; eight pages also bound the live and retired sets (P: at
 * most eight disjoint extents). */
enum { PAGES = 8, KEEP = 8 };

typedef struct live { os_fault_id id; size_t first, span; uint64_t epoch; } live;

static os_file empty = OS_FILE_INIT; /* Every page of a view of it faults. */
static uint64_t last_gen;            /* Generations only grow, across inputs too. */

static bool expect_noent(os_fault_id id)
{
    uint64_t e = UINT64_C(0x5a5a5a5a5a5a5a5a);
    return os_fault_poll(id, &e) == OS_E_NOENT && e == UINT64_C(0x5a5a5a5a5a5a5a5a);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t len)
{
    size_t ps = os_page_size();
    if (!os_file_is_open(&empty)) {
        os_dir d = OS_DIR_INIT;
        os_err rc = os_dir_open(&d, "build");
        ASSERT(rc == OS_OK);
        rc = os_file_open_anon(&empty, &d);
        ASSERT(rc == OS_OK);
        os_dir_close(&d);
    }
    os_map view = {0};
    os_err mapped = os_map_file(&view, &empty, 0, PAGES * ps);
    ASSERT(mapped == OS_OK);
    const unsigned char *base = view.addr;
    bool zero[PAGES] = {false}; /* Recovered: anonymous zero pages. */
    live lv[PAGES];
    size_t nlive = 0, nstale = 0;
    os_fault_id stale[KEEP];

    for (size_t i = 0; i < len; i++) {
        unsigned arg = data[i] & 31u;
        switch ((enum operation)((data[i] >> 5) % OPERATIONS)) {
        case REGISTER: {
            size_t first = arg % PAGES, span = 1u + arg / PAGES;
            if (span > PAGES - first) span = PAGES - first;
            bool overlap = false;
            for (size_t k = 0; k < nlive; k++)
                overlap |= first < lv[k].first + lv[k].span && lv[k].first < first + span;
            if (overlap) break; /* Misuse: os would ASSERT. */
            os_map m = {base + first * ps, span * ps};
            os_fault_id id = {7, 7};
            os_err rc = os_fault_register(&m, &id);
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
                os_fault_retire(lv[k].id);
                stale[nstale++ % KEEP] = lv[k].id;
                lv[k] = lv[--nlive];
            }
            break;
        case POLL:
            if (nlive) {
                live *l = &lv[arg % nlive];
                uint64_t e = 0;
                os_err rc = os_fault_poll(l->id, &e);
                ASSERT(rc == OS_OK && e == l->epoch);
            }
            break;
        case POLL_STALE:
            if (nstale) {
                bool gone = expect_noent(stale[arg % (nstale < KEEP ? nstale : KEEP)]);
                ASSERT(gone);
            }
            break;
        case FAULT:
            if (nlive) {
                /* A read past the end of the empty file faults unless the
                 * page was already recovered; recovery zeroes through the
                 * extent's end. */
                live *l = &lv[arg % nlive];
                size_t page = l->first + arg % l->span;
                const volatile unsigned char *v = base;
                unsigned char got = v[page * ps + arg];
                ASSERT(got == 0);
                if (!zero[page]) {
                    l->epoch++;
                    for (size_t p = page; p < l->first + l->span; p++) zero[p] = true;
                }
                uint64_t e = 0;
                os_err rc = os_fault_poll(l->id, &e);
                ASSERT(rc == OS_OK && e == l->epoch);
            }
            break;
        case OPERATIONS:
            ASSERT(false && "operation out of range");
            break;
        }
    }
    while (nlive) os_fault_retire(lv[--nlive].id);
    os_unmap(&view);
    return 0;
}
