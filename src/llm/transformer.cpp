#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <limits>
#include <iomanip>

// Define DEBUG_PRINT to enable/disable all debug printing
// #define DEBUG_PRINT

#ifdef DEBUG_PRINT
#define DEBUG_COUT(x) std::cout << x
#define DEBUG_COUT_FIXED std::cout << std::fixed << std::setprecision(10)
#else
#define DEBUG_COUT(x)
#define DEBUG_COUT_FIXED
#endif

#include "transformer.hpp"



Embedding::Embedding(int vocab_size, int n_embd): vocab_size(vocab_size), n_embd(n_embd) {
    this->weight.shape.push_back(vocab_size);
    this->weight.shape.push_back(n_embd);
    this->weight.data.resize(vocab_size * n_embd);
}

Embedding::~Embedding() {
    this->weight.data.clear();
    this->weight.shape.clear();
}

int Embedding::set_weight(Tensor& weight) {
    // DEBUG_COUT("Embedding Weight set with size: " << this->weight.data.size()<<" "<<weight.data.size() << std::endl);
    if (weight.shape[0] != this->weight.shape[0] || weight.shape[1] != this->weight.shape[1]) {
        std::cerr << "Embedding Weight shape mismatch" << std::endl;
        return -1;
    }
    this->weight = weight;
    DEBUG_COUT("Embedding Weight set with size: " << this->weight.data.size() << std::endl);
    DEBUG_COUT("Embedding Weight shape: " << this->weight.shape[0] << " " << this->weight.shape[1] << std::endl);
    DEBUG_COUT("Embedding Weight data: " << std::endl);
    for (int i = 0; i < 10; i++) {
        DEBUG_COUT(this->weight.data[i] << " ");
    }
    DEBUG_COUT(std::endl);
    return 0;
}

void Embedding::forward(std::vector<int>& input_token_ids, Tensor& output) {
    int token_count = input_token_ids.size();
    DEBUG_COUT("Embedding Forward " << input_token_ids[0] << " " << output.shape.size()<< " " << output.data.size()<< std::endl);
    output.shape.push_back(input_token_ids.size());
    output.shape.push_back(this->weight.shape[1]);
    output.data.resize(token_count * this->n_embd);
    for (int id_iter = 0; id_iter < token_count; id_iter++) {
        for (int embd_iter = 0; embd_iter < this->n_embd; embd_iter++) {
            output.data[id_iter * this->n_embd + embd_iter] = this->weight.data[input_token_ids[id_iter] * this->n_embd + embd_iter];
        }
    }
}




SinusoidalGlobalPE::SinusoidalGlobalPE(int n_embd, int max_context): n_embd(n_embd), max_context(max_context) {
    this->weight.shape.push_back(max_context);
    this->weight.shape.push_back(n_embd);
    this->weight.data.resize(n_embd * max_context);
    for (int pos_iter = 0; pos_iter < max_context; pos_iter++) {
        for (int embd_iter = 0; embd_iter < n_embd/2; embd_iter++) {
            float div_term = pow(10000.0, (float)(embd_iter*2) / (float)n_embd);
            this->weight.data[pos_iter * n_embd + embd_iter*2] = sin((float)pos_iter / div_term);
            this->weight.data[pos_iter * n_embd + embd_iter*2+1] = cos((float)pos_iter / div_term);
        }
    }
}

SinusoidalGlobalPE::~SinusoidalGlobalPE() {
    this->weight.data.clear();
    this->weight.shape.clear();
}

void SinusoidalGlobalPE::forward(std::vector<int>& input_pos, Tensor& inp_out) {
    int token_count = input_pos.size();
    for (int id_iter = 0; id_iter < token_count; id_iter++) {
        for (int embd_iter = 0; embd_iter < this->n_embd; embd_iter++) {
            inp_out.data[id_iter * this->n_embd + embd_iter] = inp_out.data[id_iter * this->n_embd + embd_iter] + this->weight.data[input_pos[id_iter] * this->n_embd + embd_iter];
        }
    }
}




Block::Block(int n_embd, int n_head, float dropout = 0.0) : n_embd(n_embd), n_head(n_head), dropout(dropout), 
    sa(n_head, n_embd / n_head, n_embd, dropout), 
    ffwd(n_embd, dropout), 
    ln1(n_embd), 
    ln2(n_embd) {
}

Block::~Block() {}

