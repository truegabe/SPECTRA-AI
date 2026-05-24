#include "tensor.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* =========================================================
 * Internal helpers
 * ========================================================= */

/* Generate a random float in [0, 1) */
static float rand_float(void)
{
    return (float)rand() / ((float)RAND_MAX + 1.0f);
}

/* Build a random WAVE Specton: 10 random weights, normalised */
static Specton make_random_wave(void)
{
    float weights[SPECT_STATES];
    for (int i = 0; i < SPECT_STATES; i++) {
        weights[i] = rand_float() + 1e-6f; /* keep positive so sum > 0 */
    }
    Specton s = spect_wave(weights);
    return spect_normalize(s);
}

/* =========================================================
 * SpectArray — allocation / deallocation
 * ========================================================= */

SpectArray* sarray_alloc(size_t length)
{
    SpectArray* arr = (SpectArray*)malloc(sizeof(SpectArray));
    if (!arr) {
        fprintf(stderr, "sarray_alloc: malloc failed\n");
        return NULL;
    }
    arr->data = (Specton*)calloc(length, sizeof(Specton));
    if (!arr->data) {
        fprintf(stderr, "sarray_alloc: calloc failed\n");
        free(arr);
        return NULL;
    }
    arr->length = length;
    /* calloc zero-fills memory; mode == 0 == SPECT_FIXED, fixed_val == 0 */
    return arr;
}

void sarray_free(SpectArray* arr)
{
    if (!arr) return;
    free(arr->data);
    free(arr);
}

/* =========================================================
 * SpectArray — constructors
 * ========================================================= */

SpectArray* sarray_zeros(size_t n)
{
    /* sarray_alloc already zero-initialises (FIXED 0) */
    return sarray_alloc(n);
}

SpectArray* sarray_ones(size_t n)
{
    SpectArray* arr = sarray_alloc(n);
    if (!arr) return NULL;
    Specton one = spect_fixed(1);
    for (size_t i = 0; i < n; i++) {
        arr->data[i] = one;
    }
    return arr;
}

SpectArray* sarray_fill(size_t n, Specton val)
{
    SpectArray* arr = sarray_alloc(n);
    if (!arr) return NULL;
    for (size_t i = 0; i < n; i++) {
        arr->data[i] = val;
    }
    return arr;
}

SpectArray* sarray_random_wave(size_t n)
{
    SpectArray* arr = sarray_alloc(n);
    if (!arr) return NULL;
    for (size_t i = 0; i < n; i++) {
        arr->data[i] = make_random_wave();
    }
    return arr;
}

/* =========================================================
 * SpectArray — element access
 * ========================================================= */

Specton sarray_get(SpectArray* arr, size_t i)
{
    if (!arr) {
        fprintf(stderr, "sarray_get: NULL array\n");
        return spect_fixed(0);
    }
    if (i >= arr->length) {
        fprintf(stderr, "sarray_get: index %zu out of range (length %zu)\n",
                i, arr->length);
        return spect_fixed(0);
    }
    return arr->data[i];
}

void sarray_set(SpectArray* arr, size_t i, Specton val)
{
    if (!arr) {
        fprintf(stderr, "sarray_set: NULL array\n");
        return;
    }
    if (i >= arr->length) {
        fprintf(stderr, "sarray_set: index %zu out of range (length %zu)\n",
                i, arr->length);
        return;
    }
    arr->data[i] = val;
}

/* =========================================================
 * SpectArray — arithmetic
 * ========================================================= */

/*
 * sarray_dot:
 *   Accumulate element-wise products via spect_add( acc, spect_mul(a[i], b[i]) ).
 *   The result is converted to a WAVE representation to carry the full signal.
 */
Specton sarray_dot(SpectArray* a, SpectArray* b)
{
    if (!a || !b) {
        fprintf(stderr, "sarray_dot: NULL input\n");
        return spect_fixed(0);
    }
    if (a->length != b->length) {
        fprintf(stderr, "sarray_dot: length mismatch (%zu vs %zu)\n",
                a->length, b->length);
        return spect_fixed(0);
    }

    Specton acc = spect_fixed(0);
    for (size_t i = 0; i < a->length; i++) {
        Specton prod = spect_mul(a->data[i], b->data[i]);
        acc = spect_add(acc, prod);
    }
    /* Return as WAVE so the caller sees the full accumulated distribution */
    return spect_to_wave(acc);
}

