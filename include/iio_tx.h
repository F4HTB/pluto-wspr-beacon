#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Thin libiio TX wrapper. This compiles as a stub unless USE_LIBIIO is defined. */
typedef struct iio_tx iio_tx_t;

typedef struct {
    long long app_sample_rate_hz;
    long long ad9363_sample_rate_hz;
    bool use_fpga_x8;
    size_t buffer_samples;
    long long lo_frequency_hz;
    long long rf_bandwidth_hz;
    long long tx_gain_mdB; /* milli-dB if backend supports it */
} iio_tx_config_t;

int iio_tx_open(iio_tx_t **out, const iio_tx_config_t *cfg);
int iio_tx_push_iq(iio_tx_t *tx, const int16_t *iq_interleaved, size_t samples);
void iio_tx_close(iio_tx_t *tx);
