#pragma once
#include <stdint.h>
#include <stddef.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846264338327950288
#endif

#define WSPR_TONE_SPACING_HZ (12000.0 / 8192.0)

typedef struct {
    uint32_t lut_bits;
    uint32_t lut_size;
    int16_t *sin_lut;
    uint64_t phase;
    uint64_t phase_inc[4];
    int16_t amplitude;
} nco_t;

int nco_init(nco_t *nco, double sample_rate_hz, double offset_hz,
             int16_t amplitude, uint32_t lut_bits);
void nco_free(nco_t *nco);
void nco_reset_phase(nco_t *nco, uint64_t phase);
void nco_generate_iq(nco_t *nco, uint8_t tone, int16_t *i, int16_t *q);

/* Generate a complete WSPR frame as interleaved int16 IQ.
 * Returns number of IQ samples written, or 0 on invalid arguments.
 * Uses exact WSPR symbol timing: 8192/12000 seconds/symbol.
 */
size_t wspr_generate_frame_iq(nco_t *nco, const uint8_t tones[162],
                              uint32_t sample_rate_hz,
                              int16_t *iq_interleaved,
                              size_t max_iq_samples,
                              uint32_t ramp_ms);

uint64_t nco_phase_inc(double freq_hz, double sample_rate_hz);