SpectArray* sarray_add(SpectArray* a, SpectArray* b)
{
    if (!a || !b) {
        fprintf(stderr, "sarray_add: NULL input\n");
        return NULL;
    }
    if (a->length != b->length) {
        fprintf(stderr, "sarray_add: length mismatch (%zu vs %zu)\n",
                a->length, b->length);
        return NULL;
    }
    SpectArray* out = sarray_alloc(a->length);
    if (!out) return NULL;
    for (size_t i = 0; i < a->length; i++) {
        out->data[i] = spect_add(a->data[i], b->data[i]);
    }
    return out;
}

SpectArray* sarray_mul(SpectArray* a, SpectArray* b)
{
    if (!a || !b) {
        fprintf(stderr, "sarray_mul: NULL input\n");
        return NULL;
    }
    if (a->length != b->length) {
        fprintf(stderr, "sarray_mul: length mismatch (%zu vs %zu)\n",
                a->length, b->length);
        return NULL;
    }
    SpectArray* out = sarray_alloc(a->length);
    if (!out) return NULL;
    for (size_t i = 0; i < a->length; i++) {
        out->data[i] = spect_mul(a->data[i], b->data[i]);
    }
    return out;
}

/* =========================================================
 * SpectArray — transformations
 * ========================================================= */

SpectArray* sarray_collapse_all(SpectArray* arr)
{
    if (!arr) {
        fprintf(stderr, "sarray_collapse_all: NULL input\n");
        return NULL;
    }
    SpectArray* out = sarray_alloc(arr->length);
    if (!out) return NULL;
    for (size_t i = 0; i < arr->length; i++) {
        out->data[i] = spect_to_fixed(arr->data[i]);
    }
    return out;
}

/*
 * sarray_entropy_map:
 *   spect_entropy returns a float (typically 0..log(SPECT_STATES)).
 *   We scale it to an integer in [0, 9] and return as FIXED Spectons.
 */
SpectArray* sarray_entropy_map(SpectArray* arr)
{
    if (!arr) {
        fprintf(stderr, "sarray_entropy_map: NULL input\n");
        return NULL;
    }
    float max_entropy = logf((float)SPECT_STATES); /* ln(10) */
    SpectArray* out = sarray_alloc(arr->length);
    if (!out) return NULL;
    for (size_t i = 0; i < arr->length; i++) {
        float e = spect_entropy(arr->data[i]);
        /* Scale to [0, SPECT_STATES-1] */
        int scaled = (int)(e / max_entropy * (float)(SPECT_STATES - 1) + 0.5f);
        if (scaled < 0) scaled = 0;
        if (scaled > SPECT_STATES - 1) scaled = SPECT_STATES - 1;
        out->data[i] = spect_fixed((uint8_t)scaled);
    }
    return out;
}

/* =========================================================
 * SpectArray — queries / printing
 * ========================================================= */

float sarray_mean_entropy(SpectArray* arr)
{
    if (!arr || arr->length == 0) {
        fprintf(stderr, "sarray_mean_entropy: NULL or empty array\n");
        return 0.0f;
    }
    float sum = 0.0f;
    for (size_t i = 0; i < arr->length; i++) {
        sum += spect_entropy(arr->data[i]);
    }
    return sum / (float)arr->length;
}

void sarray_print(SpectArray* arr)
{
    if (!arr) {
        fprintf(stderr, "sarray_print: NULL array\n");
        return;
    }
    printf("SpectArray[%zu]:\n", arr->length);
    for (size_t i = 0; i < arr->length; i++) {
        printf("  [%zu] ", i);
        spect_println(arr->data[i]);
    }
}

size_t sarray_shape(SpectArray* arr)
{
    if (!arr) {
        fprintf(stderr, "sarray_shape: NULL array\n");
        return 0;
    }
    return arr->length;
}


/* =========================================================
 * SpectMatrix — allocation / deallocation
 * ========================================================= */

SpectMatrix* smat_alloc(size_t rows, size_t cols)
{
    SpectMatrix* m = (SpectMatrix*)malloc(sizeof(SpectMatrix));
    if (!m) {
        fprintf(stderr, "smat_alloc: malloc failed\n");
        return NULL;
    }
    m->data = (Specton*)calloc(rows * cols, sizeof(Specton));
    if (!m->data) {
        fprintf(stderr, "smat_alloc: calloc failed\n");
        free(m);
        return NULL;
    }
    m->rows = rows;
    m->cols = cols;
    return m;
}

void smat_free(SpectMatrix* mat)
{
    if (!mat) return;
    free(mat->data);
    free(mat);
}

