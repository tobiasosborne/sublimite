#include "prewake.h"
#include <stdio.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#ifndef PREWAKE_VARIANT
#define PREWAKE_VARIANT 0
#endif
#include PREWAKE_IMPL
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"prewake_test:%d: FAIL %s\n",__LINE__,#c); return 1; } } while (0)
typedef struct fake { unsigned warm; uint64_t spin; int error; } fake;
static int prewake_contract_warm(void *p) { fake *f=p; f->warm++; return f->error; }
static int prewake_contract_spin(void *p,uint64_t ns) { fake *f=p; f->spin+=ns; return 0; }
int main(void)
{
    fake f={0}; prewake_ops ops={&f,prewake_contract_warm,prewake_contract_spin};
    prewake_state s={0}; uint64_t idle=UINT64_C(15000000000);
    plat_event focus={.kind=PLAT_EV_FOCUS,.focused=true};
    plat_event motion={.kind=PLAT_EV_MOTION};
    plat_event modifier={.kind=PLAT_EV_KEY,.press=true,.keysym=XKB_KEY_Shift_L};
    plat_event key={.kind=PLAT_EV_KEY,.press=true,.keysym=XKB_KEY_a};
    CHECK(prewake_hint(&s,&focus,idle-1,&ops)==0 && f.warm==0);
    CHECK(prewake_hint(&s,&key,idle,&ops)==0 && f.warm==0);
    const plat_event *events[]={&focus,&motion,&modifier};
    for (size_t i=0;i<3;i++) {
        s.last_activity_ns=0; f=(fake){0};
        CHECK(prewake_hint(&s,events[i],idle,&ops)==0);
        CHECK(f.warm==(PREWAKE_VARIANT==0 ? 0u : 1u));
        CHECK(f.spin==(PREWAKE_VARIANT==0 ? 0 : PREWAKE_VARIANT==1 ? UINT64_C(200000) : UINT64_C(100200000)));
        CHECK(prewake_hint(&s,events[i],idle+1,&ops)==0);
        CHECK(f.warm==(PREWAKE_VARIANT==0 ? 0u : 1u));
    }
    s.last_activity_ns=0; f=(fake){.error=-7};
    CHECK(prewake_hint(&s,&focus,idle,&ops)==(PREWAKE_VARIANT==0 ? 0 : -7));
    CHECK(f.spin==0);
    focus.focused=false; modifier.press=false;
    s.last_activity_ns=0; f=(fake){0};
    CHECK(prewake_hint(&s,&focus,idle,&ops)==0 && f.warm==0);
    CHECK(prewake_hint(&s,&modifier,idle,&ops)==0 && f.warm==0);
    prewake_activity(&s,idle); CHECK(s.last_activity_ns==idle);
    CHECK(prewake_hint(NULL,&motion,idle,&ops)<0);
    printf("prewake_test: variant=%c PASS idle threshold, three triggers, debounce, errors, spin budget\n",'a'+PREWAKE_VARIANT);
    return 0;
}
