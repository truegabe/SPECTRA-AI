#ifndef SPECTRA_NEURAL_H
#define SPECTRA_NEURAL_H

#include "specton.h"
#include "tensor.h"
#include <stddef.h>

/* =========================================================
 * Activation functions
 * ========================================================= */

typedef enum {
    ACT_RELU = 0,
    ACT_SIGMOID,
    ACT_TANH,
    ACT_WAVE,      /* Specton-based: distributes output as wave probability */
    ACT_SOFTMAX,
    ACT_LINEAR
} NNActivation;

NNActivation nn_parse_activation(const char* name);

/* =========================================================
 * NNLayer — single fully-connected layer
 * weights shape: [out_size][in_size]  (row-major)
 * bias    shape: [out_size]
 * ========================================================= */

typedef struct {
    float*       weights;    /* [out_size * in_size] */
    float*       bias;       /* [out_size]           */
    float*       d_weights;  /* gradient accumulator */
    float*       d_bias;
    float*       cache_in;   /* last input  (for backprop) */
    float*       cache_out;  /* last pre-activation output */
    int          in_size;
    int          out_size;
    NNActivation act;
} NNLayer;

NNLayer* nn_layer_create(int in_size, int out_size, NNActivation act);
void     nn_layer_free(NNLayer* L);

/* =========================================================
 * NNNetwork — sequence of layers
 * ========================================================= */

typedef struct {
    NNLayer** layers;
    int       num_layers;
    float     lr;
    int       input_size;
    int       output_size;
} NNNetwork;

NNNetwork* nn_create(int* sizes, int num_sizes, float lr);
void       nn_free(NNNetwork* net);

/* =========================================================
 * Forward / backward / train
 * Input/output are float[] row vectors of matching size.
 * nn_forward: writes result to output (caller allocates [out_size])
 * nn_backward: computes gradients given target (caller allocates [out_size])
 * nn_update: applies accumulated gradients, then zeroes them
 * =========================================================*/

void  nn_forward(NNNetwork* net, const float* input, float* output);
void  nn_backward(NNNetwork* net, const float* input, const float* target);
void  nn_update(NNNetwork* net);

/* Train for one epoch over a batch.
 * inputs:  [n_samples * input_size]
 * targets: [n_samples * output_size]
 * Returns mean squared error for this epoch. */
float nn_train_epoch(NNNetwork* net,
                     const float* inputs,
                     const float* targets,
                     int n_samples);

/* Predict — convenience wrapper around nn_forward */
void  nn_predict(NNNetwork* net, const float* input, float* output);

/* Specton-based interface: collapse the output float[] into a Specton wave */
Specton nn_predict_specton(NNNetwork* net, const float* input);

/* SpectMatrix interface for SPECTRA code */
SpectMatrix* nn_forward_mat(NNNetwork* net, SpectMatrix* input_row);

/* Utility */
void  nn_print_summary(NNNetwork* net);
float nn_mse(const float* pred, const float* target, int n);

#endif /* SPECTRA_NEURAL_H */
