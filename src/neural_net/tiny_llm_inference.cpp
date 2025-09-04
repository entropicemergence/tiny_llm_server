#include <iostream>

#include "simple_tokenizer.hpp"
#include "tensor.hpp"
#include "transformer.hpp"

#define INFERENCE_LOOP




struct TransformerParameters{
    static const int vocab_size = 3266;
    static const int n_embd = 192;
    static const int n_head = 6;
    static const int n_layer = 6;
    static const int max_context = 512;
    static constexpr float dropout = 0.1f;
    static const std::string model_path;
    static const std::string tokenizer_path;
}TransformerParameters;

const std::string TransformerParameters::model_path = "model/weights";
const std::string TransformerParameters::tokenizer_path = "model/tinystories_tokenizer_vocab.json";



#ifdef SINGGLE_INFERENCE

int main(){
    HybridTokenizer tokenizer;
    tokenizer.load_vocab(TransformerParameters::tokenizer_path);

    std::string text = "this morning i walk";
    
    std::vector<int> token_ids = tokenizer.encode(text);
    std::cout << "Token IDs: ";
    for (int token_id : token_ids) {
        std::cout << token_id << " ";
    }
    std::cout << std::endl;
    std::string decoded_text = tokenizer.decode(token_ids);
    std::cout << "Decoded text: " << decoded_text << std::endl;

    Transformer transformer(TransformerParameters::vocab_size, TransformerParameters::n_embd, TransformerParameters::n_head, TransformerParameters::n_layer, TransformerParameters::max_context, TransformerParameters::dropout);
    transformer.load_weights(TransformerParameters::model_path);

    Tensor logits;
    transformer.forward(token_ids,  logits, false);
    int max_index = 0;
    int start_index = logits.shape[1]* (logits.shape[0]-1);
    float max_value = logits.data[start_index];
    for (int i = start_index; i < start_index + TransformerParameters::vocab_size; i++) {
        if (logits.data[i] > max_value) {
            max_value = logits.data[i];
            max_index = i;
        }
    }
    max_index = max_index - start_index;
    std::cout << "Max index: " << max_index << " Max value: " << max_value << std::endl;
    std::string max_index_word = tokenizer.decode({max_index});
    std::cout << "Max index word: " << max_index_word << std::endl;

    return 0;
}

#endif


#ifdef INFERENCE_LOOP

int main(){
    HybridTokenizer tokenizer;
    tokenizer.load_vocab(TransformerParameters::tokenizer_path);

    // std::string text = "this morning i walk";
    std::string text = "Lily and Tom";

    std::vector<int> token_ids = tokenizer.encode(text);

    const int max_tokens = 50;  // Maximum number of tokens to generate
    const int eos_token_id = 3;

    Transformer transformer(TransformerParameters::vocab_size, TransformerParameters::n_embd, TransformerParameters::n_head, TransformerParameters::n_layer, TransformerParameters::max_context, TransformerParameters::dropout);
    transformer.load_weights(TransformerParameters::model_path);

    std::cout << "\nStarting inference loop with max_tokens = " << max_tokens << std::endl;
    std::cout << "Generated text: " << text << std::flush;

    int generated_tokens = 0;
    bool continue_generation = true;
    while (generated_tokens < max_tokens && continue_generation) {
        Tensor logits;
        transformer.forward(token_ids, logits, false);

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

        if (max_index == eos_token_id) {
            std::cout << "\n[EOS token detected, stopping generation]" << std::endl;
            continue_generation = false;
            break;
        }

        // Append new token to sequence
        token_ids.push_back(max_index);
        generated_tokens++;

        // Decode and print the new token
        std::string new_token = tokenizer.decode({max_index});
        std::cout << new_token << std::flush;
    }
    std::cout << std::endl;

    return 0;
}

#endif