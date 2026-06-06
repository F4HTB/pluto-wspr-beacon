#define _POSIX_C_SOURCE 200809L
#include "wspr_encode.h"
#include "nco.h"

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <iio.h>

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig) { (void)sig; g_stop = 1; }

struct opts {
    int probe;
    int tone;
    int wspr;
    const char *callsign;
    const char *locator;
    int power_dbm;
    long long rf_hz;
    long long fs_hz;
    long long phy_fs_hz;
    double offset_hz;
    double tone_hz;
    double rf_bw_hz;
    double tx_gain_db;
    int amp;
    int dc_i;
    int dc_q;
    size_t buffer_samples;
    int duration_s;
    int wait_even_minute;
    int leave_tx_on;
    int duty_pct;
};

struct tx_session {
    struct iio_context *ctx;
    struct iio_buffer *buf;
    struct iio_channel *tx0;
    struct iio_channel *tx1;
    nco_t nco;
    int nco_ready;
    uint8_t tones[WSPR_NSYMBOLS];
};

static void usage(const char *p) {
    fprintf(stderr,
        "Usage:\n"
        "  %s --probe\n"
        "  %s --tone --rf-hz HZ [--tone-hz 10000] [--fs 288000] [--phy-fs 2304000] [--seconds 10]\n"
        "  %s --wspr --call F4XXX --locator JN38 --power 23 --rf-hz HZ [--offset-hz 10000] [--fs 288000] [--phy-fs 2304000]\n"
        "Options:\n"
        "  --fs HZ              TXDAC IQ sample rate, use 288000 or current card value\n"
        "  --rf-bw HZ           AD936x TX RF bandwidth, default 23040\n"
        "  --gain DB            AD936x TX hardwaregain/attenuation, default -30\n"
        "  --amp N              IQ amplitude in signed int16, default 1000\n"
        "  --dc-i N             add signed DC offset to I before TX, default 0\n"
        "  --dc-q N             add signed DC offset to Q before TX, default 0\n"
        "  --buffer N           IIO buffer samples, default 65536\n"
        "  --seconds N          tone duration in seconds, default 10\n"
        "  --duty-pct N         continuous WSPR duty cycle in percent, 0..100\n"
        "  --wait-even-minute   wait for next even UTC minute before WSPR TX\n"
        "  --leave-tx-on        do not power down TX LO at program exit\n",
        p, p, p);
}

static int parse_ll(const char *s, long long *v) {
    char *e = NULL; errno = 0; long long x = strtoll(s, &e, 10);
    if (errno || !e || *e) return -1;
    *v = x;
    return 0;
}

static int parse_int(const char *s, int *v) {
    long long x;
    if (parse_ll(s, &x) != 0) return -1;
    if (x < INT32_MIN || x > INT32_MAX) return -1;
    *v = (int)x;
    return 0;
}

static int parse_size(const char *s, size_t *v) {
    char *e = NULL;
    errno = 0;
    unsigned long long x = strtoull(s, &e, 10);
    if (errno || !e || *e) return -1;
    if (x > (unsigned long long)SIZE_MAX) return -1;
    *v = (size_t)x;
    return 0;
}

static int parse_double(const char *s, double *v) {
    char *e = NULL; errno = 0; double x = strtod(s, &e);
    if (errno || !e || *e) return -1;
    *v = x;
    return 0;
}

