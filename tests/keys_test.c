#include "keys/keys.h"
#include "base/base.h"
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) EDIT_ASSERT(x)
#define U KEYS_ARG_UNIMPLEMENTED
#define E KEYS_ARG_EXTEND
#define T(k,a,f,v) {k,KEYS_ACTION_##a,f,v}
typedef struct expectation {
    const char *sequence;
    keys_action action;
    uint16_t flags;
    int16_t value;
} expectation;
/* Independently transcribed semantic expectations from the installed Linux
 * defaults, in source/command order, not keys.c order. No table-generated
 * fixtures or shared binding macros. Later duplicate defaults win. UI and
 * grammar contexts are outside the event-only interface; ordinary editor
 * Ctrl+Enter and default Shift+Tab unindent are explicitly included. */
static const expectation expected[] = {
    T("ctrl+q", EXIT,U,0),
    T("ctrl+shift+n", NEW_WINDOW,U,0),
    T("ctrl+shift+w", CLOSE_WINDOW,U,0),
    T("XF86Open", OPEN_FILE,U,0),
    T("ctrl+o", OPEN_FILE,U,0),
    T("ctrl+shift+t", REOPEN_FILE,U,0),
    T("alt+o", SWITCH_FILE,U,0),
    T("alt+shift+o", SWITCH_FILE,U|KEYS_ARG_SIDE_BY_SIDE,0),
    T("ctrl+n", NEW_FILE,U,0),
    T("XF86Save", SAVE,U,0),
    T("ctrl+s", SAVE,U,0),
    T("ctrl+shift+s", SAVE_AS,U,0),
    T("ctrl+F4", CLOSE_FILE,U,0),
    T("XF86Close", CLOSE,U,0),
    T("ctrl+w", CLOSE,U,0),
    T("ctrl+k ctrl+b", TOGGLE_SIDEBAR,U,0),
    T("F11", FULL_SCREEN,U,0),
    T("shift+F11", DISTRACTION_FREE,U,0),
    T("BackSpace", BACKSPACE,0,0),
    T("shift+BackSpace", BACKSPACE,0,0),
    T("Delete", DELETE,0,0),
    T("Return", INSERT,0,10),
    T("shift+Return", INSERT,0,10),
    T("KP_Enter", INSERT,0,10),
    T("shift+KP_Enter", INSERT,0,10),
    T("Undo", UNDO,U,0),
    T("ctrl+z", UNDO,U,0),
    T("ctrl+shift+z", REDO,U,0),
    T("Redo", REDO_OR_REPEAT,U,0),
    T("ctrl+y", REDO_OR_REPEAT,U,0),
    T("ctrl+u", SOFT_UNDO,U,0),
    T("ctrl+shift+u", SOFT_REDO,U,0),
    T("shift+Delete", CUT,U,0),
    T("ctrl+Insert", COPY,U,0),
    T("shift+Insert", PASTE,U,0),
    T("XF86Cut", CUT,U,0),
    T("ctrl+x", CUT,U,0),
    T("XF86Copy", COPY,U,0),
    T("ctrl+c", COPY,U,0),
    T("XF86Paste", PASTE,U,0),
    T("ctrl+v", PASTE,U,0),
    T("ctrl+shift+v", PASTE_AND_INDENT,U,0),
    T("ctrl+k ctrl+v", PASTE_HISTORY,U,0),
    T("Left", LEFT,0,0),
    T("Right", RIGHT,0,0),
    T("Up", UP,0,0),
    T("Down", DOWN,0,0),
    T("shift+Left", LEFT,E,0),
    T("shift+Right", RIGHT,E,0),
    T("shift+Up", UP,E,0),
    T("shift+Down", DOWN,E,0),
    T("ctrl+Left", WORD_LEFT,0,0),
    T("ctrl+Right", WORD_RIGHT,0,0),
    T("ctrl+shift+Left", WORD_LEFT,E,0),
    T("ctrl+shift+Right", WORD_RIGHT,E,0),
    T("alt+Left", SUBWORD_LEFT,U,0),
    T("alt+Right", SUBWORD_RIGHT,U,0),
    T("alt+shift+Left", SUBWORD_LEFT,U|E,0),
    T("alt+shift+Right", SUBWORD_RIGHT,U|E,0),
    T("alt+shift+Up", COLUMN_SELECTION,U,-1),
    T("alt+shift+Down", COLUMN_SELECTION,U,1),
    T("Prior", PAGE_UP,0,0),
    T("Next", PAGE_DOWN,0,0),
    T("shift+Prior", PAGE_UP,E,0),
    T("shift+Next", PAGE_DOWN,E,0),
    T("Home", HOME,0,0),
    T("End", END,0,0),
    T("shift+Home", HOME,E,0),
    T("shift+End", END,E,0),
    T("ctrl+Home", DOC_HOME,0,0),
    T("ctrl+End", DOC_END,0,0),
    T("ctrl+shift+Home", DOC_HOME,E,0),
    T("ctrl+shift+End", DOC_END,E,0),
    T("ctrl+Up", SCROLL_LINES,U,1),
    T("ctrl+Down", SCROLL_LINES,U,-1),
    T("ctrl+Next", NEXT_VIEW,U,0),
    T("ctrl+shift+Next", NEXT_VIEW,U|E,0),
    T("ctrl+Prior", PREVIOUS_VIEW,U,0),
    T("ctrl+shift+Prior", PREVIOUS_VIEW,U|E,0),
    T("ctrl+Tab", NEXT_VIEW_STACK,U,0),
    T("ctrl+shift+Tab", PREVIOUS_VIEW_STACK,U,0),
    T("ctrl+a", SELECT_ALL,0,0),
    T("ctrl+shift+l", SPLIT_SELECTION_LINES,U,0),
    T("Escape", CANCEL,U,0),
    T("Tab", INSERT,0,9),
    T("shift+Tab", UNINDENT,U,0),
    T("ctrl+bracketright", INDENT,U,0),
    T("ctrl+k ctrl+bracketright", REINDENT,U|KEYS_ARG_SINGLE_LINE,0),
    T("ctrl+bracketleft", UNINDENT,U,0),
    T("Insert", TOGGLE_OVERWRITE,U,0),
    T("ctrl+l", SELECT_LINE,0,0),
    T("alt+l", SELECT_PREVIOUS_LINE,U,0),
    T("ctrl+d", ADD_NEXT_OCCURRENCE,U,0),
    T("ctrl+k ctrl+d", SKIP_NEXT_OCCURRENCE,U,0),
    T("ctrl+shift+space", SELECT_SCOPE,U,0),
    T("ctrl+shift+a", SELECT_SMART,U,0),
    T("ctrl+shift+m", SELECT_BRACKETS,U,0),
    T("ctrl+m", GOTO_BRACKET,U,0),
    T("alt+period", CLOSE_TAG,U,0),
    T("ctrl+alt+q", RECORD_MACRO,U,0),
    T("ctrl+alt+shift+q", RUN_MACRO,U,0),
    T("ctrl+KP_Enter", INSERT_LINE_AFTER,U,0),
    T("ctrl+shift+KP_Enter", INSERT_LINE_BEFORE,U,0),
    T("ctrl+Return", INSERT_LINE_AFTER,U,0),
    T("ctrl+shift+Return", INSERT_LINE_BEFORE,U,0),
    T("ctrl+p", GOTO_FILE,U,0),
    T("ctrl+shift+p", COMMAND_PALETTE,U,0),
    T("ctrl+alt+p", SELECT_WORKSPACE,U,0),
    T("ctrl+r", GOTO_SYMBOL,U,0),
    T("ctrl+g", GOTO_LINE,U,0),
    T("ctrl+semicolon", GOTO_WORD,U,0),
    T("F12", GOTO_DEFINITION,U,0),
    T("ctrl+F12", GOTO_DEFINITION,U|KEYS_ARG_SIDE_BY_SIDE,0),
    T("shift+F12", GOTO_REFERENCE,U,0),
    T("ctrl+shift+F12", GOTO_REFERENCE,U|KEYS_ARG_SIDE_BY_SIDE,0),
    T("ctrl+shift+r", GOTO_PROJECT_SYMBOL,U,0),
    T("alt+minus", JUMP_BACK,U,0),
    T("alt+shift+minus", JUMP_FORWARD,U,0),
    T("alt+KP_Subtract", JUMP_BACK,U,0),
    T("alt+shift+KP_Subtract", JUMP_FORWARD,U,0),
    T("ctrl+i", INCREMENTAL_FIND,U,0),
    T("ctrl+shift+i", INCREMENTAL_FIND,U|KEYS_ARG_REVERSE,0),
    T("Find", FIND,U,0),
    T("ctrl+f", FIND,U,0),
    T("ctrl+h", REPLACE,U,0),
    T("ctrl+shift+h", REPLACE_NEXT,U,0),
    T("F3", FIND_NEXT,U,0),
    T("shift+F3", FIND_PREVIOUS,U,0),
    T("ctrl+F3", FIND_UNDER,U,0),
    T("ctrl+shift+F3", FIND_UNDER_PREVIOUS,U,0),
    T("alt+F3", FIND_ALL_UNDER,U,0),
    T("ctrl+e", SLURP_FIND,U,0),
    T("ctrl+shift+e", SLURP_REPLACE,U,0),
    T("ctrl+shift+f", FIND_IN_FILES,U,0),
    T("F4", NEXT_RESULT,U,0),
    T("shift+F4", PREVIOUS_RESULT,U,0),
    T("ctrl+period", NEXT_MODIFICATION,U,0),
    T("ctrl+comma", PREVIOUS_MODIFICATION,U,0),
    T("ctrl+k ctrl+z", REVERT_HUNK,U,0),
    T("ctrl+k ctrl+shift+z", REVERT_MODIFICATION,U,0),
    T("ctrl+k ctrl+slash", INLINE_DIFF,U,0),
    T("ctrl+k ctrl+semicolon", INLINE_DIFF,U|KEYS_ARG_PREFER_HIDE,0),
    T("F6", SPELL_CHECK,U,0),
    T("ctrl+F6", NEXT_MISSPELLING,U,0),
    T("ctrl+shift+F6", PREVIOUS_MISSPELLING,U,0),
    T("ctrl+shift+Up", MOVE_LINE_UP,U,0),
    T("ctrl+shift+Down", MOVE_LINE_DOWN,U,0),
    T("ctrl+BackSpace", WORD_BACKSPACE,0,0),
    T("ctrl+shift+BackSpace", DELETE_TO_BOL,U,0),
    T("ctrl+Delete", WORD_DELETE,0,0),
    T("ctrl+shift+Delete", DELETE_TO_EOL,U,0),
    T("ctrl+slash", TOGGLE_COMMENT,U,0),
    T("ctrl+shift+slash", TOGGLE_COMMENT,U|KEYS_ARG_BLOCK,0),
    T("ctrl+j ctrl+j", PRIMARY_J_CHANGED,U,0),
    T("ctrl+shift+j", JOIN_LINES,U,0),
    T("ctrl+shift+d", DUPLICATE_LINE,U,0),
    T("ctrl+grave", CONSOLE,U,0),
    T("alt+slash", AUTO_COMPLETE,U,0),
    T("ctrl+space", AUTO_COMPLETE,U,0),
    T("ctrl+alt+shift+p", SHOW_SCOPE,U,0),
    T("F7", BUILD,U,0),
    T("ctrl+b", BUILD,U,0),
    T("ctrl+shift+b", BUILD,U|KEYS_ARG_SELECT,0),
    T("ctrl+Break", CANCEL_BUILD,U,0),
    T("ctrl+t", TRANSPOSE,U,0),
    T("F9", SORT_LINES,U,0),
    T("ctrl+F9", SORT_LINES,U|KEYS_ARG_CASE_SENSITIVE,0),
    T("alt+shift+1", SET_LAYOUT,U,1),
    T("alt+shift+2", SET_LAYOUT,U,2),
    T("alt+shift+3", SET_LAYOUT,U,3),
    T("alt+shift+4", SET_LAYOUT,U,4),
    T("alt+shift+8", SET_LAYOUT,U,8),
    T("alt+shift+9", SET_LAYOUT,U,9),
    T("alt+shift+5", SET_LAYOUT,U,5),
    T("ctrl+1", FOCUS_GROUP,U,0),
    T("ctrl+2", FOCUS_GROUP,U,1),
    T("ctrl+3", FOCUS_GROUP,U,2),
    T("ctrl+4", FOCUS_GROUP,U,3),
    T("ctrl+5", FOCUS_GROUP,U,4),
    T("ctrl+6", FOCUS_GROUP,U,5),
    T("ctrl+7", FOCUS_GROUP,U,6),
    T("ctrl+8", FOCUS_GROUP,U,7),
    T("ctrl+9", FOCUS_GROUP,U,8),
    T("ctrl+shift+1", MOVE_TO_GROUP,U,0),
    T("ctrl+shift+2", MOVE_TO_GROUP,U,1),
    T("ctrl+shift+3", MOVE_TO_GROUP,U,2),
    T("ctrl+shift+4", MOVE_TO_GROUP,U,3),
    T("ctrl+shift+5", MOVE_TO_GROUP,U,4),
    T("ctrl+shift+6", MOVE_TO_GROUP,U,5),
    T("ctrl+shift+7", MOVE_TO_GROUP,U,6),
    T("ctrl+shift+8", MOVE_TO_GROUP,U,7),
    T("ctrl+shift+9", MOVE_TO_GROUP,U,8),
    T("ctrl+0", FOCUS_SIDEBAR,U,0),
    T("ctrl+k ctrl+Up", NEW_PANE,U,0),
    T("ctrl+k ctrl+shift+Up", NEW_PANE,U|KEYS_ARG_NO_MOVE,0),
    T("ctrl+k ctrl+Down", CLOSE_PANE,U,0),
    T("ctrl+k ctrl+Left", FOCUS_NEIGHBOR_GROUP,U,-1),
    T("ctrl+k ctrl+Right", FOCUS_NEIGHBOR_GROUP,U,1),
    T("ctrl+k ctrl+shift+Left", MOVE_NEIGHBOR_GROUP,U,-1),
    T("ctrl+k ctrl+shift+Right", MOVE_NEIGHBOR_GROUP,U,1),
    T("alt+1", SELECT_TAB,U,0),
    T("alt+2", SELECT_TAB,U,1),
    T("alt+3", SELECT_TAB,U,2),
    T("alt+4", SELECT_TAB,U,3),
    T("alt+5", SELECT_TAB,U,4),
    T("alt+6", SELECT_TAB,U,5),
    T("alt+7", SELECT_TAB,U,6),
    T("alt+8", SELECT_TAB,U,7),
    T("alt+9", SELECT_LAST_TAB,U,0),
    T("ctrl+j ctrl+Up", UNSELECT_OTHERS,U,0),
    T("ctrl+j ctrl+Left", UNSELECT_LEFT,U,0),
    T("ctrl+j ctrl+Right", UNSELECT_RIGHT,U,0),
    T("ctrl+j ctrl+shift+Left", SELECT_LEFT,U,0),
    T("ctrl+j ctrl+shift+Right", SELECT_RIGHT,U,0),
    T("ctrl+j ctrl+Prior", FOCUS_LEFT,U,0),
    T("ctrl+j ctrl+Next", FOCUS_RIGHT,U,0),
    T("F2", NEXT_BOOKMARK,U,0),
    T("shift+F2", PREVIOUS_BOOKMARK,U,0),
    T("ctrl+F2", TOGGLE_BOOKMARK,U,0),
    T("ctrl+shift+F2", CLEAR_BOOKMARKS,U,0),
    T("alt+F2", SELECT_BOOKMARKS,U,0),
    T("ctrl+shift+k", DELETE_LINE,U,0),
    T("alt+q", WRAP_LINES,U,0),
    T("ctrl+k ctrl+u", UPPER_CASE,U,0),
    T("ctrl+k ctrl+l", LOWER_CASE,U,0),
    T("ctrl+k ctrl+space", SET_MARK,U,0),
    T("ctrl+k ctrl+a", SELECT_TO_MARK,U,0),
    T("ctrl+k ctrl+w", DELETE_TO_MARK,U,0),
    T("ctrl+k ctrl+x", SWAP_MARK,U,0),
    T("ctrl+k ctrl+y", YANK,U,0),
    T("ctrl+k ctrl+k", DELETE_TO_EOL,U,0),
    T("ctrl+k ctrl+BackSpace", DELETE_TO_BOL,U,0),
    T("ctrl+k ctrl+g", CLEAR_MARK,U,0),
    T("ctrl+k ctrl+c", SHOW_AT_CENTER,U,0),
    T("ctrl+plus", FONT_SIZE,U,1),
    T("ctrl+equal", FONT_SIZE,U,1),
    T("ctrl+minus", FONT_SIZE,U,-1),
    T("alt+shift+w", XML_TAG_SNIPPET,U,0),
    T("ctrl+shift+bracketleft", FOLD,U,0),
    T("ctrl+shift+bracketright", UNFOLD,U,0),
    T("ctrl+k ctrl+1", FOLD_LEVEL,U,1),
    T("ctrl+k ctrl+2", FOLD_LEVEL,U,2),
    T("ctrl+k ctrl+3", FOLD_LEVEL,U,3),
    T("ctrl+k ctrl+4", FOLD_LEVEL,U,4),
    T("ctrl+k ctrl+5", FOLD_LEVEL,U,5),
    T("ctrl+k ctrl+6", FOLD_LEVEL,U,6),
    T("ctrl+k ctrl+7", FOLD_LEVEL,U,7),
    T("ctrl+k ctrl+8", FOLD_LEVEL,U,8),
    T("ctrl+k ctrl+9", FOLD_LEVEL,U,9),
    T("ctrl+k ctrl+0", UNFOLD_ALL,U,0),
    T("ctrl+k ctrl+j", UNFOLD_ALL,U,0),
    T("ctrl+k ctrl+t", FOLD_TAG_ATTRIBUTES,U,0),
    T("Menu", CONTEXT_MENU,U,0),
    T("shift+F10", CONTEXT_MENU,U,0),
    T("ctrl+k", CHORD,0,KEYS_CHORD_K),
    T("ctrl+j", CHORD,0,KEYS_CHORD_J),
};

