/* neural.c — Neural network runtime for SPECTRA
 *
 * Float-based fully-connected feed-forward network with:
 *   - ReLU, Sigmoid, Tanh, Linear, Softmax activations
 *   - "Wave" activation: maps float output to a Specton wave distribution
 *   - SGD with momentum (simple version: vanilla SGD)
 *   - Backpropagation via chain rule
 */

#include "neural.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

/* =========================================================
 * Helpers
 * ========================================================= */

static float randf(void) {
    return (float)rand() / ((float)RAND_MAX + 1.0f);
}

/* Xavier / Glorot initialization */
static float xavier(int fan_in, int fan_out) {
    float limit = sqrtf(6.0f / (float)(fan_in + fan_out));
    return (randf() * 2.0f - 1.0f) * limit;
}

static float relu(float x)    { return x > 0.0f ? x : 0.0f; }
static float relu_d(float x)  { return x > 0.0f ? 1.0f : 0.0f; }
static float sigmoid(float x) { return 1.0f / (1.0f + expf(-x)); }
static float sigmoid_d(float y) { return y * (1.0f - y); } /* y = sigmoid(x) */
static float tanh_d(float y)  { return 1.0f - y * y; }     /* y = tanh(x) */

/* =========================================================
 * NNActivation
 * ========================================================= */

NNActivation nn_parse_activation(const char* name) {
    if (!name)                          return ACT_RELU;
    if (strcmp(name, "relu") == 0)      return ACT_RELU;
    if (strcmp(name, "sigmoid") == 0)   return ACT_SIGMOID;
    if (strcmp(name, "tanh") == 0)      return ACT_TANH;
    if (strcmp(name, "wave") == 0)      return ACT_WAVE;
    if (strcmp(name, "softmax") == 0)   return ACT_SOFTMAX;
    if (strcmp(name, "linear") == 0)    return ACT_LINEAR;
    return ACT_RELU;
}

/* Apply activation in-place to vec[n] */
static void apply_activation(float* vec, int n, NNActivation act) {
    int i;
    switch (act) {
    case ACT_RELU:
        for (i = 0; i < n; i++) vec[i] = relu(vec[i]);
        break;
    case ACT_SIGMOID:
        for (i = 0; i < n; i++) vec[i] = sigmoid(vec[i]);
        break;
    case ACT_TANH:
        for (i = 0; i < n; i++) vec[i] = tanhf(vec[i]);
        break;
    case ACT_WAVE: {
        /* Normalize to [0,1] range, then treat as probability-like */
        float mn = vec[0], mx = vec[0];
        for (i = 1; i < n; i++) {
            if (vec[i] < mn) mn = vec[i];
            if (vec[i] > mx) mx = vec[i];
        }
        float range = mx - mn;
        if (range < 1e-7f) {
            for (i = 0; i < n; i++) vec[i] = 1.0f / (float)n;
        } else {
            float sum = 0.0f;
            for (i = 0; i < n; i++) { vec[i] = (vec[i] - mn) / range; sum += vec[i]; }
            if (sum > 1e-7f)
                for (i = 0; i < n; i++) vec[i] /= sum;
        }
        break;
    }
    case ACT_SOFTMAX: {
        float mx = vec[0];
        for (i = 1; i < n; i++) if (vec[i] > mx) mx = vec[i];
        float sum = 0.0f;
        for (i = 0; i < n; i++) { vec[i] = expf(vec[i] - mx); sum += vec[i]; }
        if (sum > 1e-9f)
            for (i = 0; i < n; i++) vec[i] /= sum;
        break;
    }
    case ACT_LINEAR:
    default:
        break;
    }
}

/* Compute activation derivative in-place.
 * For RELU/SIGMOID/TANH the derivative is applied to the post-activation value
 * (cache_out stores the post-activation output for efficiency). */