static int parse_args(int argc, char **argv, struct opts *o) {
    int mode_count = 0;

    memset(o, 0, sizeof(*o));
    o->callsign = "F4XXX";
    o->locator = "JN38";
    o->power_dbm = 23;
    o->fs_hz = 288000;
    o->phy_fs_hz = 2304000;
    o->offset_hz = 10000.0;
    o->tone_hz = 10000.0;
    o->rf_bw_hz = 23040.0;
    o->tx_gain_db = -30.0;
    o->amp = 1000;
    o->dc_i = 0;
    o->dc_q = 0;
    o->buffer_samples = 65536;
    o->duration_s = 10;
    o->duty_pct = -1;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--probe")) o->probe = 1;
        else if (!strcmp(argv[i], "--tone")) o->tone = 1;
        else if (!strcmp(argv[i], "--wspr")) o->wspr = 1;
        else if (!strcmp(argv[i], "--call") && i+1 < argc) o->callsign = argv[++i];
        else if (!strcmp(argv[i], "--locator") && i+1 < argc) o->locator = argv[++i];
        else if (!strcmp(argv[i], "--power") && i+1 < argc) { if (parse_int(argv[++i], &o->power_dbm)) return -1; }
        else if (!strcmp(argv[i], "--rf-hz") && i+1 < argc) { if (parse_ll(argv[++i], &o->rf_hz)) return -1; }
        else if (!strcmp(argv[i], "--fs") && i+1 < argc) { if (parse_ll(argv[++i], &o->fs_hz)) return -1; }
        else if (!strcmp(argv[i], "--phy-fs") && i+1 < argc) { if (parse_ll(argv[++i], &o->phy_fs_hz)) return -1; }
        else if (!strcmp(argv[i], "--offset-hz") && i+1 < argc) { if (parse_double(argv[++i], &o->offset_hz)) return -1; }
        else if (!strcmp(argv[i], "--tone-hz") && i+1 < argc) { if (parse_double(argv[++i], &o->tone_hz)) return -1; }
        else if (!strcmp(argv[i], "--rf-bw") && i+1 < argc) { if (parse_double(argv[++i], &o->rf_bw_hz)) return -1; }
        else if (!strcmp(argv[i], "--gain") && i+1 < argc) { if (parse_double(argv[++i], &o->tx_gain_db)) return -1; }
        else if (!strcmp(argv[i], "--amp") && i+1 < argc) { if (parse_int(argv[++i], &o->amp)) return -1; }
        else if (!strcmp(argv[i], "--dc-i") && i+1 < argc) { if (parse_int(argv[++i], &o->dc_i)) return -1; }
        else if (!strcmp(argv[i], "--dc-q") && i+1 < argc) { if (parse_int(argv[++i], &o->dc_q)) return -1; }
        else if (!strcmp(argv[i], "--buffer") && i+1 < argc) { if (parse_size(argv[++i], &o->buffer_samples)) return -1; }
        else if (!strcmp(argv[i], "--seconds") && i+1 < argc) { if (parse_int(argv[++i], &o->duration_s)) return -1; }
        else if (!strcmp(argv[i], "--duty-pct") && i+1 < argc) { if (parse_int(argv[++i], &o->duty_pct)) return -1; }
        else if (!strcmp(argv[i], "--wait-even-minute")) o->wait_even_minute = 1;
        else if (!strcmp(argv[i], "--leave-tx-on")) o->leave_tx_on = 1;
        else return -1;
    }
    mode_count = (o->probe ? 1 : 0) + (o->tone ? 1 : 0) + (o->wspr ? 1 : 0);
    if (mode_count != 1) return -1;
    if ((o->tone || o->wspr) && o->rf_hz <= 0) return -1;
    if (o->fs_hz <= 0) return -1;
    if (o->phy_fs_hz < 0) return -1;
    if (o->tone_hz < 0.0 || o->offset_hz < 0.0) return -1;
    if (o->rf_bw_hz <= 0.0) return -1;
    if (o->buffer_samples == 0) return -1;
    if (o->duration_s <= 0) return -1;
    if (o->duty_pct < -1 || o->duty_pct > 100) return -1;
    if (o->duty_pct >= 0 && !o->wspr) return -1;
    return 0;
}

static void wait_for_even_minute_edge(void) {
    for (;;) {
        time_t now = time(NULL);
        struct tm tmv;
        gmtime_r(&now, &tmv);
        if ((tmv.tm_min % 2) == 0 && tmv.tm_sec == 0) return;
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000L };
        nanosleep(&ts, NULL);
        if (g_stop) return;
    }
}

