import re
from collections import Counter
import torch
from typing import List, Dict, Union

class HybridTokenizer:
    """
    Hybrid word-character level tokenizer for TinyStories corpus.
    Uses word-level tokenization for 6400 most frequent words,
    falls back to character-level for rare words.
    """
    
    def __init__(self, vocab_size: int = 6400):
        self.vocab_size = vocab_size
        self.word_to_id = {}
        self.id_to_word = {}
        self.char_to_id = {}
        self.id_to_char = {}
        
        # Special tokens
        self.special_tokens = {
            '<PAD>': 0,
            '<UNK>': 1, 
            '<BOS>': 2,
            '<EOS>': 3,
            '<CHAR_START>': 4,  # Marker for character-level tokens
            '<CHAR_END>': 5     # Marker for end of character sequence
        }
        
        self.pad_id = 0
        self.unk_id = 1
        self.bos_id = 2
        self.eos_id = 3
        self.char_start_id = 4
        self.char_end_id = 5
        
        # Initialize mappings with special tokens
        for token, idx in self.special_tokens.items():
            self.word_to_id[token] = idx
            self.id_to_word[idx] = token
    
    def preprocess_text(self, text: str) -> str:
        """Preprocess text: lowercase, replace endoftext marker"""
        text = text.lower()
        text = text.replace('<|endoftext|>', '<EOS>')
        return text
    
    def tokenize_words(self, text: str) -> List[str]:
        """Split text into words, handling punctuation as separate tokens"""
        # Define punctuation to split on
        punctuation = r'[.,!?;:"\'\-\(\)\[\]{}]'
        
        # Replace punctuation with space + punctuation + space
        text = re.sub(punctuation, r' \g<0> ', text)
        
        # Split on whitespace and filter empty strings
        tokens = [token for token in text.split() if token.strip()]
        
        return tokens
    
    def build_vocab(self, stories: List[str]):
        """Build vocabulary from stories"""
        print("Building vocabulary...")
        
        # Collect all words
        all_words = []
        for story in stories:
            preprocessed = self.preprocess_text(story)
            words = self.tokenize_words(preprocessed)
            all_words.extend(words)
        
        # Count word frequencies
        word_counts = Counter(all_words)
        print(f"Total unique words: {len(word_counts)}")
        
        # Get most frequent words (excluding special tokens already added)
        most_frequent = word_counts.most_common(self.vocab_size)
        
        # Add words to vocabulary (starting after special tokens)
        current_id = len(self.special_tokens)
        for word, count in most_frequent:
            if word not in self.word_to_id:  # Skip if already in special tokens
                self.word_to_id[word] = current_id
                self.id_to_word[current_id] = word
                current_id += 1
        
        print(f"Word vocabulary size: {len(self.word_to_id)}")
        
        # Build character vocabulary from all unique characters
        all_chars = set()
        for story in stories:
            preprocessed = self.preprocess_text(story)
            all_chars.update(preprocessed)
        
        # Add characters to character vocabulary
        char_id = 0
        for char in sorted(all_chars):
            if char not in [' ', '\n', '\t']:  # Skip whitespace chars
                self.char_to_id[char] = char_id
                self.id_to_char[char_id] = char
                char_id += 1
        
        print(f"Character vocabulary size: {len(self.char_to_id)}")
        
        # Calculate total vocabulary size
        total_vocab_size = len(self.word_to_id) + len(self.char_to_id)
        print(f"Total vocabulary size: {total_vocab_size}")
    
    def encode_word_or_chars(self, word: str) -> List[int]:
        """Encode a single word, falling back to characters if not in vocab"""
        if word in self.word_to_id:
            return [self.word_to_id[word]]
        else:
            # Use character-level encoding
            char_ids = [self.char_start_id]
            for char in word:
                if char in self.char_to_id:
                    # Offset character IDs to avoid collision with word IDs
                    char_ids.append(self.char_to_id[char] + len(self.word_to_id))
                else:
                    char_ids.append(self.unk_id)
            char_ids.append(self.char_end_id)
            return char_ids
    
    def encode(self, text: str, add_special_tokens: bool = True) -> List[int]:
        """Encode text to list of token IDs"""
        preprocessed = self.preprocess_text(text)
        words = self.tokenize_words(preprocessed)
        
        token_ids = []
        
        if add_special_tokens:
            token_ids.append(self.bos_id)
        
        for word in words:
            token_ids.extend(self.encode_word_or_chars(word))
        
        if add_special_tokens:
            token_ids.append(self.eos_id)
        
        return token_ids
    
    def decode_chars(self, char_ids: List[int]) -> str:
        """Decode character-level token IDs back to string"""
        chars = []
        for char_id in char_ids:
            if char_id == self.char_start_id or char_id == self.char_end_id:
                continue
            
            # Adjust for character ID offset
            actual_char_id = char_id - len(self.word_to_id)
            if actual_char_id in self.id_to_char:
                chars.append(self.id_to_char[actual_char_id])
            else:
                chars.append('<UNK>')
        
        return ''.join(chars)
    
    def decode(self, token_ids: List[int]) -> str:
        """Decode list of token IDs back to text"""
        tokens = []
        i = 0
        
        while i < len(token_ids):
            token_id = token_ids[i]
            
            if token_id == self.char_start_id:
                # Find the end of character sequence
                char_seq = []
                i += 1
                while i < len(token_ids) and token_ids[i] != self.char_end_id:
                    char_seq.append(token_ids[i])
                    i += 1
                
                # Decode character sequence
                if char_seq:
                    tokens.append(self.decode_chars(char_seq))
                
                # Skip the CHAR_END token
                if i < len(token_ids) and token_ids[i] == self.char_end_id:
                    i += 1
                continue
            
            # Regular word token
            if token_id in self.id_to_word:
                token = self.id_to_word[token_id]
                if token not in ['<PAD>', '<BOS>', '<EOS>']:
                    tokens.append(token)
            else:
                tokens.append('<UNK>')
            
            i += 1
        
        return ' '.join(tokens)
    
    def encode_batch(self, texts: List[str], max_length: int = None, 
                    padding: bool = True) -> Dict[str, torch.Tensor]:
        """Encode batch of texts for PyTorch models"""
        encoded_texts = [self.encode(text) for text in texts]
        
        if max_length is None:
            max_length = max(len(seq) for seq in encoded_texts)
        
        # Truncate sequences that are too long
        encoded_texts = [seq[:max_length] for seq in encoded_texts]
        
        if padding:
            # Pad sequences to max_length
            padded_sequences = []
            attention_masks = []
            
            for seq in encoded_texts:
                padded_seq = seq + [self.pad_id] * (max_length - len(seq))
                attention_mask = [1] * len(seq) + [0] * (max_length - len(seq))
                
                padded_sequences.append(padded_seq)
                attention_masks.append(attention_mask)
            
            return {
                'input_ids': torch.tensor(padded_sequences, dtype=torch.long),
                'attention_mask': torch.tensor(attention_masks, dtype=torch.long)
            }
        else:
            return {
                'input_ids': [torch.tensor(seq, dtype=torch.long) for seq in encoded_texts]
            }
    
    def get_vocab_size(self) -> int:
        """Get total vocabulary size"""
        return len(self.word_to_id) + len(self.char_to_id)
    
    def save_vocab(self, filepath: str):
        """Save vocabulary to file"""
        import json
        
        vocab_data = {
            'word_to_id': self.word_to_id,
            'id_to_word': {str(k): v for k, v in self.id_to_word.items()},
            'char_to_id': self.char_to_id,
            'id_to_char': {str(k): v for k, v in self.id_to_char.items()},
            'vocab_size': self.vocab_size,
            'special_tokens': self.special_tokens
        }
        
        with open(filepath, 'w', encoding='utf-8') as f:
            json.dump(vocab_data, f, ensure_ascii=False, indent=2)
        
        print(f"Vocabulary saved to {filepath}")
    
    def load_vocab(self, filepath: str):
        """Load vocabulary from file"""
        import json
        
        with open(filepath, 'r', encoding='utf-8') as f:
            vocab_data = json.load(f)
        
        self.word_to_id = vocab_data['word_to_id']
        self.id_to_word = {int(k): v for k, v in vocab_data['id_to_word'].items()}
        self.char_to_id = vocab_data['char_to_id']
        self.id_to_char = {int(k): v for k, v in vocab_data['id_to_char'].items()}
        self.vocab_size = vocab_data['vocab_size']
        self.special_tokens = vocab_data['special_tokens']
        
        print(f"Vocabulary loaded from {filepath}")

# print("HybridTokenizer class created successfully!")