void Block::forward(Tensor& inp_out) {
    DEBUG_COUT("Block Forward:"<<std::endl);
    DEBUG_COUT_FIXED;
    Tensor norm1;
    ln1.forward(inp_out, norm1);
    DEBUG_COUT("Norm1 Forward shape:"<<norm1.shape[0]<< " " <<norm1.shape[1]<< " size:" <<norm1.data.size()<<" sum:" <<norm1.sum()<< " norm:" <<norm1.norm()<< std::endl);
    Tensor attn;
    sa.forward(norm1, attn);
    for (size_t i = 0; i < inp_out.data.size(); ++i) {
        inp_out.data[i] += attn.data[i];
    }
    Tensor norm2;
    ln2.forward(inp_out, norm2);
    Tensor ff;
    ffwd.forward(norm2, ff);
    for (size_t i = 0; i < inp_out.data.size(); ++i) {
        inp_out.data[i] += ff.data[i];
    }
}

void Block::set_ln1_gamma(const Tensor& g) { ln1.set_gamma(g); }

void Block::set_ln1_beta(const Tensor& b) { ln1.set_beta(b); }

void Block::set_ln2_gamma(const Tensor& g) { ln2.set_gamma(g); }

void Block::set_ln2_beta(const Tensor& b) { ln2.set_beta(b); }

void Block::set_ffwd_fc1_weight(const Tensor& w) { ffwd.set_fc1_weight(w); }

void Block::set_ffwd_fc2_weight(const Tensor& w) { ffwd.set_fc2_weight(w); }

void Block::set_sa_head_key_weight(int h, const Tensor& w) { sa.set_head_key_weight(h, w); }

void Block::set_sa_head_query_weight(int h, const Tensor& w) { sa.set_head_query_weight(h, w); }

void Block::set_sa_head_value_weight(int h, const Tensor& w) { sa.set_head_value_weight(h, w); }

void Block::set_sa_proj_weight(const Tensor& w) { sa.set_proj_weight(w); }




LayerNorm::LayerNorm(int normalized_shape) : normalized_shape(normalized_shape), eps(1e-5f) {
    gamma.shape.push_back(normalized_shape);
    gamma.data.resize(normalized_shape, 1.0f);
    beta.shape.push_back(normalized_shape);
    beta.data.resize(normalized_shape, 0.0f);
}

LayerNorm::~LayerNorm() {
    gamma.data.clear();
    beta.data.clear();
    gamma.shape.clear();
    beta.shape.clear();
}

void LayerNorm::set_gamma(const Tensor& g) {
    if (g.shape.size() != 1 || g.shape[0] != normalized_shape) {
        std::cerr << "Gamma shape mismatch" << std::endl;
        return;
    }
    gamma = g;
    DEBUG_COUT("LayerNorm Gamma set with size:" << gamma.data.size()<< std::endl);
}

void LayerNorm::set_beta(const Tensor& b) {
    if (b.shape.size() != 1 || b.shape[0] != normalized_shape) {
        std::cerr << "Beta shape mismatch" << std::endl;
        return;
    }
    beta = b;
    DEBUG_COUT("LayerNorm Beta set with size:" << beta.data.size()<< std::endl);
}

void LayerNorm::forward(const Tensor& input, Tensor& output) {
    if (input.shape.size() != 2 || input.shape[1] != normalized_shape) {
        std::cerr << "LayerNorm input shape mismatch" << std::endl;
        return;
    }
    DEBUG_COUT("LayerNorm Forward:"<<std::endl);
    int seq = input.shape[0];
    int embd = input.shape[1];
    output.shape = input.shape;
    output.data.resize(input.data.size());
    for (int t = 0; t < seq; ++t) {
        float mean = 0.0f;
        for (int c = 0; c < embd; ++c) {
            mean += input.data[t * embd + c];
        }
        mean /= static_cast<float>(embd);
        float var = 0.0f;
        for (int c = 0; c < embd; ++c) {
            float diff = input.data[t * embd + c] - mean;
            var += diff * diff;
        }
        var /= static_cast<float>(embd);
        float stddev = std::sqrt(var + eps);
        for (int c = 0; c < embd; ++c) {
            float norm = (input.data[t * embd + c] - mean) / stddev;
            output.data[t * embd + c] = norm * gamma.data[c] + beta.data[c];
        }
    }
}




Linear::Linear(int in_features, int out_features, bool use_bias) : in_features(in_features), out_features(out_features), use_bias(use_bias) {
    weight.shape = {out_features, in_features};
    weight.data.resize(out_features * in_features, 0.0f);
    if (use_bias) {
        bias.shape = {out_features};
        bias.data.resize(out_features, 0.0f);
    }
}

Linear::~Linear() {
    weight.data.clear();
    weight.shape.clear();
    bias.data.clear();
    bias.shape.clear();
}

