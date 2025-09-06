#pragma once

#include "tensor.hpp"
#include <string>
#include <vector>
#include <cmath>
#include <limits>

class Embedding {
    Tensor weight;
    int vocab_size;
    int n_embd;
public:
    Embedding(int vocab_size, int n_embd);
    ~Embedding();
    void forward(std::vector<int>& input_token_ids, Tensor& output);

    int set_weight(Tensor& weight);
};

class SinusoidalGlobalPE {
private:
    int n_embd;
    int max_context;
    Tensor weight;
public:
    SinusoidalGlobalPE(int n_embd, int max_context);
    ~SinusoidalGlobalPE();
    void forward(std::vector<int>& input_pos, Tensor& inp_out);
};

class LayerNorm {
private:
    Tensor gamma;
    Tensor beta;
    float eps = 1e-5f;
    int normalized_shape;
public:
    LayerNorm(int normalized_shape);
    ~LayerNorm();
    void forward(const Tensor& input, Tensor& output);
    void set_gamma(const Tensor& g);
    void set_beta(const Tensor& b);
};

class Linear {
private:
    Tensor weight;
    Tensor bias;
    int in_features;
    int out_features;
    bool use_bias;
public:
    Linear(int in_features, int out_features, bool bias = true);
    ~Linear();
    void forward(const Tensor& input, Tensor& output);
    void set_weight(const Tensor& w);
    void set_bias(const Tensor& b);
};

class Head {
private:
    Linear key;
    Linear query;
    Linear value;
    float dropout;
    int head_size;
public:
    Head(int head_size, int n_embd, float dropout);
    ~Head();
    void forward(const Tensor& x, Tensor& out);
    void set_key_weight(const Tensor& w);
    void set_query_weight(const Tensor& w);
    void set_value_weight(const Tensor& w);
};

class MultiHeadAttention {
private:
    std::vector<Head> heads;
    Linear proj;
    float dropout;
    int num_heads;
    int head_size;
    int n_embd;
public:
    MultiHeadAttention(int num_heads, int head_size, int n_embd, float dropout);
    ~MultiHeadAttention();
    void forward(const Tensor& x, Tensor& out);
    void set_head_key_weight(int head_idx, const Tensor& w);
    void set_head_query_weight(int head_idx, const Tensor& w);
    void set_head_value_weight(int head_idx, const Tensor& w);
    void set_proj_weight(const Tensor& w);
};

class FeedForward {
private:
    Linear fc1;
    Linear fc2;
    float dropout;
public:
    FeedForward(int n_embd, float dropout);
    ~FeedForward();
    void forward(const Tensor& input, Tensor& out);
    void set_fc1_weight(const Tensor& w);
    void set_fc2_weight(const Tensor& w);
};

class Block {
private:
    MultiHeadAttention sa;
    FeedForward ffwd;
    LayerNorm ln1;
    LayerNorm ln2;
    int n_embd;
    int n_head;
    float dropout;
public:
    Block(int n_embd, int n_head, float dropout);
    ~Block();
    void forward(Tensor& inp_out);
    void set_ln1_gamma(const Tensor& g);
    void set_ln1_beta(const Tensor& b);
    void set_ln2_gamma(const Tensor& g);
    void set_ln2_beta(const Tensor& b);
    void set_ffwd_fc1_weight(const Tensor& w);
    void set_ffwd_fc2_weight(const Tensor& w);
    void set_sa_head_key_weight(int h, const Tensor& w);
    void set_sa_head_query_weight(int h, const Tensor& w);
    void set_sa_head_value_weight(int h, const Tensor& w);
    void set_sa_proj_weight(const Tensor& w);
};

class Transformer {
private:
    Embedding embedding;
    SinusoidalGlobalPE sinusoidal_global_pe;
    std::vector<Block> blocks;
    LayerNorm ln_f;
    Linear lm_head;
    int vocab_size;
    int n_embd;
    int n_head;
    int n_layer;
    int max_context;
    float dropout;
public:
    Transformer(int vocab_size, int n_embd, int n_head, int n_layer, int max_context, float dropout);
    ~Transformer();
    void load_weights(const std::string& export_dir);
    void forward(std::vector<int>& input_token_ids, Tensor& logits, bool completion = false);
    // void generate(std::vector<int>& idx, int max_new_tokens, float temperature = 1.0f, int top_k = 0);
};
