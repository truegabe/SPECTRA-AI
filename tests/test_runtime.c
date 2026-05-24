#include <stdio.h>
#include <assert.h>
#include "../runtime/specton.h"
#include "../runtime/tensor.h"
#include "../runtime/memory.h"

static int passed = 0;
static int failed = 0;

#define TEST(name, cond) do { \
    if (cond) { printf("  PASS  %s\n", name); passed++; } \
    else      { printf("  FAIL  %s\n", name); failed++; } \
} while(0)

void test_specton_fixed() {
    printf("\n[specton fixed]\n");
    Specton s = spect_fixed(7);
    TEST("mode is FIXED",   s.mode == SPECT_FIXED);
    TEST("value is 7",      s.data.fixed_val == 7);
    TEST("collapse == 7",   spect_collapse(s) == 7);
    TEST("entropy == 0",    spect_entropy(s) == 0.0f);
    TEST("is_fixed",        spect_is_fixed(s));
    TEST("is_certain",      spect_is_certain(s));

    Specton clamped = spect_fixed(15);
    TEST("clamp to 9",      clamped.data.fixed_val == 9);
}

void test_specton_range() {
    printf("\n[specton range]\n");
    Specton r = spect_range(2, 8);
    TEST("mode is RANGE",   r.mode == SPECT_RANGE);
    TEST("lo == 2",         r.data.range.lo == 2);
    TEST("hi == 8",         r.data.range.hi == 8);
    TEST("collapse midpt",  spect_collapse(r) == 5);
    TEST("entropy > 0",     spect_entropy(r) > 0.0f);
    TEST("is_range",        spect_is_range(r));
}

void test_specton_wave() {
    printf("\n[specton wave]\n");
    float w[10] = {0,0,0,0.4f,0,0,0,0.8f,0,0};
    Specton s = spect_wave(w);
    TEST("mode is WAVE",    s.mode == SPECT_WAVE);
    TEST("peak == 7",       spect_peak(s) == 7);
    TEST("weight_at 7 > 0", spect_weight_at(s, 7) > 0.0f);
    TEST("entropy > 0",     spect_entropy(s) > 0.0f);
    TEST("is_wave",         spect_is_wave(s));

    Specton n = spect_normalize(s);
    float sum = 0.0f;
    for (int i = 0; i < 10; i++) sum += n.data.wave[i];
    TEST("normalized sums to ~1", sum > 0.99f && sum < 1.01f);
}

void test_specton_arithmetic() {
    printf("\n[specton arithmetic]\n");
    Specton a = spect_fixed(3);
    Specton b = spect_fixed(4);
    Specton c = spect_add(a, b);
    TEST("3+4 fixed collapses to 7", spect_collapse(c) == 7);

    float wa[10] = {0,0,0,0.3f,0,0,0,0.9f,0,0};
    float wb[10] = {0,0,0,0,0,0.5f,0,0.7f,0,0};
    Specton wA = spect_normalize(spect_wave(wa));
    Specton wB = spect_normalize(spect_wave(wb));

    Specton res = spect_resonate(wA, wB);
    TEST("resonate peak at shared state 7", spect_peak(res) == 7);

    Specton spr = spect_spread(wA, wB);
    TEST("spread mode is WAVE", spect_is_wave(spr));
}

void test_specton_conversion() {
    printf("\n[specton conversion]\n");
    Specton f = spect_fixed(5);
    Specton w = spect_to_wave(f);
    TEST("fixed→wave weight[5]==1", w.data.wave[5] > 0.99f);

    Specton r = spect_range(3, 7);
    Specton rw = spect_to_wave(r);
    TEST("range→wave uniform spread", rw.data.wave[3] > 0.0f && rw.data.wave[7] > 0.0f);
    TEST("range→wave outside range is 0", rw.data.wave[0] < 0.001f);
}

void test_tensor() {
    printf("\n[tensor]\n");
    SpectArray* arr = sarray_zeros(10);
    TEST("array alloc not null", arr != NULL);
    TEST("array length 10", arr->length == 10);

    Specton v = spect_fixed(3);
    sarray_set(arr, 5, v);
    TEST("set/get works", spect_collapse(sarray_get(arr, 5)) == 3);
    sarray_free(arr);

    SpectMatrix* mat = smat_zeros(4, 4);
    TEST("matrix alloc not null", mat != NULL);
    TEST("matrix rows 4", mat->rows == 4);
    TEST("matrix cols 4", mat->cols == 4);
    smat_free(mat);

    SpectMatrix* rw = smat_random_wave(3, 3);
    TEST("random_wave matrix not null", rw != NULL);
    TEST("random_wave is WAVE mode", rw->data[0].mode == SPECT_WAVE);
    smat_free(rw);
}

void test_memory() {
    printf("\n[memory]\n");
    void* p = rc_alloc(64, NULL);
    TEST("rc_alloc not null", p != NULL);
    TEST("refcount is 1", rc_refcount(p) == 1);
    rc_retain(p);
    TEST("refcount after retain is 2", rc_refcount(p) == 2);
    rc_release(p);
    TEST("refcount after release is 1", rc_refcount(p) == 1);
    rc_release(p);  /* frees */

    Arena* a = arena_create(1024);
    TEST("arena create not null", a != NULL);
    void* block = arena_push(a, 32);
    TEST("arena push not null", block != NULL);
    arena_reset(a);
    arena_destroy(a);
    TEST("arena ok", 1);

    Pool* pool = pool_create(16, 8);
    TEST("pool create not null", pool != NULL);
    void* b1 = pool_get(pool);
    void* b2 = pool_get(pool);
    TEST("pool get b1 not null", b1 != NULL);
    TEST("pool get b2 not null", b2 != NULL);
    pool_put(pool, b1);
    pool_destroy(pool);
    TEST("pool ok", 1);
}

int main() {
    printf("SPECTRA Runtime Test Suite\n");
    printf("==========================\n");
    spect_seed_rng(42);

    test_specton_fixed();
    test_specton_range();
    test_specton_wave();
    test_specton_arithmetic();
    test_specton_conversion();
    test_tensor();
    test_memory();

    printf("\n==========================\n");
    printf("Results: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
