/*
 * specton.c — C runtime implementation of the Specton type for SPECTRA.
 *
 * A Specton is a 10-state data unit that can exist in one of three modes:
 *   FIXED  (mode 0) — single definite digit 0–9
 *   RANGE  (mode 1) — active span [lo..hi], both inclusive
 *   WAVE   (mode 2) — all 10 states simultaneously active with float weights
 */

#include "specton.h"

/* =========================================================================
 * Internal RNG (simple LCG, seeded lazily with time)
 * ====================================================================== */

static unsigned int rng_state = 0;
static int rng_seeded = 0;

static void rng_ensure_seeded(void) {
    if (!rng_seeded) {
        rng_state = (unsigned int)time(NULL);
        rng_seeded = 1;
    }
}

/* Returns a pseudo-random float in [0.0, 1.0). */
static float rng_float(void) {
    rng_ensure_seeded();
    /* Numerical Recipes LCG constants. */
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 1) / (float)0x7FFFFFFFu;
}

void spect_seed_rng(unsigned int seed) {
    rng_state = seed;
    rng_seeded = 1;
}

/* =========================================================================
 * Internal helpers
 * ====================================================================== */

/* Clamp val to [0, SPECT_STATES-1]. */
static uint8_t clamp_state(uint8_t val) {
    return (val >= SPECT_STATES) ? (SPECT_STATES - 1) : val;
}

/* Return the sum of all wave weights. */
static float wave_sum(const float w[SPECT_STATES]) {
    float s = 0.0f;
    int i;
    for (i = 0; i < SPECT_STATES; i++) s += w[i];
    return s;
}

/* Normalize an in-place wave array.
 * If the sum is effectively 0, falls back to uniform. */
static void wave_normalize_inplace(float w[SPECT_STATES]) {
    float s = wave_sum(w);
    int i;
    if (s < 1e-9f) {
        for (i = 0; i < SPECT_STATES; i++) w[i] = 1.0f / SPECT_STATES;
    } else {
        for (i = 0; i < SPECT_STATES; i++) w[i] /= s;
    }
}

/* Return the index of the maximum weight in a wave array. */
static uint8_t wave_peak_index(const float w[SPECT_STATES]) {
    uint8_t best = 0;
    int i;
    for (i = 1; i < SPECT_STATES; i++) {
        if (w[i] > w[best]) best = (uint8_t)i;
    }
    return best;
}

/* =========================================================================
 * Constructors
 * ====================================================================== */

Specton spect_fixed(uint8_t val) {
    Specton s;
    s.mode = SPECT_FIXED;
    s.data.fixed_val = clamp_state(val);
    return s;
}

Specton spect_range(uint8_t lo, uint8_t hi) {
    Specton s;
    s.mode = SPECT_RANGE;
    s.data.range.lo = clamp_state(lo);
    s.data.range.hi = clamp_state(hi);
    /* Ensure lo <= hi; swap if necessary. */
    if (s.data.range.lo > s.data.range.hi) {
        uint8_t tmp = s.data.range.lo;
        s.data.range.lo = s.data.range.hi;
        s.data.range.hi = tmp;
    }
    return s;
}

Specton spect_wave(const float weights[SPECT_STATES]) {
    Specton s;
    int i;
    s.mode = SPECT_WAVE;
    for (i = 0; i < SPECT_STATES; i++) {
        s.data.wave[i] = (weights[i] < 0.0f) ? 0.0f : weights[i];
    }
    return s;
}

Specton spect_wave_uniform(void) {
    Specton s;
    int i;
    s.mode = SPECT_WAVE;
    for (i = 0; i < SPECT_STATES; i++) s.data.wave[i] = 0.1f;
    return s;
}

Specton spect_wave_single(uint8_t state, float weight) {
    Specton s;
    int i;
    s.mode = SPECT_WAVE;
    state = clamp_state(state);
    for (i = 0; i < SPECT_STATES; i++) s.data.wave[i] = 0.0f;
    s.data.wave[state] = (weight < 0.0f) ? 0.0f : weight;
    return s;
}

Specton spect_wave_normalized(const float weights[SPECT_STATES]) {
    Specton s = spect_wave(weights);
    return spect_normalize(s);
}

/* =========================================================================
 * Mode conversion
 * ====================================================================== */

