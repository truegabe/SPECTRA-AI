#ifndef SPECTON_H
#define SPECTON_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <time.h>

#define SPECT_STATES 10
#define SPECT_VERSION "0.1.0"

typedef enum { SPECT_FIXED = 0, SPECT_RANGE = 1, SPECT_WAVE = 2 } SpectMode;

typedef struct {
    SpectMode mode;
    union {
        uint8_t fixed_val;
        struct { uint8_t lo; uint8_t hi; } range;
        float wave[SPECT_STATES];
    } data;
} Specton;

/* -------------------------------------------------------------------------
 * Constructors
 * ---------------------------------------------------------------------- */

/* Create a FIXED Specton; val is clamped to [0, 9]. */
Specton spect_fixed(uint8_t val);

/* Create a RANGE Specton spanning [lo..hi] (both inclusive). */
Specton spect_range(uint8_t lo, uint8_t hi);

/* Create a WAVE Specton from an explicit float[SPECT_STATES] weight array.
 * Weights are stored as-is (not normalized). */
Specton spect_wave(const float weights[SPECT_STATES]);

/* Create a WAVE Specton with equal weights (each = 0.1). */
Specton spect_wave_uniform(void);

/* Create a WAVE Specton with a single nonzero state. */
Specton spect_wave_single(uint8_t state, float weight);

/* Create a WAVE Specton and auto-normalize the given weight array to sum 1. */
Specton spect_wave_normalized(const float weights[SPECT_STATES]);

/* -------------------------------------------------------------------------
 * Mode conversion
 * ---------------------------------------------------------------------- */

/* Convert any mode to WAVE. */
Specton spect_to_wave(Specton s);

/* Collapse any mode to FIXED (peak for WAVE, midpoint for RANGE). */
Specton spect_to_fixed(Specton s);

/* Convert any mode to RANGE. */
Specton spect_to_range(Specton s);

/* -------------------------------------------------------------------------
 * Core operations
 * ---------------------------------------------------------------------- */

/* Return the definite value: value for FIXED, midpoint for RANGE, peak for WAVE. */
uint8_t spect_collapse(Specton s);

/* Weighted random sample from WAVE; else collapse. */
uint8_t spect_observe(Specton s);

/* Index of the highest-weight state. */
uint8_t spect_peak(Specton s);

/* Shannon entropy of the state distribution. */
float spect_entropy(Specton s);

/* Weight at a given state index (0–9). */
float spect_weight_at(Specton s, uint8_t state);

/* Normalize WAVE so weights sum to 1.0.
 * If all weights are 0, falls back to uniform (each = 0.1). */
Specton spect_normalize(Specton s);

/* Multiply all wave weights by factor. */
Specton spect_amplify(Specton s, float factor);

/* Divide all wave weights by factor. */
Specton spect_attenuate(Specton s, float factor);

/* Invert weights: w[i] = 1.0 - w[i], then normalize. */
Specton spect_invert(Specton s);

/* -------------------------------------------------------------------------
 * Arithmetic
 * ---------------------------------------------------------------------- */

Specton spect_add(Specton a, Specton b);
Specton spect_sub(Specton a, Specton b);
Specton spect_mul(Specton a, Specton b);
Specton spect_div(Specton a, Specton b);
Specton spect_pow(Specton a, Specton b);

/* -------------------------------------------------------------------------
 * Wave interference
 * ---------------------------------------------------------------------- */

/* Constructive: element-wise add weights, then normalize. */
Specton spect_spread(Specton a, Specton b);

/* Destructive: element-wise max(0, a-b), then normalize. */
Specton spect_interfere(Specton a, Specton b);

/* Correlation: element-wise multiply weights, then normalize. */
Specton spect_resonate(Specton a, Specton b);

/* -------------------------------------------------------------------------
 * Comparison
 * ---------------------------------------------------------------------- */

/* Exact equality: mode and all values must match. */
int spect_eq(Specton a, Specton b);

/* Entropy-weighted fuzzy equality. */
int spect_fuzzy_eq(Specton a, Specton b, float tolerance);

/* Collapse both, compare values: a > b. */
int spect_gt(Specton a, Specton b);

/* Collapse both, compare values: a < b. */
int spect_lt(Specton a, Specton b);

/* -------------------------------------------------------------------------
 * I/O
 * ---------------------------------------------------------------------- */

/* Print without newline. */
void spect_print(Specton s);

/* Print with newline. */
void spect_println(Specton s);

/* Print full wave distribution. */
void spect_print_wave(Specton s);

/* Return a malloc'd string representation; caller must free(). */
char* spect_to_string(Specton s);

/* -------------------------------------------------------------------------
 * Utilities
 * ---------------------------------------------------------------------- */

/* Seed the internal RNG. */
void spect_seed_rng(unsigned int seed);

int spect_is_fixed(Specton s);
int spect_is_range(Specton s);
int spect_is_wave(Specton s);

/* True if FIXED, or WAVE whose peak weight >= 0.99. */
int spect_is_certain(Specton s);

#endif /* SPECTON_H */