static int seconds_until_next_wspr_slot(void) {
    time_t now = time(NULL);
    if (now < 0) return 0;
    int rem = (int)(now % 120);
    return rem == 0 ? 0 : 120 - rem;
}

static void wait_until_wspr_slot_with_prewarm(int prewarm_s) {
    for (;;) {
        int wait_s = seconds_until_next_wspr_slot();
        if (wait_s <= prewarm_s) return;

        int sleep_s = wait_s - prewarm_s;
        if (sleep_s > 1) sleep_s = 1;
        struct timespec ts = { .tv_sec = sleep_s, .tv_nsec = 0 };
        nanosleep(&ts, NULL);
        if (g_stop) return;
    }
}

static uint64_t current_wspr_slot_index(void) {
    time_t now = time(NULL);
    return now >= 0 ? (uint64_t)now / 120ull : 0;
}

static uint64_t next_wspr_slot_index(void) {
    time_t now = time(NULL);
    if (now < 0) return 0;
    return ((uint64_t)now + 119ull) / 120ull;
}

static int duty_slot_selected(uint64_t slot_index, int duty_pct) {
    if (duty_pct <= 0) return 0;
    if (duty_pct >= 100) return 1;
    if (slot_index == 0) return 1;

    return ((((uint64_t)slot_index * (uint64_t)duty_pct) / 100ull)
        > ((((uint64_t)slot_index - 1ull) * (uint64_t)duty_pct) / 100ull));
}

static uint64_t next_selected_wspr_slot(uint64_t first_slot, int duty_pct, int *slots_until_tx) {
    uint64_t slot = first_slot;
    int skipped = 0;

    while (!duty_slot_selected(slot, duty_pct)) {
        ++slot;
        ++skipped;
    }

    if (slots_until_tx) *slots_until_tx = skipped;
    return slot;
}

static struct iio_device *find_dev(struct iio_context *ctx, const char *name) {
    struct iio_device *d = iio_context_find_device(ctx, name);
    if (!d) fprintf(stderr, "IIO device not found: %s\n", name);
    return d;
}

static void print_attr_chan(struct iio_channel *ch, const char *attr) {
    char buf[512];
    int rc = iio_channel_attr_read(ch, attr, buf, sizeof(buf));
    if (rc >= 0) printf("  %s = %s\n", attr, buf);
}

static int write_ll_chan(struct iio_channel *ch, const char *attr, long long v, int required) {
    int rc = iio_channel_attr_write_longlong(ch, attr, v);
    if (rc < 0) {
        fprintf(stderr, "write %s=%lld failed: %s (%d)\n", attr, v, strerror(-rc), rc);
        return required ? -1 : 0;
    }
    return 0;
}

static int write_str_chan(struct iio_channel *ch, const char *attr, const char *v, int required) {
    int rc = iio_channel_attr_write(ch, attr, v);
    if (rc < 0) {
        fprintf(stderr, "write %s=%s failed: %s (%d)\n", attr, v, strerror(-rc), rc);
        return required ? -1 : 0;
    }
    return 0;
}

static int write_double_chan(struct iio_channel *ch, const char *attr, double v, int required) {
    char s[64]; snprintf(s, sizeof(s), "%.6f", v);
    return write_str_chan(ch, attr, s, required);
}

static int16_t clamp_i16(int32_t x) {
    if (x > 32767) return 32767;
    if (x < -32768) return -32768;
    return (int16_t)x;
}

static void disable_internal_dds(struct iio_device *txd) {
    static const char *dds[] = {
        "altvoltage0", "altvoltage1", "altvoltage2", "altvoltage3",
        "altvoltage4", "altvoltage5", "altvoltage6", "altvoltage7"
    };
    for (size_t k = 0; k < sizeof(dds)/sizeof(dds[0]); ++k) {
        struct iio_channel *ch = iio_device_find_channel(txd, dds[k], true);
        if (ch) (void)iio_channel_attr_write(ch, "scale", "0");
    }
}

