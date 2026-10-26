#if defined(_WIN32)
#include "font/discovery.h"
#include "font/selection.h"
#include "font/dwrite_internal.h"
#include <string.h>

typedef struct text_source {
    IDWriteTextAnalysisSource iface;
    WCHAR text[2]; /* (P) one Unicode scalar: at most a UTF-16 surrogate pair */
    UINT32 length;
    LONG refs;
} text_source;
static ULONG STDMETHODCALLTYPE source_add(IDWriteTextAnalysisSource *self)
{ return (ULONG)InterlockedIncrement(&((text_source *)self)->refs); }
static ULONG STDMETHODCALLTYPE source_release(IDWriteTextAnalysisSource *self)
{ return (ULONG)InterlockedDecrement(&((text_source *)self)->refs); }
static HRESULT STDMETHODCALLTYPE source_query(IDWriteTextAnalysisSource *self, REFIID iid, void **out)
{
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IDWriteTextAnalysisSource)) return E_NOINTERFACE;
    *out = self; (void)source_add(self); return S_OK;
}
static HRESULT STDMETHODCALLTYPE source_at(IDWriteTextAnalysisSource *self, UINT32 pos,
                                            const WCHAR **text, UINT32 *length)
{
    text_source *s = (text_source *)self;
    *text = pos < s->length ? s->text + pos : NULL;
    *length = pos < s->length ? s->length - pos : 0; return S_OK;
}
static HRESULT STDMETHODCALLTYPE source_before(IDWriteTextAnalysisSource *self, UINT32 pos,
                                                const WCHAR **text, UINT32 *length)
{
    text_source *s = (text_source *)self;
    if (pos > s->length) return E_INVALIDARG;
    *text = pos ? s->text : NULL; *length = pos; return S_OK;
}
static DWRITE_READING_DIRECTION STDMETHODCALLTYPE source_direction(IDWriteTextAnalysisSource *self)
{ (void)self; return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT; }
static HRESULT STDMETHODCALLTYPE source_locale(IDWriteTextAnalysisSource *self, UINT32 pos,
                                                UINT32 *length, const WCHAR **locale)
{
    text_source *s = (text_source *)self;
    if (pos > s->length) return E_INVALIDARG;
    *length = s->length - pos; *locale = L"zh-CN"; return S_OK;
}
static HRESULT STDMETHODCALLTYPE source_number(IDWriteTextAnalysisSource *self, UINT32 pos,
                                                UINT32 *length, IDWriteNumberSubstitution **sub)
{
    text_source *s = (text_source *)self;
    if (pos > s->length) return E_INVALIDARG;
    *length = s->length - pos; *sub = NULL; return S_OK;
}
static int map_scalar(font_dwrite *dw, uint32_t cp, char *path, uint32_t *index)
{
    path[0] = 0; *index = 0;
    if (cp > 0x10ffffu || (cp >= 0xd800u && cp <= 0xdfffu)) return 0;
    static const IDWriteTextAnalysisSourceVtbl vtable = {
        source_query, source_add, source_release, source_at, source_before,
        source_direction, source_locale, source_number
    };
    text_source source = {{&vtable}, {0, 0}, 1, 1};
    if (cp <= 0xffffu) source.text[0] = (WCHAR)cp;
    else {
        source.text[0] = (WCHAR)(0xd800u + ((cp - 0x10000u) >> 10));
        source.text[1] = (WCHAR)(0xdc00u + ((cp - 0x10000u) & 0x3ffu)); source.length = 2;
    }
    IDWriteFontFallback *fallback = NULL;
    IDWriteFont *mapped = NULL;
    int ok = 0;
    if (FAILED(dw->factory->lpVtbl->GetSystemFontFallback(dw->factory, &fallback))) goto done;
    UINT32 length = 0; FLOAT scale = 0;
    if (FAILED(fallback->lpVtbl->MapCharacters(fallback, &source.iface, 0, source.length,
            NULL, L"Consolas", DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, &length, &mapped, &scale)) || !mapped || length != source.length) goto done;
    BOOL covered = FALSE;
    if (FAILED(mapped->lpVtbl->HasCharacter(mapped, cp, &covered)) || !covered) goto done;
    ok = font_dwrite_path(mapped, path, FONT_FALLBACK_PATH_MAX, index);
done:
    if (mapped) mapped->lpVtbl->Release(mapped);
    if (fallback) fallback->lpVtbl->Release(fallback);
    return ok;
}
static int discovery_stopped(font_fallback *owner, work_ctx *ctx)
{
    atomic_fetch_add_explicit(&owner->cancellation_polls, 1u, memory_order_relaxed);
    return work_should_stop(ctx);
}
int font_fallback_isolated(font_fallback *owner, font_fallback_result *out, work_ctx *ctx)
{
    memset(out, 0, sizeof *out);
    uint64_t start = os_now_ns(); out->worker = os_thread_self();
    if (discovery_stopped(owner, ctx)) return 0;
    font_dwrite dw;
    if (font_dwrite_open(&dw, "dwrite.dll")) {
        out->have_fontconfig = 1;
        (void)map_scalar(&dw, 0x4e2du, out->cjk, &out->cjk_index);
        if (!discovery_stopped(owner, ctx)) (void)map_scalar(&dw, 0x1f600u, out->emoji, &out->emoji_index);
        font_dwrite_close(&dw);
    }
    out->elapsed_ns = os_now_ns() - start;
    return !discovery_stopped(owner, ctx);
}
int font_fallback_codepoint(font_fallback *owner, work_ctx *ctx, uint32_t cp, char *path, uint32_t *index)
{
    path[0] = 0; *index = 0;
    if (discovery_stopped(owner, ctx)) return 0;
    font_dwrite dw;
    if (font_dwrite_open(&dw, "dwrite.dll")) {
        (void)map_scalar(&dw, cp, path, index); font_dwrite_close(&dw);
    }
    return !discovery_stopped(owner, ctx);
}
void font_fallback_discover(font_fallback *fb, const char *soname)
{
    font_fallback_reset(fb);
    fb->worker = os_thread_self(); uint64_t start = os_now_ns();
    font_dwrite dw;
    if (font_dwrite_open(&dw, soname)) {
        fb->have_fontconfig = 1;
        (void)map_scalar(&dw, 0x4e2du, fb->cjk, &fb->cjk_index);
        (void)map_scalar(&dw, 0x1f600u, fb->emoji, &fb->emoji_index);
        font_dwrite_close(&dw);
    }
    fb->elapsed_ns = os_now_ns() - start;
    atomic_store_explicit(&fb->done, 1u, memory_order_release);
}

