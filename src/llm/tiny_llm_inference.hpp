#pragma once

#include <string>
#include <vector>

struct TransformerParameters {
    static const int vocab_size = 3266;
    static const int n_embd = 192;
    static const int n_head = 6;
    static const int n_layer = 6;
    static const int max_context = 512;
    static constexpr float dropout = 0.1f;
    static const std::string model_path;
    static const std::string tokenizer_path;
};

class HybridTokenizer;
class Transformer;

class TinyLLM {
public:
    TinyLLM();
    ~TinyLLM();

    void init(const std::string& initial_prompt);
    int inference(int latest_token);
    std::string decode(int token_id);

private:
    HybridTokenizer* tokenizer;
    Transformer* transformer;
    std::vector<int> token_ids;
};