static void apply_activation_deriv(const float* post_act, float* delta, int n, NNActivation act) {
    int i;
    switch (act) {
    case ACT_RELU:
        for (i = 0; i < n; i++) delta[i] *= relu_d(post_act[i]);
        break;
    case ACT_SIGMOID:
        for (i = 0; i < n; i++) delta[i] *= sigmoid_d(post_act[i]);
        break;
    case ACT_TANH:
        for (i = 0; i < n; i++) delta[i] *= tanh_d(post_act[i]);
        break;
    case ACT_WAVE:
    case ACT_SOFTMAX:
    case ACT_LINEAR:
    default:
        /* Pass-through gradient for wave/softmax/linear in this simple impl */
        break;
    }
}

/* =========================================================
 * NNLayer
 * ========================================================= */

NNLayer* nn_layer_create(int in_size, int out_size, NNActivation act) {
    NNLayer* L = (NNLayer*)calloc(1, sizeof(NNLayer));
    if (!L) return NULL;

    L->in_size  = in_size;
    L->out_size = out_size;
    L->act      = act;

    L->weights   = (float*)malloc(sizeof(float) * (size_t)(in_size * out_size));
    L->bias      = (float*)calloc((size_t)out_size, sizeof(float));
    L->d_weights = (float*)calloc((size_t)(in_size * out_size), sizeof(float));
    L->d_bias    = (float*)calloc((size_t)out_size, sizeof(float));
    L->cache_in  = (float*)calloc((size_t)in_size,  sizeof(float));
    L->cache_out = (float*)calloc((size_t)out_size, sizeof(float));

    if (!L->weights || !L->bias || !L->d_weights || !L->d_bias ||
        !L->cache_in || !L->cache_out) {
        nn_layer_free(L);
        return NULL;
    }

    /* Xavier init */
    for (int i = 0; i < in_size * out_size; i++)
        L->weights[i] = xavier(in_size, out_size);

    return L;
}

void nn_layer_free(NNLayer* L) {
    if (!L) return;
    free(L->weights);
    free(L->bias);
    free(L->d_weights);
    free(L->d_bias);
    free(L->cache_in);
    free(L->cache_out);
    free(L);
}

/* Layer forward pass: output[j] = act( sum_i(weights[j*in+i] * input[i]) + bias[j] ) */
static void layer_forward(NNLayer* L, const float* input, float* output) {
    memcpy(L->cache_in, input, (size_t)L->in_size * sizeof(float));
    for (int j = 0; j < L->out_size; j++) {
        float s = L->bias[j];
        for (int i = 0; i < L->in_size; i++)
            s += L->weights[j * L->in_size + i] * input[i];
        output[j] = s;
    }
    apply_activation(output, L->out_size, L->act);
    memcpy(L->cache_out, output, (size_t)L->out_size * sizeof(float));
}

/* Layer backward pass:
 * delta_in[j]  = upstream delta for this layer's output (length out_size)
 * delta_out[i] = gradient to pass to the previous layer (length in_size)
 * Accumulates into d_weights and d_bias.
 */
static void layer_backward(NNLayer* L, float* delta_in, float* delta_out) {
    /* Apply activation derivative */
    apply_activation_deriv(L->cache_out, delta_in, L->out_size, L->act);

    /* Compute delta for previous layer: delta_out[i] = sum_j(weights[j,i] * delta_in[j]) */
    if (delta_out) {
        for (int i = 0; i < L->in_size; i++) {
            float s = 0.0f;
            for (int j = 0; j < L->out_size; j++)
                s += L->weights[j * L->in_size + i] * delta_in[j];
            delta_out[i] = s;
        }
    }

    /* Accumulate gradients */
    for (int j = 0; j < L->out_size; j++) {
        L->d_bias[j] += delta_in[j];
        for (int i = 0; i < L->in_size; i++)
            L->d_weights[j * L->in_size + i] += delta_in[j] * L->cache_in[i];
    }
}

/* =========================================================
 * NNNetwork
 * ========================================================= */

