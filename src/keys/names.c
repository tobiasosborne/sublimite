#include "keys/keys.h"
#include "keys/keysym_names.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static unsigned char fold(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c - 'A' + 'a') : c;
}
static int name_compare(const char *a, const char *b)
{
    while (*a && fold((unsigned char)*a) == fold((unsigned char)*b)) { a++; b++; }
    return (int)fold((unsigned char)*a) - (int)fold((unsigned char)*b);
}
static bool hex_value(const char *text, uint32_t *value)
{
    /* (P) XKB numeric spellings contain at most a uint32_t's eight hex digits. */
    if (!*text || strlen(text) > sizeof(uint32_t) * 2u) return false;
    uint32_t result = 0;
    for (; *text; text++) {
        unsigned char c = fold((unsigned char)*text);
        uint32_t digit;
        if (c >= '0' && c <= '9') digit = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (uint32_t)(c - 'a') + 10u;
        else return false;
        /* (P) hexadecimal radix; guard the uint32_t multiply/add. */
        if (result > (UINT32_MAX - digit) / 16u) return false;
        result = result * 16u + digit;
    }
    *value = result; return true;
}
uint32_t keys_keysym_from_name(const char *name)
{
    if (!name) return 0;
    /* XKB also accepts legacy XF86_ spellings, including repeated underscores. */
    if (fold((unsigned char)name[0]) == 'x' && fold((unsigned char)name[1]) == 'f' &&
        name[2] == '8' && name[3] == '6' && name[4] == '_') {
        const char *suffix = name + 4;
        while (*suffix == '_') suffix++;
        char alias[KEYS_KEYSYM_NAME_CAP];
        size_t length = strlen(suffix);
        if (length >= sizeof alias - 4u) return 0; /* (P) XF86 prefix plus NUL. */
        memcpy(alias, "XF86", 4u); memcpy(alias + 4, suffix, length + 1u);
        return keys_keysym_from_name(alias);
    }
    size_t lo = 0, hi = sizeof keys_keysym_names / sizeof keys_keysym_names[0];
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        int cmp = name_compare(name, keys_keysym_names[mid].name);
        if (!cmp) return keys_keysym_names[mid].folded;
        if (cmp < 0) hi = mid; else lo = mid + 1u;
    }
    uint32_t value;
    if (fold((unsigned char)name[0]) == 'u' && hex_value(name + 1, &value)) {
        /* (P) XKB Unicode encoding: printable Latin-1 stays direct; other
         * code points use the Unicode namespace bit. Match XKB's acceptance
         * of surrogate/noncharacter values, too. */
        if ((value >= 0x20u && value <= 0x7eu) || (value >= 0xa0u && value <= 0xffu)) return value;
        if (value >= 0x100u && value <= 0x10ffffu) return 0x01000000u | value;
    }
    /* (P) X11 keysyms occupy the low 29 bits. */
    if (name[0] == '0' && fold((unsigned char)name[1]) == 'x' &&
        hex_value(name + 2, &value) && value <= 0x1fffffffu) return value;
    return 0;
}
int keys_keysym_name(uint32_t keysym, char *out, size_t capacity)
{
    if (!out && capacity) return -1;
    size_t lo = 0, hi = sizeof keys_keysym_by_value / sizeof keys_keysym_by_value[0];
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        const keys_keysym_entry *entry = &keys_keysym_names[keys_keysym_by_value[mid]];
        if (entry->value == keysym) {
            size_t length = strlen(entry->name);
            if (capacity) {
                size_t n = length < capacity ? length : capacity - 1u;
                memcpy(out, entry->name, n); out[n] = '\0';
            }
            return (int)length;
        }
        if (keysym < entry->value) hi = mid; else lo = mid + 1u;
    }
    if (keysym > 0x1fffffffu) {
        (void)snprintf(out, capacity, "Invalid"); return -1;
    }
    if (keysym >= 0x01000100u && keysym <= 0x0110ffffu) {
        uint32_t codepoint = keysym & 0x00ffffffu;
        /* (P) XKB uses four hex digits in the BMP, eight above it. */
        return snprintf(out, capacity, codepoint <= 0xffffu ? "U%04" PRIX32 : "U%08" PRIX32, codepoint);
    }
    return snprintf(out, capacity, "0x%08" PRIx32, keysym);
}