static void set_dac_dma_sources(struct iio_device *txd) {
#if defined(LIBIIO_HAS_REG_ACCESS)
    (void)iio_device_reg_write(txd, 0x418, 0x2);
    (void)iio_device_reg_write(txd, 0x458, 0x2);
#endif
    (void)txd;
}

static void cleanup_tx_path(struct iio_context *ctx, int leave_tx_on) {
    if (!ctx) return;

    struct iio_device *txd = iio_context_find_device(ctx, "cf-ad9361-dds-core-lpc");
    if (txd) disable_internal_dds(txd);

    if (!leave_tx_on) {
        struct iio_device *phy = iio_context_find_device(ctx, "ad9361-phy");
        if (phy) {
            struct iio_channel *lo = iio_device_find_channel(phy, "altvoltage1", true);
            if (lo) (void)iio_channel_attr_write_longlong(lo, "powerdown", 1);
        }
    }
}

static int probe(void) {
    struct iio_context *ctx = iio_create_local_context();
    if (!ctx) { fprintf(stderr, "cannot create local IIO context\n"); return 1; }
    printf("IIO devices: %u\n", iio_context_get_devices_count(ctx));
    struct iio_device *phy = find_dev(ctx, "ad9361-phy");
    struct iio_device *txd = find_dev(ctx, "cf-ad9361-dds-core-lpc");
    if (phy) {
        printf("ad9361-phy TX voltage0 output:\n");
        struct iio_channel *v0 = iio_device_find_channel(phy, "voltage0", true);
        struct iio_channel *lo = iio_device_find_channel(phy, "altvoltage1", true);
        if (v0) { print_attr_chan(v0, "sampling_frequency"); print_attr_chan(v0, "sampling_frequency_available"); print_attr_chan(v0, "rf_bandwidth"); print_attr_chan(v0, "hardwaregain"); }
        if (lo) { printf("TX_LO:\n"); print_attr_chan(lo, "frequency"); print_attr_chan(lo, "powerdown"); }
    }
    if (txd) {
        printf("cf-ad9361-dds-core-lpc voltage0/1:\n");
        struct iio_channel *v0 = iio_device_find_channel(txd, "voltage0", true);
        struct iio_channel *v1 = iio_device_find_channel(txd, "voltage1", true);
        if (v0) { print_attr_chan(v0, "sampling_frequency"); print_attr_chan(v0, "sampling_frequency_available"); }
        if (v1) { print_attr_chan(v1, "sampling_frequency"); print_attr_chan(v1, "sampling_frequency_available"); }
    }
    iio_context_destroy(ctx);
    return 0;
}

static void wait_even_minute(void) {
    wait_for_even_minute_edge();
}