NNNetwork* nn_create(int* sizes, int num_sizes, float lr) {
    if (!sizes || num_sizes < 2) return NULL;

    NNNetwork* net = (NNNetwork*)calloc(1, sizeof(NNNetwork));
    if (!net) return NULL;

    net->num_layers  = num_sizes - 1;
    net->lr          = lr > 0.0f ? lr : 0.01f;
    net->input_size  = sizes[0];
    net->output_size = sizes[num_sizes - 1];

    net->layers = (NNLayer**)calloc((size_t)net->num_layers, sizeof(NNLayer*));
    if (!net->layers) { free(net); return NULL; }

    for (int i = 0; i < net->num_layers; i++) {
        /* Last layer uses linear/sigmoid; hidden layers use relu */
        NNActivation act = (i == net->num_layers - 1) ? ACT_SIGMOID : ACT_RELU;
        net->layers[i] = nn_layer_create(sizes[i], sizes[i+1], act);
        if (!net->layers[i]) {
            for (int j = 0; j < i; j++) nn_layer_free(net->layers[j]);
            free(net->layers);
            free(net);
            return NULL;
        }
    }

    return net;
}

void nn_free(NNNetwork* net) {
    if (!net) return;
    for (int i = 0; i < net->num_layers; i++)
        nn_layer_free(net->layers[i]);
    free(net->layers);
    free(net);
}

/* =========================================================
 * Forward / Backward / Update
 * ========================================================= */

void nn_forward(NNNetwork* net, const float* input, float* output) {
    if (!net || !input || !output) return;

    int max_size = net->input_size;
    for (int i = 0; i < net->num_layers; i++)
        if (net->layers[i]->out_size > max_size)
            max_size = net->layers[i]->out_size;

    float* buf_a = (float*)malloc(sizeof(float) * (size_t)max_size);
    float* buf_b = (float*)malloc(sizeof(float) * (size_t)max_size);
    if (!buf_a || !buf_b) { free(buf_a); free(buf_b); return; }

    memcpy(buf_a, input, (size_t)net->input_size * sizeof(float));

    for (int i = 0; i < net->num_layers; i++) {
        layer_forward(net->layers[i], buf_a, buf_b);
        /* swap */
        float* tmp = buf_a; buf_a = buf_b; buf_b = tmp;
    }

    memcpy(output, buf_a, (size_t)net->output_size * sizeof(float));
    free(buf_a);
    free(buf_b);
}

void nn_backward(NNNetwork* net, const float* input, const float* target) {
    if (!net || !input || !target) return;

    /* Find max layer size for buffer allocation */
    int max_size = net->input_size;
    for (int i = 0; i < net->num_layers; i++) {
        if (net->layers[i]->out_size > max_size) max_size = net->layers[i]->out_size;
        if (net->layers[i]->in_size  > max_size) max_size = net->layers[i]->in_size;
    }

    /* Run forward to populate layer caches */
    float* fwd_out = (float*)malloc(sizeof(float) * (size_t)net->output_size);
    if (!fwd_out) return;
    nn_forward(net, input, fwd_out);

    /* Two alternating delta buffers, each sized to handle any layer */
    float* buf_a = (float*)calloc((size_t)max_size, sizeof(float));
    float* buf_b = (float*)calloc((size_t)max_size, sizeof(float));
    if (!buf_a || !buf_b) { free(buf_a); free(buf_b); free(fwd_out); return; }

    /* Init output delta: dL/dy = (prediction - target) */
    for (int j = 0; j < net->output_size; j++)
        buf_a[j] = fwd_out[j] - target[j];

    /* Backprop in reverse order */
    for (int i = net->num_layers - 1; i >= 0; i--) {
        float* pass_out = (i > 0) ? buf_b : NULL;
        layer_backward(net->layers[i], buf_a, pass_out);
        if (pass_out) {
            /* swap: buf_b now holds the input-side delta for next layer */
            float* tmp = buf_a; buf_a = buf_b; buf_b = tmp;
        }
    }

    free(buf_a);
    free(buf_b);
    free(fwd_out);
}

void nn_update(NNNetwork* net) {
    if (!net) return;
    for (int i = 0; i < net->num_layers; i++) {
        NNLayer* L = net->layers[i];
        for (int j = 0; j < L->out_size * L->in_size; j++) {
            L->weights[j] -= net->lr * L->d_weights[j];
            L->d_weights[j] = 0.0f;
        }
        for (int j = 0; j < L->out_size; j++) {
            L->bias[j] -= net->lr * L->d_bias[j];
            L->d_bias[j] = 0.0f;
        }
    }
}

