#include "fif/fif.h"
#include <string.h>
#include <fnmatch.h>
#define MUST(x) EDIT_ASSERT(x)
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* (E) Partition one PATH_MAX-sized fuzz string per parser input and path;
     * these are the production bounds, not an independent fuzz budget. */
    char include[PATH_MAX], exclude[PATH_MAX], path[PATH_MAX];
    char *outputs[] = {include, exclude, path};
    size_t at = 0;
    for (size_t part = 0; part < sizeof outputs / sizeof *outputs; part++) {
        size_t n = size - at;
        n /= sizeof outputs / sizeof *outputs - part;
        if (n >= FIF_PATH_BYTES) n = FIF_PATH_BYTES - 1u;
        memcpy(outputs[part], data + at, n); outputs[part][n] = 0; at += n;
    }
    fif_filter filter;
    fif_code code = fif_filter_parse(&filter, include, exclude);
    MUST(code == FIF_OK || code == FIF_ERR_FILTER);
    if (code == FIF_OK) {
        MUST(filter.count <= FIF_MAX_RULES && filter.used <= sizeof filter.text);
        bool result = fif_filter_matches(&filter, path);
        MUST(result == fif_filter_matches(&filter, path));
        fif_filter deny;
        MUST(fif_filter_parse(&deny, NULL, "**") == FIF_OK);
        MUST(!fif_filter_matches(&deny, path));
    } else MUST(filter.count == 0 && filter.used == 0);
    MUST(fif_glob_match(include, path) == fif_glob_match(include, path));
    /* (P) On the shared star/question-mark/literal subset, POSIX fnmatch's
     * FNM_PATHNAME has the same anchored byte contract. Classes and escapes
     * are excluded here because malformed-pattern recovery is unspecified. */
    if (!strpbrk(include, "[\\") && !strstr(include, "**"))
        MUST(fif_glob_match(include, path) == (fnmatch(include, path, FNM_PATHNAME) == 0));
    /* edit-n95: context cuts keep whole lines, never exceed the cap, back off
     * at most three bytes (P: four-byte UTF-8 maximum) and stop on the first
     * byte that is not a continuation byte. */
    size_t cap = size ? data[0] : 0, cut = fif_context_cut(data, size, cap);
    if (size <= cap) MUST(cut == size);
    else {
        MUST(cut <= cap && cap - cut <= 3u);
        if (cut < cap) MUST((data[cut + 1u] & 0xc0u) == 0x80u); /* stepped back over it */
        if (cut && cap - cut < 3u) MUST((data[cut] & 0xc0u) != 0x80u); /* stopped on a boundary */
    }
    MUST(fif_glob_match("**", path));
    MUST(!fif_glob_match(NULL, path));
    return 0;
}
