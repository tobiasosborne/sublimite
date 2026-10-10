/* P4.11b bake identity. SHA-256 is evaluated at INIT, never on lookup.
 * Both checked-in bakes use vendor/DejaVuSansMono.ttf, face 0.
 * Re-baking requires updating this digest and the all-ASCII pixel test. */
#ifndef EDIT_FONT_IDENTITY_H
#define EDIT_FONT_IDENTITY_H
static uint32_t font_identity_rotr(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32u - n));
}
static int font_identity_matches(const unsigned char *data, size_t len, uint32_t face)
{
    const uint8_t expected[32] = {
        0xc8,0x05,0xf9,0x43,0x6d,0xbc,0x26,0x86,0x44,0xc1,0xd9,0x58,0x4f,0x01,0xa6,0x01,
        0xa6,0x53,0xe0,0x28,0xe0,0x8f,0xd7,0x4b,0x9b,0x94,0x9f,0x6c,0xf8,0x30,0x4d,0x88
    };
    const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    if (face != 0 || len != 343140u) return 0;
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    size_t padded = (len + 9u + 63u) & ~(size_t)63u;
    for (size_t at = 0; at < padded; at += 64u) {
        uint32_t w[64] = {0};
        for (size_t j = 0; j < 64; j++) {
            size_t off = at + j;
            uint8_t byte = off < len ? data[off] : off == len ? 0x80u : 0u;
            if (off >= padded - 8u) byte = (uint8_t)(((uint64_t)len * 8u) >> ((padded - 1u - off) * 8u));
            w[j / 4u] |= (uint32_t)byte << ((3u - j % 4u) * 8u);
        }
        for (size_t j = 16; j < 64; j++) {
            uint32_t a = w[j - 15u], b = w[j - 2u];
            uint32_t s0 = font_identity_rotr(a,7) ^ font_identity_rotr(a,18) ^ (a >> 3);
            uint32_t s1 = font_identity_rotr(b,17) ^ font_identity_rotr(b,19) ^ (b >> 10);
            w[j] = w[j - 16u] + s0 + w[j - 7u] + s1;
        }
        uint32_t v[8]; memcpy(v, h, sizeof v);
        for (size_t j = 0; j < 64; j++) {
            uint32_t s1 = font_identity_rotr(v[4],6) ^ font_identity_rotr(v[4],11) ^ font_identity_rotr(v[4],25);
            uint32_t t1 = v[7] + s1 + ((v[4] & v[5]) ^ (~v[4] & v[6])) + k[j] + w[j];
            uint32_t s0 = font_identity_rotr(v[0],2) ^ font_identity_rotr(v[0],13) ^ font_identity_rotr(v[0],22);
            uint32_t t2 = s0 + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
            for (size_t z = 7; z > 0; z--) v[z] = v[z - 1u];
            v[4] += t1; v[0] = t1 + t2;
        }
        for (size_t j = 0; j < 8; j++) h[j] += v[j];
    }
    for (size_t j = 0; j < 32; j++)
        if ((uint8_t)(h[j / 4u] >> ((3u - j % 4u) * 8u)) != expected[j]) return 0;
    return 1;
}
#endif
