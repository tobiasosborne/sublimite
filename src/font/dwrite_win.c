#if defined(_WIN32)
/* Immutable-byte COM loaders are scoped to one isolated factory/raster call.
 * Font descriptors can still be copied by value without COM ownership. */
#define INITGUID
#include "font/dwrite.h"
#include "font/dwrite_internal.h"
#include <limits.h>
#include <string.h>

int font_dwrite_open(font_dwrite *dw, const char *library)
{
    memset(dw, 0, sizeof *dw);
    if (GetEnvironmentVariableW(L"SUBLIMITE_TEST_NO_DWRITE", NULL, 0)) return 0;
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return 0;
    dw->uninitialize = SUCCEEDED(hr);
    WCHAR name[FONT_FALLBACK_PATH_MAX];
    if (!library || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, library, -1,
                                         name, FONT_FALLBACK_PATH_MAX)) goto fail;
    dw->library = LoadLibraryW(name);
    if (!dw->library) goto fail;
    typedef HRESULT (WINAPI *create_fn)(DWRITE_FACTORY_TYPE, REFIID, IUnknown **);
    create_fn create = NULL;
    FARPROC entry = GetProcAddress(dw->library, "DWriteCreateFactory");
    _Static_assert(sizeof create == sizeof entry, "Win32 function pointer size");
    memcpy(&create, &entry, sizeof create);
    if (!create || FAILED(create(DWRITE_FACTORY_TYPE_ISOLATED, &IID_IDWriteFactory2,
                                  (IUnknown **)&dw->factory))) goto fail;
    return 1;
fail:
    font_dwrite_close(dw); return 0;
}
void font_dwrite_close(font_dwrite *dw)
{
    if (dw->factory) dw->factory->lpVtbl->Release(dw->factory);
    if (dw->library) FreeLibrary(dw->library);
    if (dw->uninitialize) CoUninitialize();
    memset(dw, 0, sizeof *dw);
}

int font_dwrite_path(IDWriteFont *font, char *path, size_t cap, uint32_t *index)
{
    IDWriteFontFace *face = NULL;
    IDWriteFontFile *file = NULL;
    IDWriteFontFileLoader *loader = NULL;
    IDWriteLocalFontFileLoader *local = NULL;
    UINT32 count = 0, key_size = 0, length = 0;
    const void *key = NULL;
    int ok = 0;
    path[0] = 0; *index = 0;
    if (FAILED(font->lpVtbl->CreateFontFace(font, &face))) goto done;
    if (FAILED(face->lpVtbl->GetFiles(face, &count, NULL)) || count != 1) goto done;
    if (FAILED(face->lpVtbl->GetFiles(face, &count, &file))) goto done;
    if (FAILED(file->lpVtbl->GetReferenceKey(file, &key, &key_size))) goto done;
    if (FAILED(file->lpVtbl->GetLoader(file, &loader))) goto done;
    if (FAILED(loader->lpVtbl->QueryInterface(loader, &IID_IDWriteLocalFontFileLoader, (void **)&local))) goto done;
    if (FAILED(local->lpVtbl->GetFilePathLengthFromKey(local, key, key_size, &length))) goto done;
    WCHAR wide[FONT_FALLBACK_PATH_MAX];
    if (length >= FONT_FALLBACK_PATH_MAX || cap > INT_MAX) goto done;
    if (FAILED(local->lpVtbl->GetFilePathFromKey(local, key, key_size, wide, length + 1u))) goto done;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, path, (int)cap, NULL, NULL)) goto done;
    *index = face->lpVtbl->GetIndex(face); ok = 1;
done:
    if (local) local->lpVtbl->Release(local);
    if (loader) loader->lpVtbl->Release(loader);
    if (file) file->lpVtbl->Release(file);
    if (face) face->lpVtbl->Release(face);
    if (!ok) path[0] = 0;
    return ok;
}

typedef struct byte_stream {
    IDWriteFontFileStream iface;
    const font_t *font;
    LONG refs;
} byte_stream;
typedef struct byte_loader {
    IDWriteFontFileLoader iface;
    byte_stream stream;
    LONG refs;
} byte_loader;
static ULONG STDMETHODCALLTYPE stream_add(IDWriteFontFileStream *self)
{ return (ULONG)InterlockedIncrement(&((byte_stream *)self)->refs); }
static ULONG STDMETHODCALLTYPE stream_release(IDWriteFontFileStream *self)
{ return (ULONG)InterlockedDecrement(&((byte_stream *)self)->refs); }
static HRESULT STDMETHODCALLTYPE stream_query(IDWriteFontFileStream *self, REFIID iid, void **out)
{
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IDWriteFontFileStream)) return E_NOINTERFACE;
    *out = self; (void)stream_add(self); return S_OK;
}
static HRESULT STDMETHODCALLTYPE stream_read(IDWriteFontFileStream *self, const void **out,
                                              UINT64 offset, UINT64 size, void **context)
{
    const font_t *f = ((byte_stream *)self)->font;
    *out = NULL; *context = NULL;
    if (offset > f->len || size > f->len - offset) return E_FAIL;
    *out = f->data + (size_t)offset; return S_OK;
}
static void STDMETHODCALLTYPE stream_fragment(IDWriteFontFileStream *self, void *context)
{ (void)self; (void)context; }
static HRESULT STDMETHODCALLTYPE stream_size(IDWriteFontFileStream *self, UINT64 *size)
{ *size = ((byte_stream *)self)->font->len; return S_OK; }
static HRESULT STDMETHODCALLTYPE stream_time(IDWriteFontFileStream *self, UINT64 *time)
{ (void)self; *time = 0; return E_NOTIMPL; }
static ULONG STDMETHODCALLTYPE loader_add(IDWriteFontFileLoader *self)
{ return (ULONG)InterlockedIncrement(&((byte_loader *)self)->refs); }
static ULONG STDMETHODCALLTYPE loader_release(IDWriteFontFileLoader *self)
{ return (ULONG)InterlockedDecrement(&((byte_loader *)self)->refs); }
static HRESULT STDMETHODCALLTYPE loader_query(IDWriteFontFileLoader *self, REFIID iid, void **out)
{
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IDWriteFontFileLoader)) return E_NOINTERFACE;
    *out = self; (void)loader_add(self); return S_OK;
}
static HRESULT STDMETHODCALLTYPE loader_stream(IDWriteFontFileLoader *self, const void *key,
                                                UINT32 size, IDWriteFontFileStream **out)
{
    (void)key;
    *out = NULL;
    if (size != sizeof(uint32_t)) return E_INVALIDARG;
    *out = &((byte_loader *)self)->stream.iface; (void)stream_add(*out); return S_OK;
}

