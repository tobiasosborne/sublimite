#include "keys/keys.h"
#include "base/base.h"
#include <xkbcommon/xkbcommon-keysyms.h>

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
        ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1]<<8));
}
/* Linear reference is deliberately independent of production binary search.
 * Raw mode compares results/state; other modes exercise every binding,
 * normalization, arbitrary chord bytes, cancellation, and reset sequences. */
static void raw_oracle(keys_state before, uint32_t sym, uint16_t mods,
                       keys_result rc, const keys_binding *out, keys_state after)
{
    if (before.chord>=KEYS_CHORD_COUNT) before.chord=0;
    mods=(uint16_t)(mods & (uint16_t)~(PLAT_MOD_CAPS|PLAT_MOD_NUM));
    const keys_binding *want=NULL; size_t n; const keys_binding *table=keys_defaults(&n);
    for (size_t i=0;i<n;++i)
        if (table[i].args.chord==before.chord && table[i].keysym==sym && table[i].modifiers==mods)
            want=&table[i];
    if (want) {
        EDIT_ASSERT(out==want);
        EDIT_ASSERT(rc==(want->action==KEYS_ACTION_CHORD?KEYS_PREFIX:KEYS_MATCH));
        EDIT_ASSERT(after.chord==(want->action==KEYS_ACTION_CHORD?(uint8_t)want->args.value:0));
    } else {
        EDIT_ASSERT(!out && rc==(before.chord?KEYS_CANCELLED:KEYS_NONE));
        EDIT_ASSERT(after.chord==0);
    }
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    keys_state state={0}; size_t count; const keys_binding *table=keys_defaults(&count);
    for (size_t pos=0;size-pos>=7;pos+=7) {
        uint8_t mode=data[pos]; uint32_t sym=read_u32(data+pos+1);
        uint16_t mods=read_u16(data+pos+5);
        int oracle=0;
        if (mode&128u) state.chord=mode; /* corrupt state must never index OOB */
        switch ((mode&63u)%6u) {
        case 0: break; /* arbitrary keysym, modifiers, and sequence */
        case 1: {
            const keys_binding *b=&table[(size_t)sym%count];
            sym=b->keysym; mods=b->modifiers;
            if (mode&64u) state.chord=b->args.chord;
            oracle=1; break;
        }
        case 2: sym=XKB_KEY_a+(sym%26u); oracle=1; break;
        case 3: sym=XKB_KEY_k; mods=PLAT_MOD_CTRL; oracle=1; break;
        case 4: sym=(mode&64u)?XKB_KEY_Escape:XKB_KEY_u; mods=(uint16_t)(mods&PLAT_MOD_CTRL); oracle=1; break;
        default: keys_reset(&state); break;
        }
        /* Oracle modes use canonical table symbols; raw fuzz mode covers the
         * normalization cases separately, always checking output validity. */
        keys_state before=state; const keys_binding *out=NULL;
        keys_result rc=keys_lookup(&state,sym,mods,&out);
        EDIT_ASSERT(rc>=KEYS_NONE && rc<=KEYS_CANCELLED);
        EDIT_ASSERT(state.chord<KEYS_CHORD_COUNT);
        if (out) {
            int found=0;
            for(size_t i=0;i<count;++i) if(out==&table[i]) found=1;
            EDIT_ASSERT(found && (rc==KEYS_MATCH || rc==KEYS_PREFIX));
            EDIT_ASSERT(out->action>KEYS_ACTION_NONE && out->action<KEYS_ACTION_COUNT);
        } else EDIT_ASSERT(rc!=KEYS_MATCH && rc!=KEYS_PREFIX);
        if (oracle) raw_oracle(before,sym,mods,rc,out,state);
    }
    return 0;
}
