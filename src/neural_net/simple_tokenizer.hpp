#ifndef SIMPLE_TOKENIZER_HPP
#define SIMPLE_TOKENIZER_HPP

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>
#include <regex>
#include <sstream>

class HybridTokenizer {
public:
    int vocab_size;
    std::unordered_map<std::string, int> word_to_id;
    std::unordered_map<int, std::string> id_to_word;
    std::unordered_map<std::string, int> char_to_id;
    std::unordered_map<int, std::string> id_to_char;

    std::vector<std::string> tokenize_words(const std::string& text);

    std::vector<int> encode_word_or_chars(const std::string& word);

    std::string decode(const std::vector<int>& token_ids);

    std::vector<int> encode(const std::string& text, bool add_special_tokens = true);
 
    void load_vocab(const std::string& filepath);

private:
    std::string to_lower(const std::string& str);

    std::string replace(const std::string& str, const std::string& from, const std::string& to);

    std::string extract_object(const std::string& content, const std::string& key);


    int pad_id, unk_id, bos_id, eos_id, char_start_id, char_end_id;
};

#endif // SIMPLE_TOKENIZER_HPP
