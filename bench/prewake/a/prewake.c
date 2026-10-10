#include "../prewake.h"
int prewake_hint(prewake_state *s,const plat_event *event,uint64_t now,const prewake_ops *ops)
{
    (void)now;
    return s==NULL || event==NULL || ops==NULL ? -1 : 0;
}
void prewake_activity(prewake_state *s,uint64_t now)
{ if (s!=NULL) s->last_activity_ns=now; }