Specton spect_to_wave(Specton s) {
    Specton out;
    int i;
    out.mode = SPECT_WAVE;

    switch (s.mode) {
    case SPECT_FIXED:
        for (i = 0; i < SPECT_STATES; i++) out.data.wave[i] = 0.0f;
        out.data.wave[s.data.fixed_val] = 1.0f;
        break;

    case SPECT_RANGE: {
        uint8_t lo = s.data.range.lo;
        uint8_t hi = s.data.range.hi;
        int span = (int)hi - (int)lo + 1;
        float w = (span > 0) ? (1.0f / (float)span) : 0.1f;
        for (i = 0; i < SPECT_STATES; i++) out.data.wave[i] = 0.0f;
        for (i = (int)lo; i <= (int)hi; i++) out.data.wave[i] = w;
        break;
    }

    case SPECT_WAVE:
        memcpy(out.data.wave, s.data.wave, sizeof(s.data.wave));
        break;
    }

    return out;
}

Specton spect_to_fixed(Specton s) {
    switch (s.mode) {
    case SPECT_FIXED:
        return s;

    case SPECT_RANGE:
        return spect_fixed((uint8_t)(((int)s.data.range.lo + (int)s.data.range.hi) / 2));

    case SPECT_WAVE:
        return spect_fixed(wave_peak_index(s.data.wave));
    }
    /* Should be unreachable; return 0 as fallback. */
    return spect_fixed(0);
}

Specton spect_to_range(Specton s) {
    switch (s.mode) {
    case SPECT_FIXED:
        return spect_range(s.data.fixed_val, s.data.fixed_val);

    case SPECT_RANGE:
        return s;

    case SPECT_WAVE: {
        /* Find the first and last state with nonzero weight. */
        int lo = -1, hi = -1, i;
        for (i = 0; i < SPECT_STATES; i++) {
            if (s.data.wave[i] > 1e-9f) {
                if (lo < 0) lo = i;
                hi = i;
            }
        }
        if (lo < 0) { lo = 0; hi = 9; } /* degenerate */
        return spect_range((uint8_t)lo, (uint8_t)hi);
    }
    }
    return spect_range(0, 9);
}

/* =========================================================================
 * Core operations
 * ====================================================================== */

uint8_t spect_collapse(Specton s) {
    switch (s.mode) {
    case SPECT_FIXED: return s.data.fixed_val;
    case SPECT_RANGE: return (uint8_t)(((int)s.data.range.lo + (int)s.data.range.hi) / 2);
    case SPECT_WAVE:  return wave_peak_index(s.data.wave);
    }
    return 0;
}

uint8_t spect_observe(Specton s) {
    int i;
    float r, cumulative;

    switch (s.mode) {
    case SPECT_FIXED:
        return s.data.fixed_val;

    case SPECT_RANGE: {
        /* Uniform sample from [lo..hi]. */
        uint8_t lo = s.data.range.lo;
        uint8_t hi = s.data.range.hi;
        int span = (int)hi - (int)lo + 1;
        if (span <= 1) return lo;
        r = rng_float();
        return (uint8_t)(lo + (uint8_t)(r * (float)span));
    }

    case SPECT_WAVE: {
        /* Build CDF from wave weights (treat negative weights as 0). */
        float total = 0.0f;
        float normalized[SPECT_STATES];
        for (i = 0; i < SPECT_STATES; i++) {
            normalized[i] = (s.data.wave[i] > 0.0f) ? s.data.wave[i] : 0.0f;
            total += normalized[i];
        }
        if (total < 1e-9f) {
            /* Degenerate wave: uniform. */
            return (uint8_t)(rng_float() * SPECT_STATES);
        }
        r = rng_float();
        cumulative = 0.0f;
        for (i = 0; i < SPECT_STATES; i++) {
            cumulative += normalized[i] / total;
            if (cumulative >= r) return (uint8_t)i;
        }
        return SPECT_STATES - 1; /* floating-point safety fallback */
    }
    }
    return 0;
}

uint8_t spect_peak(Specton s) {
    switch (s.mode) {
    case SPECT_FIXED: return s.data.fixed_val;
    case SPECT_RANGE: return (uint8_t)(((int)s.data.range.lo + (int)s.data.range.hi) / 2);
    case SPECT_WAVE:  return wave_peak_index(s.data.wave);
    }
    return 0;
}