int font_dwrite_raster(const font_t *f, uint32_t gid, edit_arena *arena,
                        font_bitmap *out, int unavailable)
{
    if (unavailable || !f || !arena || !out || !f->data || !f->px || !gid || gid >= f->num_glyphs)
        return FONT_ERR_INIT;
    font_dwrite dw;
    if (!font_dwrite_open(&dw, "dwrite.dll")) return FONT_ERR_INIT;
    /* Static const vtables are immutable, not process state. */
    static const IDWriteFontFileStreamVtbl stream_vtable = {
        stream_query, stream_add, stream_release, stream_read, stream_fragment, stream_size, stream_time
    };
    static const IDWriteFontFileLoaderVtbl loader_vtable = {
        loader_query, loader_add, loader_release, loader_stream
    };
    byte_loader loader = {{&loader_vtable}, {{&stream_vtable}, f, 1}, 1};
    IDWriteFontFile *file = NULL;
    IDWriteFontFace *face = NULL;
    IDWriteGlyphRunAnalysis *analysis = NULL;
    int result = FONT_ERR_INIT, registered = 0;
    edit_arena_mark_t mark = edit_arena_mark(arena);
    if (FAILED(dw.factory->lpVtbl->RegisterFontFileLoader(dw.factory, &loader.iface))) goto done;
    registered = 1;
    uint32_t key = 0;
    if (FAILED(dw.factory->lpVtbl->CreateCustomFontFileReference(dw.factory, &key, sizeof key, &loader.iface, &file))) goto done;
    BOOL supported = FALSE;
    DWRITE_FONT_FILE_TYPE type;
    DWRITE_FONT_FACE_TYPE face_type;
    UINT32 count = 0;
    if (FAILED(file->lpVtbl->Analyze(file, &supported, &type, &face_type, &count)) || !supported || f->face_index >= count) goto done;
    if (FAILED(dw.factory->lpVtbl->CreateFontFace(dw.factory, face_type, 1, &file,
                    f->face_index, DWRITE_FONT_SIMULATIONS_NONE, &face))) goto done;
    DWRITE_FONT_METRICS metrics;
    face->lpVtbl->GetMetrics(face, &metrics);
    UINT16 glyph = (UINT16)gid;
    DWRITE_GLYPH_METRICS gm;
    if (FAILED(face->lpVtbl->GetDesignGlyphMetrics(face, &glyph, 1, &gm, FALSE))) goto done;
    FLOAT advance = (FLOAT)gm.advanceWidth * f->scale;
    DWRITE_GLYPH_RUN run = {face, (FLOAT)metrics.designUnitsPerEm * f->scale, 1, &glyph, &advance, NULL, FALSE, 0};
    if (FAILED(dw.factory->lpVtbl->IDWriteFactory2_CreateGlyphRunAnalysis(dw.factory, &run, NULL,
            DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, DWRITE_MEASURING_MODE_NATURAL,
            DWRITE_GRID_FIT_MODE_DISABLED, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE, 0, 0, &analysis))) goto done;
    RECT box;
    if (FAILED(analysis->lpVtbl->GetAlphaTextureBounds(analysis, DWRITE_TEXTURE_ALIASED_1x1, &box))) goto done;
    if (box.right < box.left || box.bottom < box.top) goto done;
    uint64_t w = (uint64_t)((int64_t)box.right - box.left), h = (uint64_t)((int64_t)box.bottom - box.top);
    /* Same worker bitmap bounds as metrics_of_glyph in font.c. */
    if (w > 4096u || h > 4096u || w * h > UINT64_C(16777216)) goto done;
    size_t bytes = (size_t)(w * h);
    uint8_t *pixels = bytes ? edit_arena_alloc(arena, bytes, 1) : NULL;
    if (bytes && !pixels) { result = FONT_ERR_NOMEM; goto done; }
    if (bytes && FAILED(analysis->lpVtbl->CreateAlphaTexture(analysis, DWRITE_TEXTURE_ALIASED_1x1,
                                                            &box, pixels, (UINT32)bytes))) goto done;
    out->pixels = pixels; out->w = (uint32_t)w; out->h = (uint32_t)h;
    out->m = (font_metric){(int32_t)(advance + 0.5f), box.left, box.top, out->w, out->h};
    result = FONT_OK;
done:
    if (analysis) analysis->lpVtbl->Release(analysis);
    if (face) face->lpVtbl->Release(face);
    if (file) file->lpVtbl->Release(file);
    if (registered) (void)dw.factory->lpVtbl->UnregisterFontFileLoader(dw.factory, &loader.iface);
    font_dwrite_close(&dw);
    if (result != FONT_OK) edit_arena_reset_to_mark(arena, mark);
    return result;
}
#endif
