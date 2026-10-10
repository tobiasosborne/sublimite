#include "../prewake.h"
#include <xkbcommon/xkbcommon-keysyms.h>
static bool prewake_trigger(const plat_event *event)
{
    if (event->kind==PLAT_EV_FOCUS) return event->focused;
    if (event->kind==PLAT_EV_MOTION) return true;
    if (event->kind!=PLAT_EV_KEY || !event->press || event->repeat) return false;
    switch (event->keysym) {
    case XKB_KEY_Shift_L: case XKB_KEY_Shift_R:
    case XKB_KEY_Control_L: case XKB_KEY_Control_R:
    case XKB_KEY_Alt_L: case XKB_KEY_Alt_R:
    case XKB_KEY_Super_L: case XKB_KEY_Super_R:
    case XKB_KEY_ISO_Level3_Shift: return true;
    default: return false;
    }
}
int prewake_hint(prewake_state *s,const plat_event *event,uint64_t now,const prewake_ops *ops)
{
    if (s==NULL || event==NULL || ops==NULL || ops->warm==NULL || ops->spin==NULL) return -1;
    if (now<s->last_activity_ns || now-s->last_activity_ns<UINT64_C(15000000000) || !prewake_trigger(event)) return 0;
    s->last_activity_ns=now;
    int rc=ops->warm(ops->ctx);
    if (rc!=0) return rc;
    return ops->spin(ops->ctx,UINT64_C(200000));
}
void prewake_activity(prewake_state *s,uint64_t now)
{ if (s!=NULL) s->last_activity_ns=now; }