/* =========================================================
 * SpectMatrix — constructors
 * ========================================================= */

SpectMatrix* smat_zeros(size_t rows, size_t cols)
{
    return smat_alloc(rows, cols); /* calloc gives FIXED 0 */
}

SpectMatrix* smat_ones(size_t rows, size_t cols)
{
    SpectMatrix* m = smat_alloc(rows, cols);
    if (!m) return NULL;
    Specton one = spect_fixed(1);
    size_t total = rows * cols;
    for (size_t i = 0; i < total; i++) {
        m->data[i] = one;
    }
    return m;
}

SpectMatrix* smat_fill(size_t rows, size_t cols, Specton val)
{
    SpectMatrix* m = smat_alloc(rows, cols);
    if (!m) return NULL;
    size_t total = rows * cols;
    for (size_t i = 0; i < total; i++) {
        m->data[i] = val;
    }
    return m;
}

SpectMatrix* smat_random_wave(size_t rows, size_t cols)
{
    SpectMatrix* m = smat_alloc(rows, cols);
    if (!m) return NULL;
    size_t total = rows * cols;
    for (size_t i = 0; i < total; i++) {
        m->data[i] = make_random_wave();
    }
    return m;
}

SpectMatrix* smat_identity(size_t n)
{
    SpectMatrix* m = smat_zeros(n, n);
    if (!m) return NULL;
    Specton one = spect_fixed(1);
    for (size_t i = 0; i < n; i++) {
        m->data[i * n + i] = one;
    }
    return m;
}

/* =========================================================
 * SpectMatrix — element access
 * ========================================================= */

Specton smat_get(SpectMatrix* m, size_t r, size_t c)
{
    if (!m) {
        fprintf(stderr, "smat_get: NULL matrix\n");
        return spect_fixed(0);
    }
    if (r >= m->rows || c >= m->cols) {
        fprintf(stderr, "smat_get: index (%zu,%zu) out of range (%zu x %zu)\n",
                r, c, m->rows, m->cols);
        return spect_fixed(0);
    }
    return m->data[r * m->cols + c];
}

void smat_set(SpectMatrix* m, size_t r, size_t c, Specton val)
{
    if (!m) {
        fprintf(stderr, "smat_set: NULL matrix\n");
        return;
    }
    if (r >= m->rows || c >= m->cols) {
        fprintf(stderr, "smat_set: index (%zu,%zu) out of range (%zu x %zu)\n",
                r, c, m->rows, m->cols);
        return;
    }
    m->data[r * m->cols + c] = val;
}

/* =========================================================
 * SpectMatrix — arithmetic
 * ========================================================= */

/*
 * smat_matmul: C[i][j] = sum_k  spect_mul(A[i][k], B[k][j])
 *              accumulated with spect_add.
 * Requires a->cols == b->rows.
 */
SpectMatrix* smat_matmul(SpectMatrix* a, SpectMatrix* b)
{
    if (!a || !b) {
        fprintf(stderr, "smat_matmul: NULL input\n");
        return NULL;
    }
    if (a->cols != b->rows) {
        fprintf(stderr, "smat_matmul: dimension mismatch (%zu x %zu) * (%zu x %zu)\n",
                a->rows, a->cols, b->rows, b->cols);
        return NULL;
    }

    SpectMatrix* out = smat_zeros(a->rows, b->cols);
    if (!out) return NULL;

    for (size_t i = 0; i < a->rows; i++) {
        for (size_t j = 0; j < b->cols; j++) {
            Specton acc = spect_fixed(0);
            for (size_t k = 0; k < a->cols; k++) {
                Specton prod = spect_mul(a->data[i * a->cols + k],
                                        b->data[k * b->cols + j]);
                acc = spect_add(acc, prod);
            }
            out->data[i * out->cols + j] = acc;
        }
    }
    return out;
}

SpectMatrix* smat_transpose(SpectMatrix* m)
{
    if (!m) {
        fprintf(stderr, "smat_transpose: NULL input\n");
        return NULL;
    }
    SpectMatrix* out = smat_alloc(m->cols, m->rows);
    if (!out) return NULL;
    for (size_t r = 0; r < m->rows; r++) {
        for (size_t c = 0; c < m->cols; c++) {
            out->data[c * out->cols + r] = m->data[r * m->cols + c];
        }
    }
    return out;
}

