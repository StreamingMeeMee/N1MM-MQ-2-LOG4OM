#include "band.h"

#include <stdlib.h>

typedef struct {
    double lo_mhz;
    double hi_mhz;
    const char *name;
} band_range_t;

/* Ranges are deliberately wider than the legal band edges so both N1MM's
 * nominal band labels (1.8, 3.5, 7, 10, 14, 18, 21, 24, 28, 50, 144, 432 ...)
 * and real frequencies within the band land on the right name. */
static const band_range_t BANDS[] = {
    {0.100, 0.200, "2190m"},
    {0.400, 0.600, "630m"},
    {1.600, 2.100, "160m"},
    {3.000, 4.100, "80m"},
    {5.000, 5.500, "60m"},
    {6.900, 7.500, "40m"},
    {10.000, 10.200, "30m"},
    {13.900, 14.500, "20m"},
    {18.000, 18.200, "17m"},
    {20.900, 21.500, "15m"},
    {24.000, 25.000, "12m"},
    {27.900, 30.000, "10m"},
    {49.900, 54.100, "6m"},
    {69.900, 71.000, "4m"},
    {143.900, 148.100, "2m"},
    {219.900, 225.100, "1.25m"},
    {419.900, 450.100, "70cm"},
    {900.000, 930.100, "33cm"},
    {1200.000, 1300.100, "23cm"},
    {2300.000, 2450.100, "13cm"},
    {3300.000, 3500.100, "9cm"},
    {5650.000, 5925.100, "6cm"},
    {10000.000, 10500.100, "3cm"},
    {24000.000, 24250.100, "1.25cm"},
    {47000.000, 47200.100, "6mm"},
    {75500.000, 81000.100, "4mm"},
    {122250.000, 123000.100, "2.5mm"},
    {134000.000, 141000.100, "2mm"},
    {241000.000, 250000.100, "1mm"},
};

const char *band_from_mhz(const char *mhz_text) {
    if (!mhz_text) return NULL;

    char *end = NULL;
    double mhz = strtod(mhz_text, &end);
    if (end == mhz_text) return NULL; /* no number at all */
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') end++;
    if (*end != '\0') return NULL; /* trailing junk, e.g. "80m": already a band name, leave it alone */

    for (size_t i = 0; i < sizeof(BANDS) / sizeof(BANDS[0]); i++) {
        if (mhz >= BANDS[i].lo_mhz && mhz <= BANDS[i].hi_mhz) return BANDS[i].name;
    }
    return NULL;
}
