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
./wspr-beacon --tone --rf-hz 14097000 --tone-hz 10000 --fs 288000 --phy-fs 2304000 --gain -40 --seconds 10
```

Typical uses:
- checking the transmit chain
- setting a safe output level
- looking for LO leakage or DC offset

### Transmit a WSPR frame

```sh
./wspr-beacon --wspr --call F4XXX --locator JN18 --power 23 --rf-hz 14097000 --offset-hz 10000 --fs 288000 --phy-fs 2304000 --gain -40 --wait-even-minute
```

```sh
./wspr_beacon_tx --wspr --call F4HTB --locator JN18 --power 23 --rf-hz 144490000 --offset-hz 1000000 --fs 2304000 --phy-fs 2304000 --rf-bw 23040 --gain -20 --amp 800
```

## Main Options

- `--probe`: probe the local IIO devices
- `--tone`: transmit a test tone
- `--wspr`: transmit a standard WSPR frame
- `--call`: callsign
- `--locator`: 4-character Maidenhead locator
- `--power`: power value announced in the WSPR message
- `--rf-hz`: target RF frequency
- `--tone-hz`: tone offset in test mode
- `--offset-hz`: base offset in WSPR mode
- `--fs`: IQ sample rate
- `--phy-fs`: PHY-side sample rate
- `--gain`: TX gain or attenuation
- `--amp`: digital IQ amplitude
- `--dc-i`, `--dc-q`: simple DC correction
- `--buffer`: IIO buffer size in samples
- `--seconds`: tone test duration
- `--wait-even-minute`: wait for the next even UTC minute before WSPR transmission
- `--leave-tx-on`: leave the TX LO enabled on exit, for diagnostics only

## Defaults

The program defaults to:

- `--call F4XXX`
- `--locator JN18`
- `--power 23`
- `--fs 288000`
- `--phy-fs 2304000`
- `--offset-hz 10000`
- `--tone-hz 10000`
- `--rf-bw 500000`
- `--gain -40`
- `--amp 1200`
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




