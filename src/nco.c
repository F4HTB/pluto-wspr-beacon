#include "nco.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

uint64_t nco_phase_inc(double freq_hz, double sample_rate_hz) {
    long double scale = 18446744073709551616.0L; /* 2^64 */
    long double v = ((long double)freq_hz * scale) / (long double)sample_rate_hz;
    while (v < 0.0L) v += scale;
    while (v >= scale) v -= scale;
    return (uint64_t)(v + 0.5L);
}

int nco_init(nco_t *nco, double sample_rate_hz, double offset_hz,
             int16_t amplitude, uint32_t lut_bits) {
    if (!nco || sample_rate_hz <= 0.0 || lut_bits < 8 || lut_bits > 20) return -1;
    memset(nco, 0, sizeof(*nco));
    nco->lut_bits = lut_bits;
    nco->lut_size = 1u << lut_bits;
    nco->sin_lut = (int16_t*)calloc(nco->lut_size, sizeof(int16_t));
    if (!nco->sin_lut) return -2;
    for (uint32_t i = 0; i < nco->lut_size; ++i) {
        double x = sin((2.0 * M_PI * (double)i) / (double)nco->lut_size);
        nco->sin_lut[i] = (int16_t)lrint(x * 32767.0);
    }
    nco->amplitude = amplitude;
    for (int k = 0; k < 4; ++k) {
        nco->phase_inc[k] = nco_phase_inc(offset_hz + k * WSPR_TONE_SPACING_HZ,
                                          sample_rate_hz);
    }
    return 0;
}

void nco_free(nco_t *nco) {
    if (!nco) return;
    free(nco->sin_lut);
    memset(nco, 0, sizeof(*nco));
}

void nco_reset_phase(nco_t *nco, uint64_t phase) {
    if (nco) nco->phase = phase;
}

void nco_generate_iq(nco_t *nco, uint8_t tone, int16_t *i, int16_t *q) {
    if (!nco || !nco->sin_lut || !i || !q) return;
    if (tone > 3) tone = 0;
    uint32_t idx_sin = (uint32_t)(nco->phase >> (64u - nco->lut_bits));
    uint32_t idx_cos = (idx_sin + (nco->lut_size >> 2)) & (nco->lut_size - 1u);
    int32_t si = nco->sin_lut[idx_sin];
    int32_t co = nco->sin_lut[idx_cos];
    *i = (int16_t)((co * nco->amplitude) / 32767);
    *q = (int16_t)((si * nco->amplitude) / 32767);
    nco->phase += nco->phase_inc[tone];
}

static int32_t envelope_q15(size_t n, size_t total, size_t ramp) {
    if (ramp == 0 || total == 0) return 32767;
    if (n < ramp) return (int32_t)((32767ull * n) / ramp);
    if (n + ramp >= total) {
        size_t rem = total - n;
        return (int32_t)((32767ull * rem) / ramp);
    }
    return 32767;
}

size_t wspr_generate_frame_iq(nco_t *nco, const uint8_t tones[162],
                              uint32_t sample_rate_hz,
                              int16_t *iq_interleaved,
                              size_t max_iq_samples,
                              uint32_t ramp_ms) {
    if (!nco || !tones || !iq_interleaved || sample_rate_hz == 0) return 0;
    const uint64_t den = 12000ull;
    const uint64_t num = 8192ull;
    const size_t total = (size_t)(((uint64_t)sample_rate_hz * num * 162ull + den/2) / den);
    if (max_iq_samples < total) return 0;
    const size_t ramp = ((size_t)sample_rate_hz * ramp_ms) / 1000u;
    uint64_t sym_acc = 0;
    uint8_t sym = 0;
    for (size_t n = 0; n < total; ++n) {
        int16_t i = 0, q = 0;
        nco_generate_iq(nco, tones[sym], &i, &q);
        int32_t env = envelope_q15(n, total, ramp);
        iq_interleaved[2*n + 0] = (int16_t)(((int32_t)i * env) / 32767);
        iq_interleaved[2*n + 1] = (int16_t)(((int32_t)q * env) / 32767);
        sym_acc += den;
        if (sym_acc >= (uint64_t)sample_rate_hz * num) {
            sym_acc -= (uint64_t)sample_rate_hz * num;
            if (sym < 161) ++sym;
        }
    }
    return total;
}