static int configure_rf(struct iio_context *ctx, const struct opts *o) {
    struct iio_device *phy = find_dev(ctx, "ad9361-phy");
    struct iio_device *txd = find_dev(ctx, "cf-ad9361-dds-core-lpc");
    if (!phy || !txd) return -1;
    struct iio_channel *phy0 = iio_device_find_channel(phy, "voltage0", true);
    struct iio_channel *phy1 = iio_device_find_channel(phy, "voltage1", true);
    struct iio_channel *lo = iio_device_find_channel(phy, "altvoltage1", true);
    struct iio_channel *tx0 = iio_device_find_channel(txd, "voltage0", true);
    struct iio_channel *tx1 = iio_device_find_channel(txd, "voltage1", true);
    if (!phy0 || !lo || !tx0 || !tx1) return -1;

    disable_internal_dds(txd);
    set_dac_dma_sources(txd);

    long long lo_hz = o->rf_hz - (long long)llround(o->wspr ? o->offset_hz : o->tone_hz);
    if (lo_hz <= 0) { fprintf(stderr, "invalid LO computed\n"); return -1; }

    /* Conservative TX settings. Some firmwares reject optional attrs; do not fail on those. */
    write_str_chan(phy0, "rf_port_select", "A", 0);
    if (phy1) write_str_chan(phy1, "rf_port_select", "A", 0);
    write_ll_chan(phy0, "rf_bandwidth", (long long)llround(o->rf_bw_hz), 0);
    if (phy1) write_ll_chan(phy1, "rf_bandwidth", (long long)llround(o->rf_bw_hz), 0);
    write_double_chan(phy0, "hardwaregain", o->tx_gain_db, 0);
    if (phy1) write_double_chan(phy1, "hardwaregain", o->tx_gain_db, 0);

    /* On the current Tezuka setup verified by iio_attr:
       AD936x PHY TX sample rate = 2304000 S/s, TXDAC application IQ rate = 288000 S/s.
       The FPGA TX interpolator is x8.  Set PHY first, then TXDAC. */
    if (o->phy_fs_hz > 0) {
        write_ll_chan(phy0, "sampling_frequency", o->phy_fs_hz, 0);
        if (phy1) write_ll_chan(phy1, "sampling_frequency", o->phy_fs_hz, 0);
    }
    if (write_ll_chan(tx0, "sampling_frequency", o->fs_hz, 1) < 0) return -1;
    write_ll_chan(tx1, "sampling_frequency", o->fs_hz, 0);
    if (write_ll_chan(lo, "frequency", lo_hz, 1) < 0) return -1;
    write_ll_chan(lo, "powerdown", 0, 0);

    printf("Configured: rf_hz=%lld lo=%lld fs=%lld phy_fs=%lld offset/tone=%.3f amp=%d dc_i=%d dc_q=%d gain=%.2f bw=%.0f\n",
           o->rf_hz, lo_hz, o->fs_hz, o->phy_fs_hz, o->wspr ? o->offset_hz : o->tone_hz, o->amp, o->dc_i, o->dc_q, o->tx_gain_db, o->rf_bw_hz);
    return 0;
}

static void tx_session_cleanup(struct tx_session *s, int leave_tx_on) {
    if (!s) return;

    if (s->buf) {
        for (int k = 0; k < 4; ++k) {
            char *pi = (char*)iio_buffer_first(s->buf, s->tx0);
            char *pq = (char*)iio_buffer_first(s->buf, s->tx1);
            char *end = (char*)iio_buffer_end(s->buf);
            ptrdiff_t step = iio_buffer_step(s->buf);
            while (pi < end && pq < end) {
                *(int16_t*)pi = 0;
                *(int16_t*)pq = 0;
                pi += step;
                pq += step;
            }
            (void)iio_buffer_push(s->buf);
        }
    }

    if (s->nco_ready) nco_free(&s->nco);
    if (s->buf) iio_buffer_destroy(s->buf);
    cleanup_tx_path(s->ctx, leave_tx_on);
    if (s->ctx) iio_context_destroy(s->ctx);
    memset(s, 0, sizeof(*s));
}

static int tx_session_prepare(struct tx_session *s, const struct opts *o) {
    memset(s, 0, sizeof(*s));

    s->ctx = iio_create_local_context();
    if (!s->ctx) { fprintf(stderr, "cannot create local IIO context\n"); return 1; }

    if (configure_rf(s->ctx, o) < 0) return 1;

    struct iio_device *txd = iio_context_find_device(s->ctx, "cf-ad9361-dds-core-lpc");
    s->tx0 = txd ? iio_device_find_channel(txd, "voltage0", true) : NULL;
    s->tx1 = txd ? iio_device_find_channel(txd, "voltage1", true) : NULL;
    if (!txd || !s->tx0 || !s->tx1) {
        fprintf(stderr, "TX IIO channels not found\n");
        return 1;
    }

    iio_channel_enable(s->tx0);
    iio_channel_enable(s->tx1);
    s->buf = iio_device_create_buffer(txd, o->buffer_samples, false);
    if (!s->buf) {
        fprintf(stderr, "iio_device_create_buffer failed\n");
        return 1;
    }

    if (o->wspr) {
        int rc = wspr_encode_standard(o->callsign, o->locator, o->power_dbm, s->tones);
        if (rc) {
            fprintf(stderr, "WSPR encode failed: %d\n", rc);
            return 1;
        }
        printf("WSPR: %s %s %d dBm, tones encoded\n", o->callsign, o->locator, o->power_dbm);
    }

    double offset = o->wspr ? o->offset_hz : o->tone_hz;
    if (nco_init(&s->nco, (double)o->fs_hz, offset, (int16_t)o->amp, 16) != 0) {
        fprintf(stderr, "nco_init failed\n");
        return 1;
    }
    s->nco_ready = 1;

    return 0;
}

