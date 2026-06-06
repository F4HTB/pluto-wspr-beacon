# wspr-beacon

`wspr-beacon` is a small command-line WSPR transmitter for AD936x-based SDR platforms using `libiio`.

It is intended for PlutoSDR-class hardware, or compatible boards exposing a similar local IIO transmit path.

## Overview

This tool can:
- probe the local IIO devices used by the transmit chain
- transmit a test tone
- transmit a standard WSPR frame

It is meant as a compact bring-up and experimentation tool rather than a full-featured WSPR application.

### Probe the platform

```sh
./wspr-beacon --probe
```

This prints the detected IIO devices and a few useful transmit-side attributes.

### Transmit a test tone

```sh
./wspr-beacon --tone --rf-hz 144490000 --tone-hz 1000000 --fs 2304000 --phy-fs 2304000 --rf-bw 23040 --gain -20 --amp 800 --seconds 10
```

```text
Tone tuning:
  Final tone RF frequency = 144490000 Hz
  IQ tone frequency relative to the Local Oscillator = 1000000 Hz
  Local Oscillator = 144490000 - 1000000 = 143490000 Hz
```

Typical uses:
- checking the transmit chain
- setting a safe output level
- looking for LO leakage or DC offset

### Transmit a WSPR frame

HF example with an explicit final RF frequency and LO offset.

```sh
./wspr-beacon --wspr --call F4XXX --locator JN18 --power 23 --rf-hz 144490000 --offset-hz 10000 --fs 288000 --phy-fs 2304000 --gain -40 --wait-even-minute
```

```text
WSPR tuning:
  Final WSPR RF frequency = 144490000 Hz
  LO-to-WSPR-signal offset = 10000 Hz
  Local Oscillator = 144490000 - 10000 = 144480000 Hz
```

2m example with an explicit final RF frequency and LO offset.

```sh
./wspr-beacon --wspr --call F4HTB --locator JN18 --power 23 --rf-hz 144490000 --offset-hz 1000000 --fs 2304000 --phy-fs 2304000 --rf-bw 23040 --gain -20 --amp 800
```

```text
WSPR tuning:
  Final WSPR RF frequency = 144490000 Hz
  LO-to-WSPR-signal offset = 1000000 Hz
  Local Oscillator = 144490000 - 1000000 = 143490000 Hz
```

2m example with an explicit final RF frequency and fixed LO.

```sh
./wspr-beacon --wspr --call F4HTB --locator JN18 --power 23 --rf-hz 144490000 --lo-hz 143490000 --fs 2304000 --phy-fs 2304000 --rf-bw 23040 --gain -20 --amp 800
```

```text
WSPR tuning:
  Final WSPR RF frequency = 144490000 Hz
  Local Oscillator = 143490000 Hz
  LO-to-WSPR-signal offset = 144490000 - 143490000 = 1000000 Hz
```

Band example with a fixed WSPR baseband position.

```sh
./wspr-beacon --wspr --call F4HTB --locator JN18 --power 23 --band 2m --wspr-baseband-hz 1500 --offset-hz 1000000 --fs 2304000 --phy-fs 2304000 --gain -20 --wait-even-minute
```

```text
WSPR tuning:
  WSPR reference frequency for 2m = 144489000 Hz
  Selected WSPR slot = 1500 Hz
  Final WSPR RF frequency = 144489000 + 1500 = 144490500 Hz
  LO-to-WSPR-signal offset = 1000000 Hz
  Local Oscillator = 144490500 - 1000000 = 143490500 Hz
```

Band example with a random WSPR baseband position and LO locked to the 1400 Hz reference.

```sh
./wspr-beacon --wspr --call F4HTB --locator JN18 --power 23 --band 2m --wspr-random-baseband-hz --wspr-lock-lo1400hz --fs 2304000 --phy-fs 2304000 --gain -20 --wait-even-minute
```

```text
WSPR tuning:
  WSPR reference frequency for 2m = 144489000 Hz
  Selected WSPR slot = random value from 1400 to 1600 Hz in 10 Hz steps
  Final WSPR RF frequency = 144489000 + selected WSPR slot
  Local Oscillator = 144489000 + 1400 = 144490400 Hz
  LO-to-WSPR-signal offset = selected WSPR slot - 1400, from 0 to 200 Hz
```

## Main Options

### WSPR option tree