float spect_entropy(Specton s) {
    int i;
    switch (s.mode) {
    case SPECT_FIXED:
        return 0.0f;

    case SPECT_RANGE: {
        int span = (int)s.data.range.hi - (int)s.data.range.lo + 1;
        if (span <= 1) return 0.0f;
        return (float)log2((double)span);
    }

    case SPECT_WAVE: {
        float total = wave_sum(s.data.wave);
        float entropy = 0.0f;
        if (total < 1e-9f) return 0.0f;
        for (i = 0; i < SPECT_STATES; i++) {
            float p = s.data.wave[i] / total;
            if (p > 1e-9f) {
                entropy -= p * (float)log2((double)p);
            }
        }
        return entropy;
    }
    }
    return 0.0f;
}

float spect_weight_at(Specton s, uint8_t state) {
    state = clamp_state(state);
    switch (s.mode) {
    case SPECT_FIXED:
        return (s.data.fixed_val == state) ? 1.0f : 0.0f;

    case SPECT_RANGE: {
        int span = (int)s.data.range.hi - (int)s.data.range.lo + 1;
        if (state >= s.data.range.lo && state <= s.data.range.hi) {
            return (span > 0) ? (1.0f / (float)span) : 0.0f;
        }
        return 0.0f;
    }

    case SPECT_WAVE:
        return s.data.wave[state];
    }
    return 0.0f;
}

Specton spect_normalize(Specton s) {
    if (s.mode != SPECT_WAVE) return s;
    Specton out = s;
    wave_normalize_inplace(out.data.wave);
    return out;
}

Specton spect_amplify(Specton s, float factor) {
    Specton out = spect_to_wave(s);
    int i;
    if (factor < 0.0f) factor = 0.0f;
    for (i = 0; i < SPECT_STATES; i++) out.data.wave[i] *= factor;
    return out;
}

Specton spect_attenuate(Specton s, float factor) {
    Specton out = spect_to_wave(s);
    int i;
    if (factor < 1e-9f) factor = 1e-9f; /* avoid division by zero */
    for (i = 0; i < SPECT_STATES; i++) out.data.wave[i] /= factor;
    return out;
}

Specton spect_invert(Specton s) {
    Specton out = spect_to_wave(s);
    int i;
    for (i = 0; i < SPECT_STATES; i++) {
        out.data.wave[i] = 1.0f - out.data.wave[i];
        if (out.data.wave[i] < 0.0f) out.data.wave[i] = 0.0f;
    }
    return spect_normalize(out);
}

/* =========================================================================
 * Internal: promote two Spectons to a common mode before arithmetic.
 * Priority: FIXED < RANGE < WAVE
 * ====================================================================== */

static void promote_pair(Specton a, Specton b, Specton *pa, Specton *pb) {
    if (a.mode == b.mode) {
        *pa = a;
        *pb = b;
        return;
    }
    /* Promote lower-priority to the higher-priority mode. */
    if (a.mode < b.mode) {
        if (b.mode == SPECT_WAVE) {
            *pa = spect_to_wave(a);
            *pb = b;
        } else {
            /* b is RANGE, a is FIXED */
            *pa = spect_to_range(a);
            *pb = b;
        }
    } else {
        if (a.mode == SPECT_WAVE) {
            *pa = a;
            *pb = spect_to_wave(b);
        } else {
            /* a is RANGE, b is FIXED */
            *pa = a;
            *pb = spect_to_range(b);
        }
    }
}

/* =========================================================================
 * Arithmetic
 * ====================================================================== */

