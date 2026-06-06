#include "wspr_encode.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const uint8_t sync_vector[WSPR_NSYMBOLS] = {
  1,1,0,0,0,0,0,0,1,0,0,0,1,1,1,0,0,0,1,0,
  0,1,0,1,1,1,1,0,0,0,0,0,0,0,1,0,0,1,0,1,
  0,0,0,0,0,0,1,0,1,1,0,0,1,1,0,1,0,0,0,1,
  1,0,1,0,0,0,0,1,1,0,1,0,1,0,1,0,1,0,0,1,
  0,0,1,0,1,1,0,0,0,1,1,0,1,0,1,0,0,0,1,0,
  0,0,0,0,1,0,0,1,0,0,1,1,1,0,1,1,0,0,1,1,
  0,1,0,0,0,1,1,1,0,0,0,0,0,1,0,1,0,0,1,1,
  0,0,0,0,0,0,0,1,1,0,1,0,1,1,0,0,0,1,1,0,
  0,0
};

static int char36(int c) {
    c = toupper((unsigned char)c);
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}

static int char37(int c) {
    if (c == ' ') return 36;
    return char36(c);
}

/* For the last three callsign characters WSJT-X uses A=0 ... Z=25, space=26. */
static int char27(int c) {
    c = toupper((unsigned char)c);
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c == ' ') return 26;
    return -1;
}

static int normalize_callsign(const char *in, char out[6]) {
    char tmp[16] = {0};
    size_t n = 0;
    while (*in && !isspace((unsigned char)*in) && n < sizeof(tmp)-1) {
        tmp[n++] = (char)toupper((unsigned char)*in++);
    }
    tmp[n] = 0;
    if (n < 3 || n > 6) return -1;

    /* Standard WSPR packing expects the digit in column 3. K1ABC -> " K1ABC". */
    if (!isdigit((unsigned char)tmp[2])) {
        if (n > 5) return -1;
        out[0] = ' ';
        for (size_t i = 0; i < n; ++i) out[i+1] = tmp[i];
        for (size_t i = n + 1; i < 6; ++i) out[i] = ' ';
    } else {
        for (size_t i = 0; i < 6; ++i) out[i] = (i < n) ? tmp[i] : ' ';
    }
    if (!isdigit((unsigned char)out[2])) return -1;
    return 0;
}

static int pack_callsign28(const char *callsign, uint32_t *out) {
    char c[6];
    if (normalize_callsign(callsign, c) != 0) return -1;
    int v0 = char37(c[0]);
    int v1 = char36(c[1]);
    int v2 = isdigit((unsigned char)c[2]) ? c[2] - '0' : -1;
    int v3 = char27(c[3]);
    int v4 = char27(c[4]);
    int v5 = char27(c[5]);
    if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0 || v4 < 0 || v5 < 0) return -1;
    uint32_t n = (uint32_t)v0;
    n = n * 36u + (uint32_t)v1;
    n = n * 10u + (uint32_t)v2;
    n = n * 27u + (uint32_t)v3;
    n = n * 27u + (uint32_t)v4;
    n = n * 27u + (uint32_t)v5;
    *out = n;
    return 0;
}

static int pack_locator15(const char *loc, uint32_t *out) {
    if (!loc || strlen(loc) != 4) return -1;
    int a = toupper((unsigned char)loc[0]) - 'A';
    int b = toupper((unsigned char)loc[1]) - 'A';
    int c = loc[2] - '0';
    int d = loc[3] - '0';
    if (a < 0 || a > 17 || b < 0 || b > 17 || c < 0 || c > 9 || d < 0 || d > 9) return -1;
    /* Same result as WSJT-X packgrid/grid2deg for 4-char Maidenhead locators. */
    *out = (uint32_t)((179 - 10*a - c) * 180 + 10*b + d);
    return 0;
}

static int normalized_power_dbm(int p) {
    static const int nu[10] = {0,-1,1,0,-1,2,1,0,-1,1};
    if (p < 0) p = 0;
    if (p > 60) p = 60;
    return p + nu[p % 10];
}

int wspr_pack_standard(const char *callsign, const char *locator4, int power_dbm,
                       uint8_t data[11]) {
    if (!data) return -1;
    memset(data, 0, 11);
    uint32_t n1, ng;
    if (pack_callsign28(callsign, &n1) != 0) return -2;
    if (pack_locator15(locator4, &ng) != 0) return -3;
    int p = normalized_power_dbm(power_dbm);
    uint32_t n2 = 128u * ng + (uint32_t)(p + 64);
    uint64_t packed = ((uint64_t)n1 << 22) | (uint64_t)(n2 & 0x3fffffu);
    /* 50 bits, MSB-first, left-aligned into 7 bytes; remaining bytes are zero tail. */
    data[0] = (uint8_t)(packed >> 42);
    data[1] = (uint8_t)(packed >> 34);
    data[2] = (uint8_t)(packed >> 26);
    data[3] = (uint8_t)(packed >> 18);
    data[4] = (uint8_t)(packed >> 10);
    data[5] = (uint8_t)(packed >> 2);
    data[6] = (uint8_t)((packed & 0x3u) << 6);
    return 0;
}

static uint8_t parity32(uint32_t x) {
#if defined(__GNUC__)
    return (uint8_t)(__builtin_parity(x) & 1);
#else
    x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; x &= 0xf;
    return (uint8_t)((0x6996 >> x) & 1);
#endif
}

static void conv_encode_162(const uint8_t data[11], uint8_t bits[WSPR_NSYMBOLS]) {
    const uint32_t poly1 = 0xF2D05351u;
    const uint32_t poly2 = 0xE4613C47u;
    uint32_t state = 0;
    int k = 0;
    for (int j = 0; j < 11 && k < WSPR_NSYMBOLS; ++j) {
        for (int b = 7; b >= 0 && k < WSPR_NSYMBOLS; --b) {
            uint32_t bit = (uint32_t)((data[j] >> b) & 1u);
            state = (state << 1) | bit;
            bits[k++] = parity32(state & poly1);
            if (k >= WSPR_NSYMBOLS) break;
            bits[k++] = parity32(state & poly2);
        }
    }
}

static void interleave_wspr(uint8_t id[WSPR_NSYMBOLS]) {
    uint8_t tmp[WSPR_NSYMBOLS];
    int k = -1;
    for (int i = 0; i < 256; ++i) {
        int n = 0, ii = i;
        for (int j = 0; j < 8; ++j) {
            n += n;
            if (ii & 1) n += 1;
            ii /= 2;
        }
        if (n <= 161) {
            ++k;
            tmp[n] = id[k];
        }
    }
    memcpy(id, tmp, WSPR_NSYMBOLS);
}

int wspr_encode_standard(const char *callsign, const char *locator4, int power_dbm,
                         uint8_t tones[WSPR_NSYMBOLS]) {
    uint8_t data[11];
    uint8_t bits[WSPR_NSYMBOLS];
    if (!tones) return -1;
    int rc = wspr_pack_standard(callsign, locator4, power_dbm, data);
    if (rc != 0) return rc;
    conv_encode_162(data, bits);
    interleave_wspr(bits);
    for (int i = 0; i < WSPR_NSYMBOLS; ++i) {
        tones[i] = (uint8_t)(sync_vector[i] + 2u * bits[i]);
    }
    return 0;
}