void Linear::set_weight(const Tensor& w) {
    if (w.shape.size() != 2 || w.shape[0] != out_features || w.shape[1] != in_features) {
        std::cerr << "Linear weight shape mismatch" << std::endl;
        return;
    }
    weight = w;
    DEBUG_COUT("Linear Weight set with size:" << weight.data.size()<< std::endl);
}

void Linear::set_bias(const Tensor& b) {
    if (!use_bias) return;
    if (b.shape.size() != 1 || b.shape[0] != out_features) {
        std::cerr << "Linear bias shape mismatch" << std::endl;
        return;
    }
    bias = b;
    DEBUG_COUT("Linear Bias set with size:" << bias.data.size()<< std::endl);
}

void Linear::forward(const Tensor& input, Tensor& output) {
    if (input.shape.size() != 2 || input.shape[1] != in_features) {
        std::cerr << "Linear input shape mismatch" << std::endl;
        return;
    }
    int seq = input.shape[0];
    output.shape = {seq, out_features};
    output.data.resize(seq * out_features);
    for (int t = 0; t < seq; ++t) {
        for (int o = 0; o < out_features; ++o) {
            float val = 0.0f;
            for (int i = 0; i < in_features; ++i) {
                val += input.data[t * in_features + i] * weight.data[o * in_features + i];
            }
            if (use_bias) val += bias.data[o];
            output.data[t * out_features + o] = val;
        }
    }
}




Head::Head(int head_size, int n_embd, float dropout) : head_size(head_size), dropout(dropout), key(n_embd, head_size, false), query(n_embd, head_size, false), value(n_embd, head_size, false) {}

Head::~Head() {}

void Head::forward(const Tensor& x, Tensor& out) {
    int seq = x.shape[0];
    int embd = x.shape[1];
    Tensor k, q, v;
    key.forward(x, k);
    query.forward(x, q);
    value.forward(x, v);
    DEBUG_COUT_FIXED;
    DEBUG_COUT("Head Key shape:" << k.shape[0]<< " " << k.shape[1]<< " size:" << k.data.size()<<" sum:" << k.sum()<< " norm:" <<k.norm()<< std::endl);
    DEBUG_COUT("Head Query shape:" << q.shape[0]<< " " << q.shape[1]<< " size:" << q.data.size()<<" sum:" << q.sum()<< " norm:" <<q.norm()<< std::endl);
    DEBUG_COUT("Head Value shape:" << v.shape[0]<< " " << v.shape[1]<< " size:" << v.data.size()<<" sum:" << v.sum()<< " norm:" <<v.norm()<< std::endl);
    Tensor wei;
    wei.shape = {seq, seq};
    wei.data.resize(seq * seq);
    float scale = 1.0f / std::sqrt(static_cast<float>(head_size));
    for (int t1 = 0; t1 < seq; ++t1) {
        for (int t2 = 0; t2 < seq; ++t2) {
            float val = 0.0f;
            for (int h = 0; h < head_size; ++h) {
                val += q.data[t1 * head_size + h] * k.data[t2 * head_size + h];
            }
            wei.data[t1 * seq + t2] = val * scale;
        }
    }
    // Causal mask
    for (int t1 = 0; t1 < seq; ++t1) {
        for (int t2 = t1 + 1; t2 < seq; ++t2) {
            wei.data[t1 * seq + t2] = -std::numeric_limits<float>::infinity();
        }
    }
    // Softmax
    for (int t1 = 0; t1 < seq; ++t1) {
        float max_val = -std::numeric_limits<float>::infinity();
        for (int t2 = 0; t2 < seq; ++t2) {
            if (wei.data[t1 * seq + t2] > max_val) max_val = wei.data[t1 * seq + t2];
        }
        float sum = 0.0f;
        for (int t2 = 0; t2 < seq; ++t2) {
            float expv = std::exp(wei.data[t1 * seq + t2] - max_val);
            sum += expv;
            wei.data[t1 * seq + t2] = expv;
        }
        for (int t2 = 0; t2 < seq; ++t2) {
            wei.data[t1 * seq + t2] /= sum;
        }
    }
    // out = wei @ v
    out.shape = {seq, head_size};
    out.data.resize(seq * head_size);
    for (int t1 = 0; t1 < seq; ++t1) {
        for (int h = 0; h < head_size; ++h) {
            float val = 0.0f;
            for (int t2 = 0; t2 < seq; ++t2) {
                val += wei.data[t1 * seq + t2] * v.data[t2 * head_size + h];
            }
            out.data[t1 * head_size + h] = val;
        }
    }
}

void Head::set_key_weight(const Tensor& w) { key.set_weight(w); }

void Head::set_query_weight(const Tensor& w) { query.set_weight(w); }