float nn_train_epoch(NNNetwork* net,
                     const float* inputs,
                     const float* targets,
                     int n_samples) {
    if (!net || !inputs || !targets || n_samples <= 0) return 0.0f;

    float total_loss = 0.0f;
    float* out = (float*)malloc(sizeof(float) * (size_t)net->output_size);
    if (!out) return 0.0f;

    for (int s = 0; s < n_samples; s++) {
        const float* in  = inputs  + s * net->input_size;
        const float* tgt = targets + s * net->output_size;

        nn_backward(net, in, tgt);
        nn_update(net);

        nn_forward(net, in, out);
        total_loss += nn_mse(out, tgt, net->output_size);
    }

    free(out);
    return total_loss / (float)n_samples;
}

void nn_predict(NNNetwork* net, const float* input, float* output) {
    nn_forward(net, input, output);
}

Specton nn_predict_specton(NNNetwork* net, const float* input) {
    if (!net) return spect_fixed(0);
    float* out = (float*)calloc((size_t)net->output_size, sizeof(float));
    if (!out) return spect_fixed(0);
    nn_forward(net, input, out);

    /* Map output to a Specton wave with up to 10 states */
    float weights[10] = {0};
    int n = net->output_size < 10 ? net->output_size : 10;
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        weights[i] = out[i] > 0.0f ? out[i] : 0.0f;
        sum += weights[i];
    }
    free(out);
    if (sum < 1e-7f) return spect_fixed(0);
    for (int i = 0; i < n; i++) weights[i] /= sum;
    return spect_wave(weights);
}

SpectMatrix* nn_forward_mat(NNNetwork* net, SpectMatrix* input_row) {
    if (!net || !input_row) return NULL;

    /* Convert SpectMatrix row to float[] by collapsing each Specton */
    int in_cols = (int)input_row->cols;
    float* in_f = (float*)malloc(sizeof(float) * (size_t)in_cols);
    if (!in_f) return NULL;

    for (int j = 0; j < in_cols; j++)
        in_f[j] = (float)spect_collapse(smat_get(input_row, 0, (size_t)j)) / 9.0f;

    float* out_f = (float*)calloc((size_t)net->output_size, sizeof(float));
    if (!out_f) { free(in_f); return NULL; }

    nn_forward(net, in_f, out_f);

    /* Convert float[] output to SpectMatrix (1 row) */
    SpectMatrix* result = smat_alloc(1, (size_t)net->output_size);
    for (int j = 0; j < net->output_size; j++) {
        uint8_t state = (uint8_t)(out_f[j] * 9.0f + 0.5f);
        if (state > 9) state = 9;
        smat_set(result, 0, (size_t)j, spect_fixed(state));
    }

    free(in_f);
    free(out_f);
    return result;
}

void nn_print_summary(NNNetwork* net) {
    if (!net) { printf("NNNetwork: NULL\n"); return; }
    printf("NNNetwork: %d layers, lr=%.4f\n", net->num_layers, (double)net->lr);
    printf("  Input size:  %d\n", net->input_size);
    for (int i = 0; i < net->num_layers; i++) {
        NNLayer* L = net->layers[i];
        const char* act_name = "relu";
        switch (L->act) {
            case ACT_SIGMOID: act_name = "sigmoid"; break;
            case ACT_TANH:    act_name = "tanh";    break;
            case ACT_WAVE:    act_name = "wave";    break;
            case ACT_SOFTMAX: act_name = "softmax"; break;
            case ACT_LINEAR:  act_name = "linear";  break;
            default: break;
        }
        printf("  Layer %d: %d → %d  [%s]  params=%d\n",
               i, L->in_size, L->out_size, act_name,
               L->in_size * L->out_size + L->out_size);
    }
    printf("  Output size: %d\n", net->output_size);
}

float nn_mse(const float* pred, const float* target, int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        float d = pred[i] - target[i];
        sum += d * d;
    }
    return n > 0 ? sum / (float)n : 0.0f;
}