static void stroke(const char *text, uint32_t *sym, uint16_t *mods)
{
    *mods=0;
    for (;;) {
        if (strncmp(text,"ctrl+",5)==0) { *mods|=PLAT_MOD_CTRL; text+=5; }
        else if (strncmp(text,"shift+",6)==0) { *mods|=PLAT_MOD_SHIFT; text+=6; }
        else if (strncmp(text,"alt+",4)==0) { *mods|=PLAT_MOD_ALT; text+=4; }
        else break;
    }
    *sym=xkb_keysym_from_name(text,XKB_KEYSYM_NO_FLAGS);
    CHECK(*sym!=XKB_KEY_NoSymbol);
}
static void compare(const keys_binding *b, const expectation *e)
{
    if (!b || b->action!=e->action || b->args.flags!=e->flags || b->args.value!=e->value) {
        fprintf(stderr,"keys_test: FAIL %s expected action=%d flags=%u value=%d; got action=%d\n",
                e->sequence,(int)e->action,(unsigned)e->flags,(int)e->value,b?(int)b->action:-1);
        CHECK(0);
    }
}
static void bindings(void)
{
    size_t count; const keys_binding *table=keys_defaults(&count);
    /* Lookup each independently written expectation before inspecting table. */
    for (size_t i=0; i<sizeof expected/sizeof expected[0]; ++i) {
        const expectation *e=&expected[i]; keys_state state={0};
        char first[64]; const char *space=strchr(e->sequence,' ');
        const char *last=e->sequence; uint32_t sym; uint16_t mods;
        const keys_binding *b=NULL;
        if (space) {
            size_t n=(size_t)(space-e->sequence); CHECK(n<sizeof first);
            memcpy(first,e->sequence,n); first[n]=0; stroke(first,&sym,&mods);
            CHECK(keys_lookup(&state,sym,mods,&b)==KEYS_PREFIX);
            CHECK(b && b->action==KEYS_ACTION_CHORD); last=space+1;
        }
        stroke(last,&sym,&mods);
        keys_result rc=keys_lookup(&state,sym,mods,&b);
        if (rc!=(e->action==KEYS_ACTION_CHORD?KEYS_PREFIX:KEYS_MATCH)) {
            fprintf(stderr,"keys_test: FAIL %s expected binding, result=%d\n",e->sequence,(int)rc);
            CHECK(0);
        }
        compare(b,e);
        CHECK(state.chord==(e->action==KEYS_ACTION_CHORD?(uint8_t)e->value:0));
        /* Check lock bits and modifier-applied uppercase for EVERY row. */
        state.chord=b->args.chord;
        if (sym>=XKB_KEY_a && sym<=XKB_KEY_z) sym-=XKB_KEY_a-XKB_KEY_A;
        CHECK(keys_lookup(&state,sym,(uint16_t)(mods|PLAT_MOD_CAPS|PLAT_MOD_NUM),&b)==rc);
        compare(b,e);
    }
    CHECK(count==sizeof expected/sizeof expected[0]); CHECK(table);
    CHECK(keys_defaults(NULL)==table);
    /* Each table row must be reached exactly once by the independent oracle;
     * strict ordering also prohibits ambiguous or duplicated keys. */
    for (size_t i=0; i<count; ++i) {
        const keys_binding *b=&table[i]; size_t matches=0;
        for (size_t j=0; j<sizeof expected/sizeof expected[0]; ++j) {
            const char *last=strchr(expected[j].sequence,' ');
            uint8_t chord=last?(expected[j].sequence[5]=='k'?KEYS_CHORD_K:KEYS_CHORD_J):0;
            uint32_t sym; uint16_t mods; stroke(last?last+1:expected[j].sequence,&sym,&mods);
            if (b->args.chord==chord && b->keysym==sym && b->modifiers==mods) ++matches;
        }
        CHECK(matches==1);
        if (i) {
            const keys_binding *p=&table[i-1];
            CHECK(p->args.chord<b->args.chord || (p->args.chord==b->args.chord &&
                  (p->modifiers<b->modifiers || (p->modifiers==b->modifiers && p->keysym<b->keysym))));
        }
    }
    printf("keys_test: %zu independent binding rows passed\n",count);
}