void Head::set_value_weight(const Tensor& w) { value.set_weight(w); }




MultiHeadAttention::MultiHeadAttention(int num_heads, int head_size, int n_embd, float dropout) : num_heads(num_heads), head_size(head_size), n_embd(n_embd), dropout(dropout), proj(n_embd, n_embd, false) {
    heads.reserve(num_heads);
    for (int i = 0; i < num_heads; ++i) {
        heads.emplace_back(head_size, n_embd, dropout);
    }
}

MultiHeadAttention::~MultiHeadAttention() {
    heads.clear();
}

void MultiHeadAttention::forward(const Tensor& x, Tensor& out) {
    DEBUG_COUT_FIXED;
    int seq = x.shape[0];
    Tensor concat;
    concat.shape = {seq, n_embd};
    concat.data.resize(seq * n_embd);
    for (int h = 0; h < num_heads; ++h) {
        Tensor head_out;
        heads[h].forward(x, head_out);
        for (int t = 0; t < seq; ++t) {
            for (int d = 0; d < head_size; ++d) {
                concat.data[t * n_embd + h * head_size + d] = head_out.data[t * head_size + d];
            }
        }
    }
    proj.forward(concat, out);
    DEBUG_COUT("MultiHeadAttention Forward shape:" << out.shape[0]<< " " << out.shape[1]<< " size:" << out.data.size()<<" sum:" << out.sum()<< " norm:" <<out.norm()<< std::endl);
}

void MultiHeadAttention::set_head_key_weight(int head_idx, const Tensor& w) { heads[head_idx].set_key_weight(w); }

void MultiHeadAttention::set_head_query_weight(int head_idx, const Tensor& w) { heads[head_idx].set_query_weight(w); }

void MultiHeadAttention::set_head_value_weight(int head_idx, const Tensor& w) { heads[head_idx].set_value_weight(w); }

void MultiHeadAttention::set_proj_weight(const Tensor& w) { proj.set_weight(w); }




FeedForward::FeedForward(int n_embd, float dropout) : dropout(dropout), fc1(n_embd, 4 * n_embd, false), fc2(4 * n_embd, n_embd, false) {}

FeedForward::~FeedForward() {}

void FeedForward::forward(const Tensor& input, Tensor& out) {
    Tensor hidden;
    fc1.forward(input, hidden);
    // GELU
    Tensor gelu;
    gelu.shape = hidden.shape;
    gelu.data.resize(hidden.data.size());
    for (size_t i = 0; i < hidden.data.size(); ++i) {
        float x = hidden.data[i];
        gelu.data[i] = 0.5f * x * (1.0f + std::erf(x / std::sqrt(2.0f)));
    }
    fc2.forward(gelu, out);
}

void FeedForward::set_fc1_weight(const Tensor& w) { fc1.set_weight(w); }

void FeedForward::set_fc2_weight(const Tensor& w) { fc2.set_weight(w); }




Transformer::Transformer(int vocab_size, int n_embd, int n_head, int n_layer, int max_context, float dropout)
    : embedding(vocab_size, n_embd), sinusoidal_global_pe(n_embd, max_context), 
    vocab_size(vocab_size), n_embd(n_embd), n_head(n_head), n_layer(n_layer), 
    max_context(max_context), dropout(dropout), 
    ln_f(n_embd), lm_head(n_embd, vocab_size, false){

    blocks.reserve(n_layer);
    for (int i = 0; i < n_layer; ++i) {
        blocks.push_back(Block(n_embd, n_head, dropout));
    }
}

Transformer::~Transformer() {
    blocks.clear();
}