Specton spect_add(Specton a, Specton b) {
    Specton pa, pb;

    /* Handle same-mode cases directly before promotion. */
    if (a.mode == SPECT_FIXED && b.mode == SPECT_FIXED) {
        int sum = (int)a.data.fixed_val + (int)b.data.fixed_val;
        if (sum < SPECT_STATES) {
            return spect_fixed((uint8_t)sum);
        } else {
            /* Spread result: peak at sum%10, taper to neighbors. */
            float w[SPECT_STATES] = {0};
            uint8_t center = (uint8_t)(sum % SPECT_STATES);
            uint8_t left   = (uint8_t)((center + SPECT_STATES - 1) % SPECT_STATES);
            uint8_t right  = (uint8_t)((center + 1) % SPECT_STATES);
            w[center] = 0.6f;
            w[left]   = 0.2f;
            w[right]  = 0.2f;
            return spect_wave_normalized(w);
        }
    }

    if (a.mode == SPECT_RANGE && b.mode == SPECT_RANGE) {
        uint8_t lo = (uint8_t)(((int)a.data.range.lo + (int)b.data.range.lo) % SPECT_STATES);
        uint8_t hi = (uint8_t)(((int)a.data.range.hi + (int)b.data.range.hi) % SPECT_STATES);
        return spect_range(lo, hi);
    }

    /* Mixed: FIXED + WAVE → circular shift. */
    if (a.mode == SPECT_FIXED && b.mode == SPECT_WAVE) {
        float w[SPECT_STATES] = {0};
        int i;
        for (i = 0; i < SPECT_STATES; i++) {
            int dest = ((int)i + (int)a.data.fixed_val) % SPECT_STATES;
            w[dest] += b.data.wave[i];
        }
        return spect_wave_normalized(w);
    }
    if (a.mode == SPECT_WAVE && b.mode == SPECT_FIXED) {
        float w[SPECT_STATES] = {0};
        int i;
        for (i = 0; i < SPECT_STATES; i++) {
            int dest = ((int)i + (int)b.data.fixed_val) % SPECT_STATES;
            w[dest] += a.data.wave[i];
        }
        return spect_wave_normalized(w);
    }

    /* All remaining mixed cases: promote then operate. */
    promote_pair(a, b, &pa, &pb);

    if (pa.mode == SPECT_WAVE && pb.mode == SPECT_WAVE) {
        /* Element-wise add, normalize. */
        return spect_spread(pa, pb);
    }

    if (pa.mode == SPECT_RANGE && pb.mode == SPECT_RANGE) {
        uint8_t lo = (uint8_t)(((int)pa.data.range.lo + (int)pb.data.range.lo) % SPECT_STATES);
        uint8_t hi = (uint8_t)(((int)pa.data.range.hi + (int)pb.data.range.hi) % SPECT_STATES);
        return spect_range(lo, hi);
    }

    /* Fallback: convert both to wave. */
    return spect_spread(spect_to_wave(a), spect_to_wave(b));
}

Specton spect_sub(Specton a, Specton b) {
    int i;

    if (a.mode == SPECT_FIXED && b.mode == SPECT_FIXED) {
        int diff = ((int)a.data.fixed_val - (int)b.data.fixed_val + SPECT_STATES) % SPECT_STATES;
        return spect_fixed((uint8_t)diff);
    }

    if (a.mode == SPECT_RANGE && b.mode == SPECT_RANGE) {
        uint8_t lo = (uint8_t)(((int)a.data.range.lo - (int)b.data.range.hi + SPECT_STATES * 2) % SPECT_STATES);
        uint8_t hi = (uint8_t)(((int)a.data.range.hi - (int)b.data.range.lo + SPECT_STATES * 2) % SPECT_STATES);
        return spect_range(lo, hi);
    }

    /* FIXED - WAVE: shift wave in negative direction. */
    if (a.mode == SPECT_FIXED && b.mode == SPECT_WAVE) {
        float w[SPECT_STATES] = {0};
        for (i = 0; i < SPECT_STATES; i++) {
            int dest = ((int)a.data.fixed_val - (int)i + SPECT_STATES) % SPECT_STATES;
            w[dest] += b.data.wave[i];
        }
        return spect_wave_normalized(w);
    }

    /* WAVE - FIXED: shift wave in negative direction by fixed amount. */
    if (a.mode == SPECT_WAVE && b.mode == SPECT_FIXED) {
        float w[SPECT_STATES] = {0};
        for (i = 0; i < SPECT_STATES; i++) {
            int dest = ((int)i - (int)b.data.fixed_val + SPECT_STATES) % SPECT_STATES;
            w[dest] += a.data.wave[i];
        }
        return spect_wave_normalized(w);
    }

    /* General: promote, then element-wise destructive interference. */
    {
        Specton wa = spect_to_wave(a);
        Specton wb = spect_to_wave(b);
        return spect_interfere(wa, wb);
    }
}

Specton spect_mul(Specton a, Specton b) {
    if (a.mode == SPECT_FIXED && b.mode == SPECT_FIXED) {
        int prod = (int)a.data.fixed_val * (int)b.data.fixed_val;
        if (prod < SPECT_STATES) {
            return spect_fixed((uint8_t)prod);
        } else {
            /* Spread: peak at prod%10 with neighbors. */
            float w[SPECT_STATES] = {0};
            uint8_t center = (uint8_t)(prod % SPECT_STATES);
            uint8_t left   = (uint8_t)((center + SPECT_STATES - 1) % SPECT_STATES);
            uint8_t right  = (uint8_t)((center + 1) % SPECT_STATES);
            w[center] = 0.6f;
            w[left]   = 0.2f;
            w[right]  = 0.2f;
            return spect_wave_normalized(w);
        }
    }

    if (a.mode == SPECT_RANGE && b.mode == SPECT_RANGE) {
        uint8_t lo = (uint8_t)(((int)a.data.range.lo * (int)b.data.range.lo) % SPECT_STATES);
        uint8_t hi = (uint8_t)(((int)a.data.range.hi * (int)b.data.range.hi) % SPECT_STATES);
        return spect_range(lo, hi);
    }

    /* WAVE * WAVE or any mixed: promote to wave, correlate. */
    return spect_resonate(spect_to_wave(a), spect_to_wave(b));
}

