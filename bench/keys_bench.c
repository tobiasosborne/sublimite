#include "keys/keys.h"
#include "base/base.h"
#include "harness.h"
#include <xkbcommon/xkbcommon-keysyms.h>

#define EVENTS 1000000u
#define GATE_NS UINT64_C(1000)

static uint32_t random_next(uint32_t *seed)
{
    *seed^=*seed<<13; *seed^=*seed>>17; *seed^=*seed<<5; return *seed;
}
int main(void)
{
    char power[32], load[32]="unknown";
    bench_battery_status(power,sizeof power);
    FILE *f=fopen("/proc/loadavg","r");
    if (f) { if (fscanf(f,"%31s",load)!=1) strcpy(load,"unknown"); fclose(f); }
    const char *tag=bench__tag_from_power(power);
    printf("POWER status=%s %s load1=%s; keys measurements TRACK only\n",power,tag,load);
    edit_arena arena;
    if (edit_arena_init(&arena,EVENTS*sizeof(uint64_t))!=0) return 2;
    uint64_t *times=edit_arena_alloc(&arena,EVENTS*sizeof(*times),_Alignof(uint64_t));
    if (!times) { edit_arena_free(&arena); return 2; }
    size_t count; const keys_binding *table=keys_defaults(&count);
    size_t roots=0;
    while (roots<count && table[roots].args.chord==KEYS_CHORD_NONE) ++roots;
    EDIT_ASSERT(roots>0);
    size_t results[KEYS_ERR_ARG+1]={0}; keys_state state={0};
    uint32_t seed=UINT32_C(0x4b455953);
    (void)bench_now_ns(); /* resolve clock path before the allocation guard */
    edit_malloc_guard_begin();
    for (size_t i=0;i<EVENTS;++i) {
        uint32_t r=random_next(&seed), sym; uint16_t mods=0;
        switch (i%16u) {
        case 0: {
            const keys_binding *b=&table[(size_t)r%roots]; sym=b->keysym; mods=b->modifiers; break;
        }
        case 1: sym=r; mods=(uint16_t)(r>>16); break;
        case 2: sym=XKB_KEY_D; mods=PLAT_MOD_CTRL|PLAT_MOD_SHIFT|PLAT_MOD_CAPS; break;
        case 3: sym=XKB_KEY_Left; break;
        case 4: case 7: case 14: sym=XKB_KEY_k; mods=PLAT_MOD_CTRL; break;
        case 5: sym=XKB_KEY_u; mods=PLAT_MOD_CTRL; break;
        case 6: sym=XKB_KEY_Tab; break;
        case 8: sym=UINT32_MAX; break;
        case 9: sym=XKB_KEY_j; mods=PLAT_MOD_CTRL; break;
        case 10: sym=XKB_KEY_Left; mods=PLAT_MOD_CTRL; break;
        case 11: sym=XKB_KEY_question; mods=PLAT_MOD_CTRL|PLAT_MOD_SHIFT; break;
        case 12: sym=XKB_KEY_a+(r%26u); break;
        case 13: sym=XKB_KEY_a; mods=UINT16_MAX; break;
        default: sym=XKB_KEY_Escape; break;
        }
        const keys_binding *out=NULL;
        uint64_t start=bench_now_ns();
        keys_result rc=keys_lookup(&state,sym,mods,&out);
        times[i]=bench_now_ns()-start;
        EDIT_ASSERT(rc>=KEYS_NONE && rc<=KEYS_CANCELLED);
        ++results[rc];
        if (rc==KEYS_MATCH || rc==KEYS_PREFIX) EDIT_ASSERT(out);
    }
    size_t mallocs=edit_malloc_guard_end();
    bench_samples samples; bench_samples_init(&samples,times,EVENTS); samples.n=EVENTS;
    uint64_t p50=bench_p50(&samples), p99=bench_p99(&samples);
    int fail=p99>GATE_NS || mallocs!=0 || !edit_malloc_guard_active();
    printf("BENCH keys_lookup TRACK n=%u p50=%.3f us p99=%.3f us (M)%s load1=%s "
           "gate_p99<=1.000 us(G) pass=%d\n",EVENTS,(double)p50/1000.0,(double)p99/1000.0,tag,load,fail?0:1);
    printf("STREAM (M)%s load1=%s matches=%zu prefixes=%zu cancelled=%zu misses=%zu "
           "mallocs=%zu guard=%s gate_mallocs=0(G)\n",tag,load,results[KEYS_MATCH],results[KEYS_PREFIX],
           results[KEYS_CANCELLED],results[KEYS_NONE],mallocs,edit_malloc_guard_active()?"active":"inactive");
    edit_arena_free(&arena); return fail;
}
