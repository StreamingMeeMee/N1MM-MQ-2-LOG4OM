#ifndef N1MMMQ2LOG4OM_BAND_H
#define N1MMMQ2LOG4OM_BAND_H

/*
 * Converts N1MM's numeric band value in MHz (e.g. "3.5", "14", "144") to
 * the amateur-radio band name Log4OM/ADIF uses (e.g. "80m", "20m", "2m").
 * Bands shorter than a metre use ADIF's names ("70cm", "23cm", "3cm", "6mm").
 *
 * The value only has to fall inside a band's range, so a nominal band label
 * ("14") and an actual frequency ("14.074") both work. Returns a pointer to a
 * static string, or NULL if the text isn't a plain number or isn't in any
 * known amateur band (the caller should then keep the original text).
 */
const char *band_from_mhz(const char *mhz_text);

#endif /* N1MMMQ2LOG4OM_BAND_H */