static int tx_session_run(struct tx_session *s, const struct opts *o) {
    uint64_t sample_global = 0;
    const uint64_t sym_den = 12000ull;
    const uint64_t sym_num = 8192ull;
    const uint64_t samples_per_symbol = ((uint64_t)o->fs_hz * sym_num) / sym_den;
    const int exact_symbol = (((uint64_t)o->fs_hz * sym_num) % sym_den) == 0;
    if (o->wspr) printf("samples_per_symbol=%llu exact=%d\n", (unsigned long long)samples_per_symbol, exact_symbol);

    uint64_t sample_in_symbol = 0;
    uint64_t total_samples = o->tone
        ? (uint64_t)o->duration_s * (uint64_t)o->fs_hz
        : (((uint64_t)o->fs_hz * sym_num * WSPR_NSYMBOLS) + sym_den / 2) / sym_den;
    int sym = 0;
    const uint64_t ramp = ((uint64_t)o->fs_hz * 50ull) / 1000ull;
    uint64_t sym_acc = 0;

    while (!g_stop && sample_global < total_samples) {
        char *pi = (char*)iio_buffer_first(s->buf, s->tx0);
        char *pq = (char*)iio_buffer_first(s->buf, s->tx1);
        char *end = (char*)iio_buffer_end(s->buf);
        ptrdiff_t step = iio_buffer_step(s->buf);
        while (pi < end && pq < end && sample_global < total_samples && !g_stop) {
            uint8_t tone = o->wspr ? s->tones[sym] : 0;
            int16_t i, q;
            nco_generate_iq(&s->nco, tone, &i, &q);
            if (ramp && (sample_global < ramp || sample_global + ramp >= total_samples)) {
                uint64_t e = sample_global < ramp ? sample_global : (total_samples - sample_global);
                i = (int16_t)(((int32_t)i * (int32_t)e) / (int32_t)ramp);
                q = (int16_t)(((int32_t)q * (int32_t)e) / (int32_t)ramp);
            }
            *(int16_t*)pi = clamp_i16((int32_t)i + (int32_t)o->dc_i);
            *(int16_t*)pq = clamp_i16((int32_t)q + (int32_t)o->dc_q);
            pi += step;
            pq += step;
            ++sample_global;
            if (o->wspr) {
                sample_in_symbol++;
                sym_acc += sym_den;
                if (sym_acc >= (uint64_t)o->fs_hz * sym_num) {
                    sym_acc -= (uint64_t)o->fs_hz * sym_num;
                    sample_in_symbol = 0;
                    if (++sym >= WSPR_NSYMBOLS) sym = WSPR_NSYMBOLS - 1;
                }
            }
        }
        ssize_t pushed = iio_buffer_push(s->buf);
        if (pushed < 0) {
            fprintf(stderr, "iio_buffer_push failed: %s (%zd)\n", strerror((int)-pushed), pushed);
            return 1;
        }
    }

    printf("Done, streamed %.3f seconds%s\n",
           (double)sample_global / (double)o->fs_hz,
           o->leave_tx_on ? " (TX LO left on)" : " (TX LO powered down)");
    return 0;
}

static int stream_tone_or_wspr(const struct opts *o) {
    struct tx_session s;
    int ret;

    if (o->wait_even_minute && o->wspr) {
        printf("Waiting for next even UTC minute...\n");
        wait_even_minute();
        if (g_stop) return 1;
    }

    ret = tx_session_prepare(&s, o);
    if (ret == 0) ret = tx_session_run(&s, o);
    tx_session_cleanup(&s, o->leave_tx_on);
    return ret;
}

