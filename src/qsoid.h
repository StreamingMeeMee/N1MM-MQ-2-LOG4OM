#ifndef N1MMMQ2LOG4OM_QSOID_H
#define N1MMMQ2LOG4OM_QSOID_H

#define QSOID_LEN 17

/*
 * Generates a qsoid from the current local time and a random number:
 * YYYYMMDDHHMMSS (4-digit year, 2-digit month, day, 24-hour, minute, second)
 * followed by 3 random digits with leading zeros -- QSOID_LEN (17) characters.
 * `out` must hold QSOID_LEN + 1 bytes.
 *
 * Within one process, two ids generated in the same second never get the same
 * 3-digit suffix (it steps to the next unused one), so this app can't
 * generate a colliding id for two messages handled in the same second.
 */
void qsoid_generate(char *out);

#endif /* N1MMMQ2LOG4OM_QSOID_H */
