#include "tiny_llm_inference.hpp"

#include <string>
#include <vector>

#include "simple_tokenizer.hpp"
#include "tensor.hpp"
#include "transformer.hpp"

const std::string TransformerParameters::model_path = "model/weights";
const std::string TransformerParameters::tokenizer_path = "model/tinystories_tokenizer_vocab.json";

TinyLLM::TinyLLM()
    : tokenizer(nullptr), transformer(nullptr) {
    transformer = new Transformer(TransformerParameters::vocab_size, TransformerParameters::n_embd,
                                 TransformerParameters::n_head, TransformerParameters::n_layer,
                                 TransformerParameters::max_context, TransformerParameters::dropout);
    tokenizer = new HybridTokenizer();
}

TinyLLM::~TinyLLM() {
    delete tokenizer;
    delete transformer;
}

void TinyLLM::init(const std::string& initial_prompt) {
    tokenizer->load_vocab(TransformerParameters::tokenizer_path);
    transformer->load_weights(TransformerParameters::model_path);
    if (!initial_prompt.empty()) {
        token_ids = tokenizer->encode(initial_prompt);
    }
}

int TinyLLM::inference(int latest_token) {
    if (latest_token != -1) {
        token_ids.push_back(latest_token);
    }

    Tensor logits;
    transformer->forward(token_ids, logits, false);

    int start_index = logits.shape[1] * (logits.shape[0] - 1);
    int max_index = 0;
    float max_value = logits.data[start_index];

    for (int i = start_index; i < start_index + TransformerParameters::vocab_size; i++) {
        if (logits.data[i] > max_value) {
            max_value = logits.data[i];
            max_index = i;
        }
    }
    max_index = max_index - start_index;
    return max_index;
}

std::string TinyLLM::decode(int token_id) {
    return tokenizer->decode({token_id});
}
#if defined(SINGGLE_INFERENCE)
#include <iostream>

int main(){
    TinyLLM llm;
    std::string text = "this morning i walk";
    
    // The init function handles tokenizer loading, model loading, and encoding the initial prompt.
    llm.init(text);
    std::cout << "Initial text: " << text << std::endl;

    // The first inference call uses the initial prompt.
    // We pass -1 to indicate it's the first token.
    int max_index = llm.inference(-1);
    
    std::cout << "Max index: " << max_index << std::endl;
    std::string max_index_word = llm.decode(max_index);
    std::cout << "Max index word: " << max_index_word << std::endl;

    return 0;
}
#endif


#if defined(INFERENCE_LOOP)
#include <iostream>

int main() {
    TinyLLM llm;
    std::string text = "Lily and Tom";
    llm.init(text);

    const int max_tokens = 50;
    const int eos_token_id = 3;

    std::cout << "\nStarting inference loop with max_tokens = " << max_tokens << std::endl;
    std::cout << "Generated text: " << text << std::flush;

    int generated_tokens = 0;
    int next_token = -1; // Start with -1 to indicate first inference

    while (generated_tokens < max_tokens) {
        next_token = llm.inference(next_token);

        if (next_token == eos_token_id) {
            std::cout << "\n[EOS token detected, stopping generation]" << std::endl;
            break;
        }

        std::string new_token_str = llm.decode(next_token);
        std::cout << new_token_str << std::flush;

        generated_tokens++;
    }
    std::cout << std::endl;

    return 0;
}
#endif