```text
wspr-beacon
├── --probe: inspect local IIO devices and TX attributes
│   └── no required radio option
│
├── --tone: transmit a continuous test tone
│   ├── required
│   │   └── --rf-hz HZ: final RF frequency of the transmitted tone
│   ├── optional
│   │   ├── --tone-hz HZ: tone frequency generated in IQ relative to the Local Oscillator
│   │   ├── --fs HZ: software IQ sample rate used by the TXDAC buffer and tone generator
│   │   ├── --phy-fs HZ: AD936x TX sample rate configured on the physical RF path
│   │   ├── --rf-bw HZ: AD936x TX analog/RF bandwidth setting
│   │   ├── --gain DB: AD936x TX hardware gain or attenuation
│   │   ├── --amp N: digital IQ amplitude before samples are sent to the TX buffer
│   │   ├── --dc-i N: signed DC correction added to I samples
│   │   ├── --dc-q N: signed DC correction added to Q samples
│   │   ├── --buffer N: number of IQ samples per libiio TX buffer
│   │   ├── --seconds N: test tone duration before stopping transmission
│   │   └── --leave-tx-on: keep the AD936x TX Local Oscillator enabled on exit
│   └── forbidden
│       ├── --band: WSPR-only band selector
│       ├── --call: WSPR-only callsign field
│       ├── --locator: WSPR-only Maidenhead locator field
│       ├── --power: WSPR-only announced transmit power field
│       ├── --offset-hz: WSPR-only Local Oscillator offset strategy
│       ├── --wspr-lock-lo1400hz: WSPR-only Local Oscillator lock strategy
│       ├── --lo-hz: WSPR-only fixed Local Oscillator strategy
│       ├── --wspr-baseband-hz: WSPR-only fixed slot inside the 1400..1600 Hz window
│       ├── --wspr-random-baseband-hz: WSPR-only random slot inside the 1400..1600 Hz window
│       ├── --wait-even-minute: WSPR-only even-minute synchronization
│       └── --duty-pct: WSPR-only continuous duty-cycle scheduler
│
└── --wspr: transmit a standard WSPR frame
    ├── required message
    │   ├── --call CALL: callsign encoded in the WSPR message
    │   ├── --locator LOCATOR: 4-character Maidenhead locator encoded in the WSPR message
    │   └── --power DBM: transmit power value encoded in the WSPR message
    │
    ├── RF source, exactly one
    │   ├── --rf-hz HZ: exact final RF frequency of the WSPR signal
    │   │   ├── use this when full manual frequency control is needed
    │   │   └── forbidden
    │   │       ├── --band
    │   │       ├── --wspr-baseband-hz
    │   │       ├── --wspr-random-baseband-hz
    │   │       └── --wspr-lock-lo1400hz
    │   │
    │   └── --band BAND: choose an AD936x-compatible WSPR band from the internal table
    │       ├── WSPR reference frequency is the WSPR frequency assigned to that band
    │       ├── rf_hz = WSPR reference frequency + selected 1400..1600 Hz WSPR slot
    │       ├── bands: 6m, 4m, 2m
    │       ├── WSPR slot position, exactly one
    │       │   ├── --wspr-baseband-hz HZ: manually choose the WSPR slot position
    │       │   │   └── accepts any value from 1400 to 1600 Hz inclusive
    │       │   └── --wspr-random-baseband-hz: randomly choose the WSPR slot position
    │       │       └── chooses one of 21 slots from 1400 to 1600 Hz inclusive, in 10 Hz steps
    │       └── forbidden
    │           └── --rf-hz
    │
    ├── Local Oscillator strategy, exactly one
    │   ├── --offset-hz HZ: place the Local Oscillator below the final WSPR RF signal by this offset
    │   │   ├── compatible with --rf-hz
    │   │   ├── compatible with --band
    │   │   ├── Local Oscillator = rf_hz - offset_hz
    │   │   └── LO-to-WSPR-signal offset = offset_hz
    │   │
    │   ├── --lo-hz HZ: force the AD936x TX Local Oscillator to an explicit frequency
    │   │   ├── compatible with --rf-hz
    │   │   ├── compatible with --band
    │   │   ├── Local Oscillator = lo_hz
    │   │   ├── LO-to-WSPR-signal offset = rf_hz - lo_hz
    │   │   └── constraint: abs(rf_hz - lo_hz) < fs_hz / 2
    │   │
    │   └── --wspr-lock-lo1400hz: lock the Local Oscillator to the 1400 Hz WSPR reference
    │       ├── compatible only with --band
    │       ├── forbidden with --rf-hz
    │       ├── Local Oscillator = WSPR reference frequency + 1400
    │       └── LO-to-WSPR-signal offset = rf_hz - Local Oscillator, from 0 to 200 Hz
    │
    ├── optional WSPR options
    │   ├── --wait-even-minute: wait until the next even UTC minute before transmitting one WSPR frame
    │   ├── --duty-pct N: repeatedly transmit selected WSPR slots according to a 0..100% duty cycle
    │   └── --leave-tx-on: keep the AD936x TX Local Oscillator enabled on exit
    │
    └── optional common options
        ├── --fs HZ: software IQ sample rate used by the TXDAC buffer and tone generator
        ├── --phy-fs HZ: AD936x TX sample rate configured on the physical RF path
        ├── --rf-bw HZ: AD936x TX analog/RF bandwidth setting
        ├── --gain DB: AD936x TX hardware gain or attenuation
        ├── --amp N: digital IQ amplitude before samples are sent to the TX buffer
        ├── --dc-i N: signed DC correction added to I samples
        ├── --dc-q N: signed DC correction added to Q samples
        └── --buffer N: number of IQ samples per libiio TX buffer
```

## Defaults

WSPR message and frequency options intentionally have no defaults in the strict CLI.
When using `--wspr`, the following values must be provided explicitly:

- `--call`
- `--locator`
- `--power`
- exactly one of `--rf-hz` or `--band`
- exactly one LO strategy: `--offset-hz`, `--lo-hz`, or `--wspr-lock-lo1400hz`
- with `--band`, exactly one of `--wspr-baseband-hz` or `--wspr-random-baseband-hz`

The program keeps defaults only for hardware and streaming parameters:

- `--fs 288000`
- `--phy-fs 2304000`
- `--tone-hz 10000`
- `--rf-bw 23040`
- `--gain -30`
- `--amp 1000`
- `--dc-i 0`
- `--dc-q 0`
- `--buffer 65536`
- `--seconds 10`

### Hardware-dependent values

The valid range for the following options depends on the underlying AD936x platform and its exposed IIO attributes:

- `--fs`
- `--phy-fs`
- `--rf-bw`
- `--gain`
- `--buffer`
- `--tone-hz`
- `--offset-hz`

Use `--probe` and your platform documentation to determine supported values.