SpectMatrix* smat_add(SpectMatrix* a, SpectMatrix* b)
{
    if (!a || !b) {
        fprintf(stderr, "smat_add: NULL input\n");
        return NULL;
    }
    if (a->rows != b->rows || a->cols != b->cols) {
        fprintf(stderr, "smat_add: shape mismatch (%zu x %zu) vs (%zu x %zu)\n",
                a->rows, a->cols, b->rows, b->cols);
        return NULL;
    }
    SpectMatrix* out = smat_alloc(a->rows, a->cols);
    if (!out) return NULL;
    size_t total = a->rows * a->cols;
    for (size_t i = 0; i < total; i++) {
        out->data[i] = spect_add(a->data[i], b->data[i]);
    }
    return out;
}

SpectMatrix* smat_mul(SpectMatrix* a, SpectMatrix* b)
{
    if (!a || !b) {
        fprintf(stderr, "smat_mul: NULL input\n");
        return NULL;
    }
    if (a->rows != b->rows || a->cols != b->cols) {
        fprintf(stderr, "smat_mul: shape mismatch (%zu x %zu) vs (%zu x %zu)\n",
                a->rows, a->cols, b->rows, b->cols);
        return NULL;
    }
    SpectMatrix* out = smat_alloc(a->rows, a->cols);
    if (!out) return NULL;
    size_t total = a->rows * a->cols;
    for (size_t i = 0; i < total; i++) {
        out->data[i] = spect_mul(a->data[i], b->data[i]);
    }
    return out;
}

/* =========================================================
 * SpectMatrix — transformations
 * ========================================================= */

SpectMatrix* smat_collapse_all(SpectMatrix* m)
{
    if (!m) {
        fprintf(stderr, "smat_collapse_all: NULL input\n");
        return NULL;
    }
    SpectMatrix* out = smat_alloc(m->rows, m->cols);
    if (!out) return NULL;
    size_t total = m->rows * m->cols;
    for (size_t i = 0; i < total; i++) {
        out->data[i] = spect_to_fixed(m->data[i]);
    }
    return out;
}

SpectMatrix* smat_entropy_map(SpectMatrix* m)
{
    if (!m) {
        fprintf(stderr, "smat_entropy_map: NULL input\n");
        return NULL;
    }
    float max_entropy = logf((float)SPECT_STATES);
    SpectMatrix* out = smat_alloc(m->rows, m->cols);
    if (!out) return NULL;
    size_t total = m->rows * m->cols;
    for (size_t i = 0; i < total; i++) {
        float e = spect_entropy(m->data[i]);
        int scaled = (int)(e / max_entropy * (float)(SPECT_STATES - 1) + 0.5f);
        if (scaled < 0) scaled = 0;
        if (scaled > SPECT_STATES - 1) scaled = SPECT_STATES - 1;
        out->data[i] = spect_fixed((uint8_t)scaled);
    }
    return out;
}

/* =========================================================
 * SpectMatrix — row / column extraction
 * ========================================================= */

SpectArray* smat_row(SpectMatrix* m, size_t r)
{
    if (!m) {
        fprintf(stderr, "smat_row: NULL matrix\n");
        return NULL;
    }
    if (r >= m->rows) {
        fprintf(stderr, "smat_row: row %zu out of range (rows=%zu)\n", r, m->rows);
        return NULL;
    }
    SpectArray* arr = sarray_alloc(m->cols);
    if (!arr) return NULL;
    memcpy(arr->data, m->data + r * m->cols, m->cols * sizeof(Specton));
    return arr;
}

SpectArray* smat_col(SpectMatrix* m, size_t c)
{
    if (!m) {
        fprintf(stderr, "smat_col: NULL matrix\n");
        return NULL;
    }
    if (c >= m->cols) {
        fprintf(stderr, "smat_col: col %zu out of range (cols=%zu)\n", c, m->cols);
        return NULL;
    }
    SpectArray* arr = sarray_alloc(m->rows);
    if (!arr) return NULL;
    for (size_t r = 0; r < m->rows; r++) {
        arr->data[r] = m->data[r * m->cols + c];
    }
    return arr;
}

/* =========================================================
 * SpectMatrix — printing
 * ========================================================= */

void smat_print(SpectMatrix* m)
{
    if (!m) {
        fprintf(stderr, "smat_print: NULL matrix\n");
        return;
    }
    printf("SpectMatrix[%zu x %zu]:\n", m->rows, m->cols);
    for (size_t r = 0; r < m->rows; r++) {
        printf("  row %zu:\n", r);
        for (size_t c = 0; c < m->cols; c++) {
            printf("    [%zu][%zu] ", r, c);
            spect_println(m->data[r * m->cols + c]);
        }
    }
}