Specton spect_div(Specton a, Specton b) {
    uint8_t divisor = spect_collapse(b);

    if (a.mode == SPECT_FIXED) {
        if (divisor == 0) {
            /* Division by zero: return uniform wave (undefined). */
            return spect_wave_uniform();
        }
        uint8_t result = (uint8_t)((int)a.data.fixed_val / (int)divisor);
        return spect_fixed(result);
    }

    if (a.mode == SPECT_RANGE) {
        if (divisor == 0) return spect_wave_uniform();
        uint8_t lo = (uint8_t)((int)a.data.range.lo / (int)divisor);
        uint8_t hi = (uint8_t)((int)a.data.range.hi / (int)divisor);
        return spect_range(lo, hi);
    }

    /* WAVE: divide each weight by the divisor's float value. */
    {
        float dv = (float)divisor;
        if (dv < 1e-9f) return spect_wave_uniform();
        Specton out = a;
        int i;
        for (i = 0; i < SPECT_STATES; i++) out.data.wave[i] /= dv;
        return spect_normalize(out);
    }
}

Specton spect_pow(Specton a, Specton b) {
    uint8_t exponent = spect_collapse(b);

    if (a.mode == SPECT_FIXED) {
        int result = 1;
        int i;
        for (i = 0; i < (int)exponent; i++) result = (result * (int)a.data.fixed_val) % SPECT_STATES;
        return spect_fixed((uint8_t)result);
    }

    if (a.mode == SPECT_RANGE) {
        int lo_p = 1, hi_p = 1, i;
        for (i = 0; i < (int)exponent; i++) {
            lo_p = (lo_p * (int)a.data.range.lo) % SPECT_STATES;
            hi_p = (hi_p * (int)a.data.range.hi) % SPECT_STATES;
        }
        return spect_range((uint8_t)lo_p, (uint8_t)hi_p);
    }

    /* WAVE^exponent: raise each weight to the power, re-normalize. */
    {
        Specton out = a;
        int i;
        float exp_f = (float)exponent;
        for (i = 0; i < SPECT_STATES; i++) {
            if (out.data.wave[i] < 0.0f) out.data.wave[i] = 0.0f;
            out.data.wave[i] = (exponent == 0) ? 1.0f : (float)pow((double)out.data.wave[i], (double)exp_f);
        }
        return spect_normalize(out);
    }
}

/* =========================================================================
 * Wave interference
 * ====================================================================== */

Specton spect_spread(Specton a, Specton b) {
    Specton wa = spect_to_wave(a);
    Specton wb = spect_to_wave(b);
    float w[SPECT_STATES];
    int i;
    for (i = 0; i < SPECT_STATES; i++) w[i] = wa.data.wave[i] + wb.data.wave[i];
    wave_normalize_inplace(w);
    return spect_wave(w);
}

Specton spect_interfere(Specton a, Specton b) {
    Specton wa = spect_to_wave(a);
    Specton wb = spect_to_wave(b);
    float w[SPECT_STATES];
    int i;
    for (i = 0; i < SPECT_STATES; i++) {
        float diff = wa.data.wave[i] - wb.data.wave[i];
        w[i] = (diff > 0.0f) ? diff : 0.0f;
    }
    wave_normalize_inplace(w);
    return spect_wave(w);
}

Specton spect_resonate(Specton a, Specton b) {
    Specton wa = spect_to_wave(a);
    Specton wb = spect_to_wave(b);
    float w[SPECT_STATES];
    int i;
    for (i = 0; i < SPECT_STATES; i++) {
        w[i] = wa.data.wave[i] * wb.data.wave[i];
        if (w[i] < 0.0f) w[i] = 0.0f;
    }
    wave_normalize_inplace(w);
    return spect_wave(w);
}

/* =========================================================================
 * Comparison
 * ====================================================================== */

