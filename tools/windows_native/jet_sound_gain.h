#ifndef RUSH_REMASTER_JET_SOUND_GAIN_H
#define RUSH_REMASTER_JET_SOUND_GAIN_H
#include <stdint.h>

/* Original PLANE/PLANEJETFIGHTER record 49 uses logical sound slot 14.
 * Apply once to each incoming start/update gain, before positional mixing.
 * Pitch, pan, attenuation and every other sound slot remain original.
 */
static inline uint32_t rush_remaster_jet_gain_bits(uint32_t bits, uint32_t sound_slot) {
    union { uint32_t bits; float value; } gain;
    gain.bits = bits;
    if (sound_slot == 14 && gain.value >= 0.0f && gain.value <= 1.0f) {
        /* Additional pre-mix headroom for overlapping flyovers. Verify by
         * labelled native recording/listening before calling crackling fixed. */
        gain.value *= 0.50f;
    }
    return gain.bits;
}
#endif