/* Fontconfig patterns are a Linux grammar. Windows accepts an exact installed
 * family (plus the internal monochrome suffix used by font_chain_resolve). */
int font_selection_resolve(const char *pattern, char *path, size_t cap, uint32_t *index)
{
    if (!pattern || !path || !cap || !index) return FONT_ERR_ARG;
    path[0] = 0; *index = 0;
    char family[FONT_FALLBACK_PATH_MAX];
    size_t n = strlen(pattern);
    static const char suffix[] = ":color=false";
    if (n >= sizeof suffix - 1u && !strcmp(pattern + n - (sizeof suffix - 1u), suffix)) n -= sizeof suffix - 1u;
    if (!n || n >= sizeof family) return FONT_ERR_ARG;
    memcpy(family, pattern, n); family[n] = 0;
    WCHAR wide[FONT_FALLBACK_PATH_MAX];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, family, -1, wide, FONT_FALLBACK_PATH_MAX)) return FONT_ERR_ARG;
    font_dwrite dw;
    if (!font_dwrite_open(&dw, "dwrite.dll")) return FONT_ERR_MISSING;
    IDWriteFontCollection *collection = NULL;
    IDWriteFontFamily *found = NULL;
    IDWriteFont *font = NULL;
    int result = FONT_ERR_MISSING;
    if (FAILED(dw.factory->lpVtbl->GetSystemFontCollection(dw.factory, &collection, FALSE))) goto done;
    UINT32 at = 0; BOOL exists = FALSE;
    if (FAILED(collection->lpVtbl->FindFamilyName(collection, wide, &at, &exists)) || !exists) goto done;
    if (FAILED(collection->lpVtbl->GetFontFamily(collection, at, &found))) goto done;
    if (FAILED(found->lpVtbl->GetFirstMatchingFont(found, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font))) goto done;
    if (font_dwrite_path(font, path, cap, index)) result = FONT_OK;
done:
    if (font) font->lpVtbl->Release(font);
    if (found) found->lpVtbl->Release(found);
    if (collection) collection->lpVtbl->Release(collection);
    font_dwrite_close(&dw); return result;
}
#endif
