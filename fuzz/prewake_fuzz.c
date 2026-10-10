/* Existing A/B/C hint policies against an independent event/clock model. */
#include "base/base.h"
#include "../bench/prewake/prewake.h"
#include <xkbcommon/xkbcommon-keysyms.h>
#define prewake_hint prewake_hint_a
#define prewake_activity prewake_activity_a
#include "../bench/prewake/a/prewake.c"
#undef prewake_hint
#undef prewake_activity
#define prewake_hint prewake_hint_b
#define prewake_activity prewake_activity_b
#define prewake_trigger prewake_trigger_b
#include "../bench/prewake/b/prewake.c"
#undef prewake_hint
#undef prewake_activity
#undef prewake_trigger
#define prewake_hint prewake_hint_c
#define prewake_activity prewake_activity_c
#define prewake_trigger prewake_trigger_c
#include "../bench/prewake/c/prewake.c"
#undef prewake_hint
#undef prewake_activity
#undef prewake_trigger
typedef struct prewake_probe { unsigned calls; uint64_t budget; int warm_error,spin_error; } prewake_probe;
static int prewake_probe_warm(void *ctx)
{ prewake_probe *p=ctx; p->calls++; return p->warm_error; }
static int prewake_probe_spin(void *ctx,uint64_t ns)
{ prewake_probe *p=ctx; p->budget=ns; return p->spin_error; }
int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size)
{
    int (*hint[3])(prewake_state *,const plat_event *,uint64_t,const prewake_ops *)={prewake_hint_a,prewake_hint_b,prewake_hint_c};
    void (*activity[3])(prewake_state *,uint64_t)={prewake_activity_a,prewake_activity_b,prewake_activity_c};
    prewake_state states[3]={{0},{0},{0}};
    uint64_t model[3]={0},now=0;
    const uint64_t deltas[]={0,1,UINT64_C(14999999999),UINT64_C(15000000000),UINT64_C(15000000001)};
    const uint32_t keys[]={XKB_KEY_Shift_L,XKB_KEY_Shift_R,XKB_KEY_Control_L,XKB_KEY_Control_R,
        XKB_KEY_Alt_L,XKB_KEY_Alt_R,XKB_KEY_Super_L,XKB_KEY_Super_R,XKB_KEY_ISO_Level3_Shift,XKB_KEY_a};
    for (size_t i=0;size-i>=5;i+=5) {
        uint64_t delta=deltas[data[i]%5u];
        now=(data[i]&128u) ? (now>=delta ? now-delta : 0) : now+delta;
        uint8_t kind=data[i+1]%5u,key=data[i+3]%10u;
        plat_event event={.kind=kind==0 ? PLAT_EV_FOCUS : kind==1 ? PLAT_EV_MOTION :
            kind==2 ? PLAT_EV_KEY : kind==3 ? PLAT_EV_BUTTON : PLAT_EV_EXPOSE,
            .focused=(data[i+2]&1u)!=0,.press=(data[i+2]&2u)!=0,
            .repeat=(data[i+2]&4u)!=0,.keysym=keys[key]};
        bool trigger=(kind==0 && event.focused) || kind==1 ||
            (kind==2 && event.press && !event.repeat && key<9);
        for (unsigned v=0;v<3;v++) {
            if (data[i+4]&1u) { activity[v](&states[v],now); model[v]=now; }
            bool eligible=v!=0 && now>=model[v] && now-model[v]>=UINT64_C(15000000000) && trigger;
            prewake_probe p={.warm_error=(data[i+4]&2u) ? -7 : 0,.spin_error=(data[i+4]&4u) ? -9 : 0};
            prewake_ops ops={&p,prewake_probe_warm,prewake_probe_spin};
            int expected=eligible ? (p.warm_error ? p.warm_error : p.spin_error) : 0;
            EDIT_ASSERT(hint[v](&states[v],&event,now,&ops)==expected);
            EDIT_ASSERT(p.calls==(eligible ? 1u : 0u));
            uint64_t budget=eligible && !p.warm_error ? (v==1 ? UINT64_C(200000) : UINT64_C(100200000)) : 0;
            EDIT_ASSERT(p.budget==budget);
            if (eligible) model[v]=now;
            EDIT_ASSERT(states[v].last_activity_ns==model[v]);
        }
    }
    return 0;
}