/* =========================================================
 * SpectTensor — allocation / deallocation
 * ========================================================= */

SpectTensor* stensor_alloc(size_t ndim, size_t* shape)
{
    if (!shape && ndim > 0) {
        fprintf(stderr, "stensor_alloc: NULL shape with ndim > 0\n");
        return NULL;
    }

    SpectTensor* t = (SpectTensor*)malloc(sizeof(SpectTensor));
    if (!t) {
        fprintf(stderr, "stensor_alloc: malloc failed for header\n");
        return NULL;
    }

    t->ndim = ndim;

    /* Copy the shape */
    if (ndim > 0) {
        t->shape = (size_t*)malloc(ndim * sizeof(size_t));
        if (!t->shape) {
            fprintf(stderr, "stensor_alloc: malloc failed for shape\n");
            free(t);
            return NULL;
        }
        memcpy(t->shape, shape, ndim * sizeof(size_t));
    } else {
        t->shape = NULL;
    }

    /* Compute total number of elements */
    size_t total = 1;
    for (size_t i = 0; i < ndim; i++) {
        total *= shape[i];
    }
    t->total = total;

    t->data = (Specton*)calloc(total, sizeof(Specton));
    if (!t->data) {
        fprintf(stderr, "stensor_alloc: calloc failed for data\n");
        free(t->shape);
        free(t);
        return NULL;
    }

    return t;
}

void stensor_free(SpectTensor* t)
{
    if (!t) return;
    free(t->data);
    free(t->shape);
    free(t);
}

/* =========================================================
 * SpectTensor — constructors
 * ========================================================= */

SpectTensor* stensor_zeros(size_t ndim, size_t* shape)
{
    /* stensor_alloc uses calloc, which gives FIXED 0 already */
    return stensor_alloc(ndim, shape);
}

SpectTensor* stensor_random_wave(size_t ndim, size_t* shape)
{
    SpectTensor* t = stensor_alloc(ndim, shape);
    if (!t) return NULL;
    for (size_t i = 0; i < t->total; i++) {
        t->data[i] = make_random_wave();
    }
    return t;
}

/* =========================================================
 * SpectTensor — index computation
 * ========================================================= */

/*
 * Row-major (C-order) flat index:
 *   flat = indices[0]*shape[1]*...*shape[ndim-1]
 *         + indices[1]*shape[2]*...*shape[ndim-1]
 *         + ...
 *         + indices[ndim-1]
 */
size_t stensor_flat_index(SpectTensor* t, size_t* indices)
{
    if (!t || !indices) {
        fprintf(stderr, "stensor_flat_index: NULL argument\n");
        return 0;
    }
    size_t flat = 0;
    size_t stride = 1;
    /* Compute strides from the last dimension backwards */
    for (size_t i = t->ndim; i-- > 0; ) {
        flat += indices[i] * stride;
        stride *= t->shape[i];
    }
    return flat;
}

/* =========================================================
 * SpectTensor — element access
 * ========================================================= */

Specton stensor_get(SpectTensor* t, size_t* indices)
{
    if (!t || !indices) {
        fprintf(stderr, "stensor_get: NULL argument\n");
        return spect_fixed(0);
    }
    size_t flat = stensor_flat_index(t, indices);
    if (flat >= t->total) {
        fprintf(stderr, "stensor_get: flat index %zu out of range (total=%zu)\n",
                flat, t->total);
        return spect_fixed(0);
    }
    return t->data[flat];
}

void stensor_set(SpectTensor* t, size_t* indices, Specton val)
{
    if (!t || !indices) {
        fprintf(stderr, "stensor_set: NULL argument\n");
        return;
    }
    size_t flat = stensor_flat_index(t, indices);
    if (flat >= t->total) {
        fprintf(stderr, "stensor_set: flat index %zu out of range (total=%zu)\n",
                flat, t->total);
        return;
    }
    t->data[flat] = val;
}

/* =========================================================
 * SpectTensor — printing
 * ========================================================= */

void stensor_print_shape(SpectTensor* t)
{
    if (!t) {
        fprintf(stderr, "stensor_print_shape: NULL tensor\n");
        return;
    }
    printf("[");
    for (size_t i = 0; i < t->ndim; i++) {
        printf("%zu", t->shape[i]);
        if (i + 1 < t->ndim) printf(", ");
    }
    printf("]\n");
}
