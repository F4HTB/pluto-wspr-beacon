#pragma once
#include <stdint.h>

#define WSPR_NSYMBOLS 162

/* Standard WSPR type-1 message only: CALL GRID POWER_DBM.
 * Examples: "K1ABC", "FN42", 37.
 * Compound callsigns, hashed callsigns and 6-char locators are intentionally
 * not implemented in this first RF beacon core.
 */
int wspr_encode_standard(const char *callsign, const char *locator4, int power_dbm,
                         uint8_t tones[WSPR_NSYMBOLS]);

/* Lower-level helpers exposed for tests. */
int wspr_pack_standard(const char *callsign, const char *locator4, int power_dbm,
                       uint8_t data[11]);
