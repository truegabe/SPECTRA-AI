#ifndef TENSOR_H
#define TENSOR_H

#include "specton.h"
#include <stddef.h>
#include <stdio.h>

/* =========================================================
 * SpectArray — 1-D array of Spectons
 * ========================================================= */

typedef struct {
    Specton* data;
    size_t   length;
} SpectArray;

/* Allocation / deallocation */
SpectArray* sarray_alloc(size_t length);
void        sarray_free(SpectArray* arr);

/* Constructors */
SpectArray* sarray_zeros(size_t n);
SpectArray* sarray_ones(size_t n);
SpectArray* sarray_fill(size_t n, Specton val);
SpectArray* sarray_random_wave(size_t n);

/* Element access */
Specton sarray_get(SpectArray* arr, size_t i);
void    sarray_set(SpectArray* arr, size_t i, Specton val);

/* Arithmetic */
Specton     sarray_dot(SpectArray* a, SpectArray* b);
SpectArray* sarray_add(SpectArray* a, SpectArray* b);
SpectArray* sarray_mul(SpectArray* a, SpectArray* b);

/* Transformations */
SpectArray* sarray_collapse_all(SpectArray* arr);
SpectArray* sarray_entropy_map(SpectArray* arr);

/* Queries / printing */
float  sarray_mean_entropy(SpectArray* arr);
void   sarray_print(SpectArray* arr);
size_t sarray_shape(SpectArray* arr);


/* =========================================================
 * SpectMatrix — 2-D array of Spectons (row-major)
 * ========================================================= */

typedef struct {
    Specton* data;   /* row-major storage */
    size_t   rows;
    size_t   cols;
} SpectMatrix;

/* Allocation / deallocation */
SpectMatrix* smat_alloc(size_t rows, size_t cols);
void         smat_free(SpectMatrix* mat);

/* Constructors */
SpectMatrix* smat_zeros(size_t rows, size_t cols);
SpectMatrix* smat_ones(size_t rows, size_t cols);
SpectMatrix* smat_fill(size_t rows, size_t cols, Specton val);
SpectMatrix* smat_random_wave(size_t rows, size_t cols);
SpectMatrix* smat_identity(size_t n);

/* Element access */
Specton smat_get(SpectMatrix* m, size_t r, size_t c);
void    smat_set(SpectMatrix* m, size_t r, size_t c, Specton val);

/* Arithmetic */
SpectMatrix* smat_matmul(SpectMatrix* a, SpectMatrix* b);
SpectMatrix* smat_transpose(SpectMatrix* m);
SpectMatrix* smat_add(SpectMatrix* a, SpectMatrix* b);
SpectMatrix* smat_mul(SpectMatrix* a, SpectMatrix* b);

/* Transformations */
SpectMatrix* smat_collapse_all(SpectMatrix* m);
SpectMatrix* smat_entropy_map(SpectMatrix* m);

/* Row / column extraction */
SpectArray* smat_row(SpectMatrix* m, size_t r);
SpectArray* smat_col(SpectMatrix* m, size_t c);

/* Printing */
void smat_print(SpectMatrix* m);


/* =========================================================
 * SpectTensor — N-dimensional array of Spectons
 * ========================================================= */

typedef struct {
    Specton* data;
    size_t*  shape;  /* allocated array of dimension sizes, length == ndim */
    size_t   ndim;
    size_t   total;  /* product of all dims */
} SpectTensor;

/* Allocation / deallocation */
SpectTensor* stensor_alloc(size_t ndim, size_t* shape);
void         stensor_free(SpectTensor* t);

/* Constructors */
SpectTensor* stensor_zeros(size_t ndim, size_t* shape);
SpectTensor* stensor_random_wave(size_t ndim, size_t* shape);

/* Element access */
Specton stensor_get(SpectTensor* t, size_t* indices);
void    stensor_set(SpectTensor* t, size_t* indices, Specton val);

/* Utility */
size_t stensor_flat_index(SpectTensor* t, size_t* indices);
void   stensor_print_shape(SpectTensor* t);

#endif /* TENSOR_H */
