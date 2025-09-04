import torch
import torch.nn as nn
import torch.nn.functional as F
import math

class TinyTransformer(nn.Module):
    def __init__(self, vocab_size=6500, n_embd=192, n_head=6, n_layer=6, max_context=512, dropout=0.1):
        super().__init__()
        self.max_context = max_context
        # Token embeddings
        self.token_embedding = nn.Embedding(vocab_size, n_embd)
        # No positional embedding weights - we'll compute sinusoidal on the fly
        # Dropout
        self.dropout = nn.Dropout(dropout)
        # Transformer blocks
        self.blocks = nn.ModuleList([Block(n_embd, n_head, dropout) for _ in range(n_layer)])
        # Final layer norm
        self.ln_f = nn.LayerNorm(n_embd)
        # LM head
        self.lm_head = nn.Linear(n_embd, vocab_size, bias=False)
        
        # Initialize weights
        self.apply(self._init_weights)
    
    def _init_weights(self, module):
        if isinstance(module, nn.Linear):
            torch.nn.init.normal_(module.weight, mean=0.0, std=0.02)
            if module.bias is not None:
                torch.nn.init.zeros_(module.bias)
        elif isinstance(module, nn.Embedding):
            torch.nn.init.normal_(module.weight, mean=0.0, std=0.02)
    
    def get_sinusoidal_embeddings(self, seq_len, d_model):
        position = torch.arange(0, seq_len, dtype=torch.float).unsqueeze(1)
        div_term = torch.exp(torch.arange(0, d_model, 2).float() * (-math.log(10000.0) / d_model))
        pe = torch.zeros(seq_len, d_model)
        pe[:, 0::2] = torch.sin(position * div_term)
        pe[:, 1::2] = torch.cos(position * div_term)
        return pe
    
    def forward(self, idx, targets=None):
        B, T = idx.shape
        device = idx.device
        
        # Token embeddings
        tok_emb = self.token_embedding(idx)  # (B, T, C)
        
        # Sinusoidal positional embeddings
        pos_emb = self.get_sinusoidal_embeddings(T, tok_emb.size(-1)).to(device)  # (T, C)
        pos_emb = pos_emb.unsqueeze(0).expand(B, -1, -1)  # (B, T, C)
        
        # Add pos to tok
        x = tok_emb + pos_emb
        x = self.dropout(x)
        
        # Transformer blocks
        for block in self.blocks:
            x = block(x)
        
        # Final norm
        x = self.ln_f(x)
        
        # Logits
        logits = self.lm_head(x)  # (B, T, vocab_size)
        
        if targets is None:
            return logits
        
        # Compute loss if targets provided (for training)
        B, T, C = logits.shape
        logits = logits.view(B*T, C)
        targets = targets.view(B*T)
        loss = F.cross_entropy(logits, targets, ignore_index=0)  # Assuming 0 is PAD
        
        return logits, loss
    
    @torch.no_grad()
    def generate(self, idx, max_new_tokens, temperature=1.0, top_k=None):
        for _ in range(max_new_tokens):
            # Crop to max_context
            idx_cond = idx if idx.size(1) <= self.max_context else idx[:, -self.max_context:]
            # Forward
            logits = self(idx_cond)
            # Last timestep
            logits = logits[:, -1, :] / temperature
            if top_k is not None:
                v, _ = torch.topk(logits, min(top_k, logits.size(-1)))
                logits[logits < v[:, [-1]]] = -float('Inf')
            probs = F.softmax(logits, dim=-1)
            idx_next = torch.multinomial(probs, num_samples=1)
            idx = torch.cat((idx, idx_next), dim=1)
        return idx

class Block(nn.Module):
    def __init__(self, n_embd, n_head, dropout):
        super().__init__()
        head_size = n_embd // n_head
        self.sa = MultiHeadAttention(n_head, head_size, n_embd, dropout)
        self.ffwd = FeedForward(n_embd, dropout)
        self.ln1 = nn.LayerNorm(n_embd)
        self.ln2 = nn.LayerNorm(n_embd)
    
    def forward(self, x):
        # Residual connections
        x = x + self.sa(self.ln1(x))
        x = x + self.ffwd(self.ln2(x))
        return x

class MultiHeadAttention(nn.Module):
    def __init__(self, num_heads, head_size, n_embd, dropout):
        super().__init__()
        self.heads = nn.ModuleList([Head(head_size, n_embd, dropout) for _ in range(num_heads)])
        self.proj = nn.Linear(n_embd, n_embd, bias=False)
        self.dropout = nn.Dropout(dropout)
    
    def forward(self, x):
        out = torch.cat([h(x) for h in self.heads], dim=-1)
        out = self.dropout(self.proj(out))
        return out

class Head(nn.Module):
    def __init__(self, head_size, n_embd, dropout):
        super().__init__()
        self.key = nn.Linear(n_embd, head_size, bias=False)
        self.query = nn.Linear(n_embd, head_size, bias=False)
        self.value = nn.Linear(n_embd, head_size, bias=False)
        self.dropout = nn.Dropout(dropout)
    
    def forward(self, x):
        B, T, C = x.shape
        k = self.key(x)   # (B,T,head_size)
        q = self.query(x) # (B,T,head_size)
        v = self.value(x) # (B,T,head_size)
        
        # Attention scores
        wei = q @ k.transpose(-2, -1) * (C ** -0.5)  # (B,T,T)
        
        # Causal mask
        mask = torch.tril(torch.ones(T, T, device=x.device)).bool()
        wei = wei.masked_fill(~mask, float('-inf'))
        
        wei = F.softmax(wei, dim=-1)
        wei = self.dropout(wei)
        
        out = wei @ v  # (B,T,head_size)
        return out

class FeedForward(nn.Module):
    def __init__(self, n_embd, dropout):
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(n_embd, 4 * n_embd, bias=False),
            nn.GELU(),
            nn.Linear(4 * n_embd, n_embd, bias=False),
            nn.Dropout(dropout),
        )
    
    def forward(self, x):
        return self.net(x)