int spect_eq(Specton a, Specton b) {
    int i;
    if (a.mode != b.mode) return 0;
    switch (a.mode) {
    case SPECT_FIXED:
        return a.data.fixed_val == b.data.fixed_val;
    case SPECT_RANGE:
        return (a.data.range.lo == b.data.range.lo) &&
               (a.data.range.hi == b.data.range.hi);
    case SPECT_WAVE:
        for (i = 0; i < SPECT_STATES; i++) {
            if (fabsf(a.data.wave[i] - b.data.wave[i]) > 1e-6f) return 0;
        }
        return 1;
    }
    return 0;
}

int spect_fuzzy_eq(Specton a, Specton b, float tolerance) {
    /*
     * Entropy-weighted similarity: convert both to normalized wave distributions
     * and compute the L1 distance between them. If the distance is within
     * tolerance, consider them equal.
     *
     * The entropy weighting scales tolerance: a higher combined entropy means
     * we allow a larger distance (the distributions are inherently "blurry").
     */
    Specton wa = spect_normalize(spect_to_wave(a));
    Specton wb = spect_normalize(spect_to_wave(b));
    float l1 = 0.0f;
    float entropy_scale;
    int i;

    for (i = 0; i < SPECT_STATES; i++) {
        l1 += fabsf(wa.data.wave[i] - wb.data.wave[i]);
    }

    /* Scale tolerance by average entropy (max entropy = log2(10) ≈ 3.32). */
    entropy_scale = 1.0f + (spect_entropy(a) + spect_entropy(b)) / (2.0f * 3.3219f);
    return (l1 / 2.0f) <= (tolerance * entropy_scale);
}

int spect_gt(Specton a, Specton b) {
    return (int)spect_collapse(a) > (int)spect_collapse(b);
}

int spect_lt(Specton a, Specton b) {
    return (int)spect_collapse(a) < (int)spect_collapse(b);
}

/* =========================================================================
 * I/O
 * ====================================================================== */

char* spect_to_string(Specton s) {
    /* Worst case for WAVE: "~{0:0.00, 1:0.00, ..., 9:0.00}~\0" ≈ 128 bytes. */
    char *buf = (char*)malloc(256);
    int i;
    int pos = 0;
    int first;

    if (!buf) return NULL;

    switch (s.mode) {
    case SPECT_FIXED:
        pos += snprintf(buf + pos, 256 - pos, "%d", (int)s.data.fixed_val);
        break;

    case SPECT_RANGE:
        pos += snprintf(buf + pos, 256 - pos, "|[%d..%d]|",
                        (int)s.data.range.lo, (int)s.data.range.hi);
        break;

    case SPECT_WAVE:
        pos += snprintf(buf + pos, 256 - pos, "~{");
        first = 1;
        for (i = 0; i < SPECT_STATES; i++) {
            if (s.data.wave[i] > 0.005f) {
                if (!first) pos += snprintf(buf + pos, 256 - pos, ", ");
                pos += snprintf(buf + pos, 256 - pos, "%d:%.2f", i, (double)s.data.wave[i]);
                first = 0;
            }
        }
        pos += snprintf(buf + pos, 256 - pos, "}~");
        break;
    }

    buf[pos] = '\0';
    return buf;
}

void spect_print(Specton s) {
    char *str = spect_to_string(s);
    if (str) {
        fputs(str, stdout);
        free(str);
    }
}

void spect_println(Specton s) {
    spect_print(s);
    putchar('\n');
}

void spect_print_wave(Specton s) {
    Specton w = spect_to_wave(s);
    int i;
    printf("Wave distribution:\n");
    for (i = 0; i < SPECT_STATES; i++) {
        float wt = w.data.wave[i];
        int bars = (int)(wt * 40.0f + 0.5f);
        int j;
        printf("  [%d] %6.4f |", i, (double)wt);
        for (j = 0; j < bars; j++) putchar('#');
        putchar('\n');
    }
}

/* =========================================================================
 * Utilities
 * ====================================================================== */

int spect_is_fixed(Specton s) { return s.mode == SPECT_FIXED; }
int spect_is_range(Specton s) { return s.mode == SPECT_RANGE; }
int spect_is_wave(Specton s)  { return s.mode == SPECT_WAVE;  }

int spect_is_certain(Specton s) {
    if (s.mode == SPECT_FIXED) return 1;
    if (s.mode == SPECT_WAVE) {
        uint8_t pk = wave_peak_index(s.data.wave);
        return s.data.wave[pk] >= 0.99f;
    }
    return 0;
}