int main(int argc, char **argv) {
    signal(SIGINT, on_sigint); signal(SIGTERM, on_sigint);
    struct opts o;
    if (parse_args(argc, argv, &o) < 0) { usage(argv[0]); return 2; }
    if (o.probe) return probe();

    if (o.wspr && o.duty_pct >= 0) {
        int ret = 0;
        uint64_t last_slot = UINT64_MAX;
        uint64_t last_announced_tx_slot = UINT64_MAX;
        uint64_t base_slot = next_wspr_slot_index();

        if (o.duty_pct == 0) {
            printf("No transmit scheduled (duty 0%%)\n");
            while (!g_stop) {
                struct timespec ts = { .tv_sec = 1, .tv_nsec = 0 };
                nanosleep(&ts, NULL);
            }
            return 0;
        }

        while (!g_stop) {
            uint64_t first_slot = next_wspr_slot_index();
            int slots_until_tx = 0;
            uint64_t rel_first_slot = first_slot >= base_slot ? first_slot - base_slot : 0;
            uint64_t rel_tx_slot = next_selected_wspr_slot(rel_first_slot, o.duty_pct, &slots_until_tx);
            uint64_t tx_slot = base_slot + rel_tx_slot;
            if (tx_slot != last_announced_tx_slot) {
                printf("Next transmit in %d %s\n",
                       slots_until_tx,
                       slots_until_tx == 1 ? "slot" : "slots");
                last_announced_tx_slot = tx_slot;
            }

            if (slots_until_tx == 0) {
                struct tx_session s;
                struct opts run_opts = o;
                run_opts.wait_even_minute = 0;

                wait_until_wspr_slot_with_prewarm(5);
                if (g_stop) break;

                ret = tx_session_prepare(&s, &run_opts);
                if (ret != 0) {
                    tx_session_cleanup(&s, run_opts.leave_tx_on);
                    break;
                }

                wait_for_even_minute_edge();
                if (g_stop) {
                    tx_session_cleanup(&s, run_opts.leave_tx_on);
                    break;
                }

                uint64_t slot = current_wspr_slot_index();
                if (slot == last_slot) {
                    tx_session_cleanup(&s, run_opts.leave_tx_on);
                    struct timespec ts = { .tv_sec = 0, .tv_nsec = 200000000L };
                    nanosleep(&ts, NULL);
                    continue;
                }
                last_slot = slot;

                if (slot < base_slot || !duty_slot_selected(slot - base_slot, o.duty_pct)) {
                    tx_session_cleanup(&s, run_opts.leave_tx_on);
                    continue;
                }

                ret = tx_session_run(&s, &run_opts);
                tx_session_cleanup(&s, run_opts.leave_tx_on);
                if (ret != 0 || g_stop) break;
                continue;
            }

            wait_for_even_minute_edge();
            if (g_stop) break;

            uint64_t slot = current_wspr_slot_index();
            if (slot == last_slot) {
                struct timespec ts = { .tv_sec = 0, .tv_nsec = 200000000L };
                nanosleep(&ts, NULL);
                continue;
            }
            last_slot = slot;

            if (slot < base_slot || !duty_slot_selected(slot - base_slot, o.duty_pct)) {
                continue;
            }

            if (seconds_until_next_wspr_slot() > 5) {
                continue;
            }

            struct tx_session s;
            struct opts run_opts = o;
            run_opts.wait_even_minute = 0;

            ret = tx_session_prepare(&s, &run_opts);
            if (ret != 0) {
                tx_session_cleanup(&s, run_opts.leave_tx_on);
                break;
            }

            ret = tx_session_run(&s, &run_opts);
            tx_session_cleanup(&s, run_opts.leave_tx_on);
            if (ret != 0 || g_stop) break;
        }
        return ret;
    }

    return stream_tone_or_wspr(&o);
}