static void expect_key(keys_state *s, uint32_t sym, uint16_t mods,
                       keys_result result, keys_action action)
{
    const keys_binding *out=(const keys_binding *)(const void *)s;
    CHECK(keys_lookup(s,sym,mods,&out)==result);
    if (result==KEYS_MATCH || result==KEYS_PREFIX) CHECK(out && out->action==action);
    else CHECK(out==NULL);
}
static void edge_cases(void)
{
    keys_state s={0}, other={0}; const keys_binding *out=NULL;
    expect_key(&s,UINT32_MAX,0,KEYS_NONE,KEYS_ACTION_NONE);
    expect_key(&s,0,0,KEYS_NONE,KEYS_ACTION_NONE);
    expect_key(&s,XKB_KEY_a,0,KEYS_NONE,KEYS_ACTION_NONE);
    expect_key(&s,XKB_KEY_a,PLAT_MOD_CTRL|PLAT_MOD_ALTGR,KEYS_NONE,KEYS_ACTION_NONE);
    expect_key(&s,XKB_KEY_a,PLAT_MOD_CTRL|PLAT_MOD_SUPER,KEYS_NONE,KEYS_ACTION_NONE);
    expect_key(&s,XKB_KEY_a,UINT16_MAX,KEYS_NONE,KEYS_ACTION_NONE);
    expect_key(&s,XKB_KEY_question,PLAT_MOD_CTRL|PLAT_MOD_SHIFT,KEYS_MATCH,KEYS_ACTION_TOGGLE_COMMENT);
    expect_key(&s,XKB_KEY_braceleft,PLAT_MOD_CTRL|PLAT_MOD_SHIFT,KEYS_MATCH,KEYS_ACTION_FOLD);
    expect_key(&s,XKB_KEY_braceright,PLAT_MOD_CTRL|PLAT_MOD_SHIFT,KEYS_MATCH,KEYS_ACTION_UNFOLD);
    expect_key(&s,XKB_KEY_underscore,PLAT_MOD_ALT|PLAT_MOD_SHIFT,KEYS_MATCH,KEYS_ACTION_JUMP_FORWARD);
    expect_key(&s,XKB_KEY_exclam,PLAT_MOD_CTRL|PLAT_MOD_SHIFT,KEYS_MATCH,KEYS_ACTION_MOVE_TO_GROUP);
    expect_key(&s,XKB_KEY_at,PLAT_MOD_ALT|PLAT_MOD_SHIFT,KEYS_MATCH,KEYS_ACTION_SET_LAYOUT);
    expect_key(&s,XKB_KEY_plus,PLAT_MOD_CTRL|PLAT_MOD_SHIFT,KEYS_MATCH,KEYS_ACTION_FONT_SIZE);
    expect_key(&s,XKB_KEY_question,PLAT_MOD_CTRL,KEYS_NONE,KEYS_ACTION_NONE);
    expect_key(&s,XKB_KEY_ISO_Left_Tab,PLAT_MOD_SHIFT,KEYS_MATCH,KEYS_ACTION_UNINDENT);
    expect_key(&s,XKB_KEY_ISO_Left_Tab,PLAT_MOD_CTRL|PLAT_MOD_SHIFT,KEYS_MATCH,KEYS_ACTION_PREVIOUS_VIEW_STACK);
    const uint32_t kp[]={XKB_KEY_KP_Left,XKB_KEY_KP_Right,XKB_KEY_KP_Up,XKB_KEY_KP_Down,
        XKB_KEY_KP_Home,XKB_KEY_KP_End,XKB_KEY_KP_Prior,XKB_KEY_KP_Next,XKB_KEY_KP_Insert,XKB_KEY_KP_Delete};
    const keys_action actions[]={KEYS_ACTION_LEFT,KEYS_ACTION_RIGHT,KEYS_ACTION_UP,KEYS_ACTION_DOWN,
        KEYS_ACTION_HOME,KEYS_ACTION_END,KEYS_ACTION_PAGE_UP,KEYS_ACTION_PAGE_DOWN,
        KEYS_ACTION_TOGGLE_OVERWRITE,KEYS_ACTION_DELETE};
    for(size_t i=0;i<sizeof kp/sizeof kp[0];++i) expect_key(&s,kp[i],0,KEYS_MATCH,actions[i]);
    expect_key(&s,XKB_KEY_KP_1,PLAT_MOD_NUM,KEYS_NONE,KEYS_ACTION_NONE);
    expect_key(&s,XKB_KEY_k,PLAT_MOD_CTRL,KEYS_PREFIX,KEYS_ACTION_CHORD);
    expect_key(&other,XKB_KEY_u,PLAT_MOD_CTRL,KEYS_MATCH,KEYS_ACTION_SOFT_UNDO);
    const uint32_t modifier_keys[]={XKB_KEY_Shift_L,XKB_KEY_Shift_R,XKB_KEY_Control_L,XKB_KEY_Control_R,
        XKB_KEY_Alt_L,XKB_KEY_Alt_R,XKB_KEY_Meta_L,XKB_KEY_Meta_R,XKB_KEY_Super_L,XKB_KEY_Super_R,
        XKB_KEY_Hyper_L,XKB_KEY_Hyper_R,XKB_KEY_Caps_Lock,XKB_KEY_Num_Lock,
        XKB_KEY_ISO_Level3_Shift,XKB_KEY_ISO_Level5_Shift,XKB_KEY_Mode_switch};
    for(size_t i=0;i<sizeof modifier_keys/sizeof modifier_keys[0];++i) {
        expect_key(&s,modifier_keys[i],0,KEYS_NONE,KEYS_ACTION_NONE); CHECK(s.chord==KEYS_CHORD_K);
    }
    expect_key(&s,XKB_KEY_u,PLAT_MOD_CTRL,KEYS_MATCH,KEYS_ACTION_UPPER_CASE);
    CHECK(s.chord==0);
    expect_key(&s,XKB_KEY_u,PLAT_MOD_CTRL,KEYS_MATCH,KEYS_ACTION_SOFT_UNDO);
    expect_key(&s,XKB_KEY_k,PLAT_MOD_CTRL,KEYS_PREFIX,KEYS_ACTION_CHORD);
    expect_key(&s,XKB_KEY_Escape,0,KEYS_CANCELLED,KEYS_ACTION_NONE);
    CHECK(s.chord==0);
    expect_key(&s,XKB_KEY_k,PLAT_MOD_CTRL,KEYS_PREFIX,KEYS_ACTION_CHORD);
    expect_key(&s,XKB_KEY_k,PLAT_MOD_CTRL,KEYS_MATCH,KEYS_ACTION_DELETE_TO_EOL);
    expect_key(&s,XKB_KEY_k,PLAT_MOD_CTRL,KEYS_PREFIX,KEYS_ACTION_CHORD);
    expect_key(&s,XKB_KEY_u,0,KEYS_CANCELLED,KEYS_ACTION_NONE); CHECK(s.chord==0);
    expect_key(&s,XKB_KEY_j,PLAT_MOD_CTRL,KEYS_PREFIX,KEYS_ACTION_CHORD);
    expect_key(&s,XKB_KEY_u,PLAT_MOD_CTRL,KEYS_CANCELLED,KEYS_ACTION_NONE);
    expect_key(&s,XKB_KEY_k,PLAT_MOD_CTRL,KEYS_PREFIX,KEYS_ACTION_CHORD);
    expect_key(&s,XKB_KEY_u,UINT16_MAX,KEYS_CANCELLED,KEYS_ACTION_NONE);
    expect_key(&s,XKB_KEY_k,PLAT_MOD_CTRL,KEYS_PREFIX,KEYS_ACTION_CHORD);
    keys_reset(&s); CHECK(s.chord==0); keys_reset(NULL);
    s.chord=UINT8_MAX;
    expect_key(&s,XKB_KEY_Left,0,KEYS_MATCH,KEYS_ACTION_LEFT); CHECK(s.chord==0);
    CHECK(keys_lookup(NULL,XKB_KEY_Left,0,&out)==KEYS_ERR_ARG && out==NULL);
    CHECK(keys_lookup(&s,XKB_KEY_Left,0,NULL)==KEYS_ERR_ARG);
}
static void no_allocations(void)
{
    size_t count; const keys_binding *table=keys_defaults(&count); keys_state s={0};
    edit_malloc_guard_begin();
    for(size_t i=0;i<10000;++i) {
        const keys_binding *b=&table[i%count], *out=NULL;
        s.chord=b->args.chord;
        CHECK(keys_lookup(&s,b->keysym,b->modifiers,&out)==(b->action==KEYS_ACTION_CHORD?KEYS_PREFIX:KEYS_MATCH));
        CHECK(out==b);
        expect_key(&s,UINT32_MAX,UINT16_MAX,s.chord?KEYS_CANCELLED:KEYS_NONE,KEYS_ACTION_NONE);
    }
    size_t n=edit_malloc_guard_end(); CHECK(n==0);
    printf("keys_test: 20000 lookups mallocs=%zu guard=%s\n",n,edit_malloc_guard_active()?"active":"ASan-inert");
}
int main(void)
{
    bindings(); edge_cases(); no_allocations(); puts("keys_test: all passed"); return 0;
}