// Function to load all weights, choose modularity, so load all weight to dictionary, then transfer them to the layers. Later optimization might load directly to the layers.n
void Transformer::load_weights(const std::string& export_dir) {
    std::unordered_map<std::string, Tensor> weights;
    std::ifstream metadata_file(export_dir + "/metadata.txt");
    if (!metadata_file) {
        std::cerr << "Failed to open metadata.txt, make sure to donwload the model weights, refer to *Model Inference* section in the documentation" << std::endl;
    }
    std::string line;
    while (std::getline(metadata_file, line)) {
        Tensor tensor;
        std::istringstream iss(line);
        std::string name, shapex, shapey, dtype, size;
        if (iss >> name >> shapex >> shapey >> dtype >> size) {
            tensor.shape.push_back(std::stoi(shapex));
            tensor.shape.push_back(std::stoi(shapey));
        } else {
            iss.clear();
            iss.str(line);
            if (iss >> name >> shapex >> dtype >> size) {
                tensor.shape.push_back(std::stoi(shapex));
            } else {
                std::cerr << "Fatal Error: Malformed line, weight metadata invalid: " << line << std::endl;
                continue;
            }
        }
        int expected_size = std::stoi(size);

        std::string formatted_name = name;
        std::replace(formatted_name.begin(), formatted_name.end(), '.', '_');
        std::string bin_path = export_dir + "/" + formatted_name + ".bin";

        std::ifstream bin_file(bin_path, std::ios::binary);
        if (!bin_file) {
            std::cerr << "Failed to open " << bin_path << ", make sure to download the model weights, refer to *Model Inference* section in the documentation" << std::endl;
            continue;
        }
        tensor.data.resize(expected_size);
        bin_file.read(reinterpret_cast<char*>(tensor.data.data()), expected_size * sizeof(float));
        if (bin_file.gcount() != expected_size * sizeof(float)) {
            std::cerr << "Incomplete read for " << name << ", make sure to download the model weights, refer to *Model Inference* section in the documentation" << std::endl;
            continue;
        }
        // std::cout << "Weight name:" << name << " shape:" << tensor.shape[0]<< " " << tensor.shape[1]<< " size:" << tensor.data.size()<<" sum:" << tensor.sum()<< " norm:" <<tensor.norm()<< std::endl;
        weights[name] = std::move(tensor);
    }
    embedding.set_weight(weights["token_embedding.weight"]);
    for (int layer_index = 0; layer_index < n_layer; layer_index++) {
        std::string prefix = "blocks." + std::to_string(layer_index) + ".";
        blocks[layer_index].set_ln1_gamma(weights[prefix + "ln1.weight"]);
        blocks[layer_index].set_ln1_beta(weights[prefix + "ln1.bias"]);
        blocks[layer_index].set_ln2_gamma(weights[prefix + "ln2.weight"]);
        blocks[layer_index].set_ln2_beta(weights[prefix + "ln2.bias"]);
        blocks[layer_index].set_ffwd_fc1_weight(weights[prefix + "ffwd.net.0.weight"]);
        blocks[layer_index].set_ffwd_fc2_weight(weights[prefix + "ffwd.net.2.weight"]);
        for (int head_index = 0; head_index < n_head; head_index++) {
            std::string hprefix = prefix + "sa.heads." + std::to_string(head_index) + ".";
            blocks[layer_index].set_sa_head_key_weight(head_index, weights[hprefix + "key.weight"]);
            blocks[layer_index].set_sa_head_query_weight(head_index, weights[hprefix + "query.weight"]);
            blocks[layer_index].set_sa_head_value_weight(head_index, weights[hprefix + "value.weight"]);
        }
        blocks[layer_index].set_sa_proj_weight(weights[prefix + "sa.proj.weight"]);
    }
    ln_f.set_gamma(weights["ln_f.weight"]);
    ln_f.set_beta(weights["ln_f.bias"]);
    lm_head.set_weight(weights["lm_head.weight"]);
}

void Transformer::forward(std::vector<int>& input_token_ids, Tensor& logits, bool completion) {
    DEBUG_COUT_FIXED;
    Tensor x;
    embedding.forward(input_token_ids, x);
    DEBUG_COUT("Embedding  Forward shape:" << x.shape[0]<< " " << x.shape[1]<< " size:" << x.data.size()<<" sum:" << x.sum()<< " norm:" <<x.norm()<< std::endl);
    if (completion) {
        // this->sinusoidal_global_pe.forward(input_token_ids, x); // solve for completionlater
    }else{
        std::vector<int> input_pos(input_token_ids.size(), 0);
        for (int id_iter = 0; id_iter < input_token_ids.size(); id_iter++) {
            input_pos[id_iter] = id_iter;
        }
        this->sinusoidal_global_pe.forward(input_pos, x);
    }
    DEBUG_COUT("Sinusoidal Global PE Forward shape:" << x.shape[0]<< " " << x.shape[1]<< " size:" << x.data.size()<<" sum:" << x.sum()<< " norm:" <<x.norm()<< std::endl);
    for (auto& b : blocks) {
        b.forward(x);
    }
    DEBUG_COUT("Block Forward shape:" << x.shape[0]<< " " << x.shape[1]<< " size:" << x.data.size()<<" sum:" << x.sum()<< " norm:" <<x.norm()<< std::endl);
    Tensor norm;
    ln_f.forward(x, norm);
    lm_head.forward(norm, logits);
    DEBUG_COUT("LM Head Forward shape:" << logits.shape[0]<< " " << logits.shape[1]<< " size:" << logits.data.size()<<" sum:" << logits.sum()<< " norm:" <<logits.norm()<< std::endl);
}