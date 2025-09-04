#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include "simple_tokenizer.hpp"


// Helper function to un-escape a basic JSON string
std::string unescape_json_string(const std::string& s) {
    std::string res;
    res.reserve(s.length());
    for (size_t i = 0; i < s.length(); ++i) {
        if (s[i] == '\\' && i + 1 < s.length()) {
            switch (s[i + 1]) {
                case '"': res += '"'; break;
                case '\\': res += '\\'; break;
                // Add other escapes like \n, \t if needed
                default: res += s[i]; break; // Not a valid escape, keep backslash
            }
            ++i; // Skip the escaped character
        } else {
            res += s[i];
        }
    }
    return res;
}

std::string HybridTokenizer::to_lower(const std::string& str) {
    std::string s = str;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

std::string HybridTokenizer::replace(const std::string& str, const std::string& from, const std::string& to) {
    std::string result = str;
    size_t pos = 0;
    while ((pos = result.find(from, pos)) != std::string::npos) {
        result.replace(pos, from.length(), to);
        pos += to.length();
    }
    return result;
}

std::vector<std::string> HybridTokenizer::tokenize_words(const std::string& text) {
    std::regex punct_regex(R"([.,!?;:"\'\-\(\)\[\]{}])");
    std::string spaced = std::regex_replace(text, punct_regex, " $& ");
    std::vector<std::string> tokens;
    std::stringstream ss(spaced);
    std::string token;
    while (ss >> token) {
        if (!token.empty()) tokens.push_back(token);
    }
    return tokens;
}

std::vector<int> HybridTokenizer::encode_word_or_chars(const std::string& word) {
    auto it = word_to_id.find(word);
    if (it != word_to_id.end()) {
        // The word exists in the main vocabulary, return its ID.
        return {it->second};
    } else {
        // The word is not in the vocabulary, so we encode it character by character.
        std::vector<int> char_ids = {char_start_id};
    
        // Use an index-based loop to handle multi-byte characters.
        for (size_t i = 0; i < word.length(); ) {
            bool found_char = false;
    
            // Greedily check for the longest possible character match first.
            // Most UTF-8 characters are 4 bytes or less. Let's check from 4 down to 1.
            for (int len = 4; len > 0; --len) {
                if (i + len <= word.length()) {
                    std::string sub = word.substr(i, len);
                    auto cit = char_to_id.find(sub);
                    if (cit != char_to_id.end()) {
                        // Found a valid character in our map
                        char_ids.push_back(cit->second + static_cast<int>(word_to_id.size()));
                        i += len; // Advance the index by the length of the matched character
                        found_char = true;
                        break; // Exit the inner loop and continue with the next character
                    }
                }
            }
            // If no character (of length 4, 3, 2, or 1) was found at the current position
            if (!found_char) {
                char_ids.push_back(unk_id);
                i += 1; // Move past the unknown byte and continue
            }
        }
    
        char_ids.push_back(char_end_id);
        return char_ids;
    }
}

std::string HybridTokenizer::decode(const std::vector<int>& token_ids) {
    std::string text;
    for (int token_id : token_ids) {
        text += " " + id_to_word[token_id];
    }
    return text;
}

std::vector<int> HybridTokenizer::encode(const std::string& text, bool add_special_tokens) {
    std::string preprocessed = replace(to_lower(text), "<|endoftext|>", "<EOS>");
    auto words = tokenize_words(preprocessed);
    std::vector<int> token_ids;
    if (add_special_tokens) token_ids.push_back(bos_id);
    for (const auto& word : words) {
        auto ids = encode_word_or_chars(word);
        token_ids.insert(token_ids.end(), ids.begin(), ids.end());
    }
    if (add_special_tokens) token_ids.push_back(eos_id);
    return token_ids;
}

std::string HybridTokenizer::extract_object(const std::string& content, const std::string& key) {
    std::string search = "\"" + key + "\": ";
    size_t key_pos = content.find(search);
    if (key_pos == std::string::npos) return "";
    size_t brace_pos = content.find('{', key_pos + search.length());
    if (brace_pos == std::string::npos) return "";
    size_t start = brace_pos + 1;
    int count = 1;
    size_t pos = start;
    while (pos < content.size() && count > 0) {
        if (content[pos] == '{') ++count;
        else if (content[pos] == '}') --count;
        ++pos;
    }
    if (count != 0) return "";
    return content.substr(start, pos - start - 1);
}

void HybridTokenizer::load_vocab(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Could not open vocab file: " << filepath << std::endl;
        return;
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    
    // A more robust regex to capture content inside quotes, allowing for escaped quotes.
    const std::regex str_key_regex(R"(\"((?:\\\"|[^\"])*)\":\s*(\d+))");
    const std::regex int_key_regex(R"(\"(\d+)\":\s*\"((?:\\\"|[^\"])*)\")");
    
    // Parse word_to_id
    std::string word_to_id_str = extract_object(content, "word_to_id");
    if (!word_to_id_str.empty()) {
        std::sregex_iterator iter(word_to_id_str.begin(), word_to_id_str.end(), str_key_regex);
        std::sregex_iterator end;
        for (; iter != end; ++iter) {
            std::smatch match = *iter;
            std::string key = unescape_json_string(match[1].str()); // Un-escape the key
            int val = std::stoi(match[2].str());
            word_to_id[key] = val;
        }
    }
    
    // Parse id_to_word
    std::string id_to_word_str = extract_object(content, "id_to_word");
    if (!id_to_word_str.empty()) {
        std::sregex_iterator iter(id_to_word_str.begin(), id_to_word_str.end(), int_key_regex);
        std::sregex_iterator end;
        for (; iter != end; ++iter) {
            std::smatch match = *iter;
            int key = std::stoi(match[1].str());
            std::string val = unescape_json_string(match[2].str()); // Un-escape the value
            id_to_word[key] = val;
        }
    }
    
    // Parse char_to_id
    // NOTE: The type of char_to_id must be std::unordered_map<std::string, int>
    std::string char_to_id_str = extract_object(content, "char_to_id");
    if (!char_to_id_str.empty()) {
        std::sregex_iterator iter(char_to_id_str.begin(), char_to_id_str.end(), str_key_regex);
        std::sregex_iterator end;
        for (; iter != end; ++iter) {
            std::smatch match = *iter;
            std::string key = unescape_json_string(match[1].str());
            int val = std::stoi(match[2].str());
            char_to_id[key] = val; // Store the string key
        }
    }
    
    // Parse id_to_char
    // NOTE: The type of id_to_char must be std::unordered_map<int, std::string>
    std::string id_to_char_str = extract_object(content, "id_to_char");
    if (!id_to_char_str.empty()) {
        std::sregex_iterator iter(id_to_char_str.begin(), id_to_char_str.end(), int_key_regex);
        std::sregex_iterator end;
        for (; iter != end; ++iter) {
            std::smatch match = *iter;
            int key = std::stoi(match[1].str());
            std::string val = unescape_json_string(match[2].str());
            id_to_char[key] = val; // Store the string value
        }
    }

    // Parse special_tokens
    std::string special_str = extract_object(content, "special_tokens");
    std::unordered_map<std::string, int> special_tokens;
    if (!special_str.empty()) {
        std::regex pair_regex(R"(\"([^\"]+?)\":\s*(\d+?)\s*)");
        std::sregex_iterator iter(special_str.begin(), special_str.end(), pair_regex);
        std::sregex_iterator end;
        for (; iter != end; ++iter) {
            std::smatch match = *iter;
            std::string key = match[1].str();
            int val = std::stoi(match[2].str());
            special_tokens[key] = val;
        }
    }

    // Set special ids
    auto it = special_tokens.find("<PAD>");
    pad_id = (it != special_tokens.end()) ? it->second : -1;
    it = special_tokens.find("<UNK>");
    unk_id = (it != special_tokens.end()) ? it->second : -1;
    it = special_tokens.find("<BOS>");
    bos_id = (it != special_tokens.end()) ? it->second : -1;
    it = special_tokens.find("<EOS>");
    eos_id = (it != special_tokens.end()) ? it->second : -1;
    it = special_tokens.find("<CHAR_START>");
    char_start_id = (it != special_tokens.end()) ? it->second : -1;
    it = special_tokens.find("<CHAR_END>");
    char_end_id = (it != special_tokens.end()) ? it->second : -1;

    // Parse vocab_size
    size_t vs_pos = content.find("\"vocab_size\": ");
    if (vs_pos != std::string::npos) {
        size_t num_start = content.find_first_of("0123456789", vs_pos + 14);
        if (num_start != std::string::npos) {
            size_t num_end = content.find_first_not_of("0123456789", num_start);
            std::string num_str = content.substr(num_start, (num_end == std::string::npos ? std::string::npos : num_end - num_start));
            vocab_size = std::stoi(num_str);
        }
    }
}

#ifdef TOKENIZER_DEBUG

int main(void){
    HybridTokenizer tokenizer;
    tokenizer.load_vocab("model/tinystories_tokenizer_vocab.json");
    int n = 0;
    std::cout << "================================================" << tokenizer.word_to_id.size() << std::endl;
    for(auto& [key, value] : tokenizer.word_to_id){
        std::cout << key << " " << value << std::endl;
        n++;
        if(n > 100) break;
    }
    std::cout << "================================================" << tokenizer.char_to_id.size() << std::endl;
    for(auto& [key, value] : tokenizer.char_to_id){
        std::cout << key << " " << value << std::endl;
        n++;
        if(n > 200) break;
    }
    std::cout << "================================================" << tokenizer.id_to_char.size() << std::endl;
    for(auto& [key, value] : tokenizer.id_to_char){
        std::cout << key << " " << value << std::endl;
        n++;
        if(n > 300) break;
    }
    std::cout << "================================================" << tokenizer.id_to_word.size() << std::endl;
    for(auto& [key, value] : tokenizer.id_to_word){
        std::cout << key << " " << value << std::endl;
        n++;
        if(n > 400) break;
    }
    std::string text = "this morning i walk";
    std::vector<int> token_ids = tokenizer.encode(text);
    std::cout << "Token IDs: ";
    for (int token_id : token_ids) {
        std::cout << token_id << " ";
    }
    std::cout << std::endl;

    std::string decoded_text = tokenizer.decode(token_ids);
    std::cout << "Decoded text: " << decoded_text << std::endl;

    return 0;
}


#endif