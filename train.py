#!/usr/bin/env python3
"""
train.py - PyTorch Trainer for Liora NNUE
Architecture: HalfKP (40960 -> 256)x2 -> 1
Reads: dataset.txt (FEN | Score | Result)
Outputs: liora_[epoch].nnue
"""

import sys
import os
import struct
import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import Dataset, DataLoader

NNUE_INPUTS = 40960
NNUE_L1 = 256
SCALE = 16

# Map piece chars to internal index
PIECE_MAP_W = {'P': 0, 'N': 1, 'B': 2, 'R': 3, 'Q': 4, 'p': 5, 'n': 6, 'b': 7, 'r': 8, 'q': 9}
PIECE_MAP_B = {'p': 0, 'n': 1, 'b': 2, 'r': 3, 'q': 4, 'P': 5, 'N': 6, 'B': 7, 'R': 8, 'Q': 9}

def fen_to_features(fen):
    parts = fen.strip().split()
    if len(parts) < 2:
        return [], [], 0
        
    board_str = parts[0]
    side = 0 if parts[1] == 'w' else 1
    
    rows = board_str.split('/')
    if len(rows) != 8:
        return [], [], side
        
    board = [None] * 64
    w_ksq, b_ksq = -1, -1
    
    for r in range(8):
        f = 0
        for ch in rows[7 - r]:
            if ch.isdigit():
                f += int(ch)
            else:
                sq = r * 8 + f
                board[sq] = ch
                if ch == 'K': w_ksq = sq
                if ch == 'k': b_ksq = sq
                f += 1
                
    if w_ksq == -1 or b_ksq == -1:
        return [], [], side

    w_feats = []
    b_feats = []
    
    for sq in range(64):
        p = board[sq]
        if p and p not in ['K', 'k'] and p in PIECE_MAP_W:
            # White perspective
            p_idx_w = PIECE_MAP_W[p]
            w_feats.append(w_ksq * 640 + p_idx_w * 64 + sq)
            
            # Black perspective (Rank flipped)
            p_idx_b = PIECE_MAP_B[p]
            b_feats.append((b_ksq ^ 56) * 640 + p_idx_b * 64 + (sq ^ 56))
            
    return w_feats, b_feats, side

class ChessDataset(Dataset):
    def __init__(self, filename, max_samples=None):
        self.samples = []
        print(f"Loading {filename}...")
        with open(filename, 'r') as f:
            for line in f:
                parts = line.strip().split('|')
                if len(parts) != 3: continue
                fen = parts[0].strip()
                try:
                    score = float(parts[1].strip())
                    result = float(parts[2].strip())
                except ValueError:
                    continue
                
                parts_fen = fen.split()
                if 'K' not in parts_fen[0] or 'k' not in parts_fen[0]:
                    continue

                # Parse side to move ('w' = 0, 'b' = 1)
                side_to_move = 0 if len(parts_fen) > 1 and parts_fen[1] == 'w' else 1

                # Target: Blend 80% search evaluation with 20% actual game result (from White's perspective)
                eval_prob = 1.0 / (1.0 + np.exp(-score / 400.0))
                target = 0.8 * eval_prob + 0.2 * result

                # CRITICAL FIX: The neural network evaluates from the perspective of the side to move ('us').
                # Because score and result are from White's perspective, invert target if Black is to move!
                if side_to_move == 1:
                    target = 1.0 - target
                
                self.samples.append((fen, target))
                if max_samples and len(self.samples) >= max_samples: 
                    break
                    
        print(f"Loaded {len(self.samples)} positions.")

    def __len__(self):
        return len(self.samples)

    def __getitem__(self, idx):
        fen, target = self.samples[idx]
        w_feats, b_feats, side = fen_to_features(fen)
        return w_feats, b_feats, side, np.float32(target)

def collate_fn(batch):
    batch_size = len(batch)
    w_tensor = torch.zeros((batch_size, NNUE_INPUTS), dtype=torch.float32)
    b_tensor = torch.zeros((batch_size, NNUE_INPUTS), dtype=torch.float32)
    sides = []
    targets = []
    
    for i, (w_f, b_f, side, target) in enumerate(batch):
        if w_f:
            w_tensor[i, w_f] = 1.0
        if b_f:
            b_tensor[i, b_f] = 1.0
        sides.append(side)
        targets.append(target)
        
    return w_tensor, b_tensor, torch.tensor(sides, dtype=torch.long), torch.tensor(targets, dtype=torch.float32)

class LioraNNUE(nn.Module):
    def __init__(self):
        super().__init__()
        self.ft = nn.Linear(NNUE_INPUTS, NNUE_L1)
        self.out = nn.Linear(2 * NNUE_L1, 1)

    def forward(self, w_dense, b_dense, sides):
        w_acc = torch.clamp(self.ft(w_dense), 0.0, 1.0)
        b_acc = torch.clamp(self.ft(b_dense), 0.0, 1.0)
        
        # Select side to move perspective
        us = torch.where(sides.unsqueeze(1) == 0, w_acc, b_acc)
        them = torch.where(sides.unsqueeze(1) == 0, b_acc, w_acc)
        
        x = torch.cat([us, them], dim=1)
        score = self.out(x)
        return torch.sigmoid(score / 2.5)

def export_to_nnue(model, filename="liora.nnue"):
    print(f"Exporting quantized weights to {filename}...")
    ft_w = model.ft.weight.data.cpu().numpy() # [256, 40960]
    ft_b = model.ft.bias.data.cpu().numpy()   # [256]
    out_w = model.out.weight.data.cpu().numpy() # [1, 512]
    out_b = model.out.bias.data.cpu().numpy()   # [1]

    # Quantization scales
    FV_SCALE = 127.0  # Feature transformer activations [0, 1] -> [0, 127]
    OUT_SCALE = 64.0  # Output weights scale

    q_ft_w = np.clip(np.round(ft_w.T * FV_SCALE), -32767, 32767).astype(np.int16)
    q_ft_b = np.clip(np.round(ft_b * FV_SCALE), -32767, 32767).astype(np.int16)
    q_out_w = np.clip(np.round(out_w * OUT_SCALE), -32767, 32767).astype(np.int16)
    
    # CRITICAL FIX: The dot product in C is (clipped * out_w) = (127 * out_w_float * 64) = 8128 * score.
    # Therefore, out_b MUST be scaled by the exact same combined factor (127 * 64 = 8128), NOT (64 * 16)!
    q_out_b = np.int32(np.round(out_b[0] * FV_SCALE * OUT_SCALE))

    with open(filename, "wb") as f:
        f.write(q_ft_b.tobytes())
        f.write(q_ft_w.tobytes())
        f.write(struct.pack("<i", q_out_b))
        f.write(q_out_w.tobytes())
    print(f"Done! {filename} is ready.")

def main():
    if not os.path.exists("dataset.txt"):
        print("Error: dataset.txt not found. Run 'datagen 1000' inside Liora first!")
        return

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Training on device: {device}")

    # Charge toutes les positions sans limite
    dataset = ChessDataset("dataset.txt", max_samples=None)
    loader = DataLoader(dataset, batch_size=256, shuffle=True, collate_fn=collate_fn, num_workers=2, pin_memory=True)

    model = LioraNNUE().to(device)
    optimizer = optim.Adam(model.parameters(), lr=0.001)
    criterion = nn.MSELoss()

    epochs = 5
    for epoch in range(1, epochs + 1):
        total_loss = 0.0
        model.train()
        for w_d, b_d, sides, targets in loader:
            w_d = w_d.to(device, non_blocking=True)
            b_d = b_d.to(device, non_blocking=True)
            sides = sides.to(device, non_blocking=True)
            targets = targets.to(device, non_blocking=True)
            
            optimizer.zero_grad()
            preds = model(w_d, b_d, sides).squeeze(-1)
            loss = criterion(preds, targets)
            loss.backward()
            optimizer.step()
            total_loss += loss.item()

        avg_loss = total_loss / len(loader)
        print(f"Epoch {epoch}/{epochs} - Loss: {avg_loss:.5f}")

        # Sauvegarde un nouveau fichier pour chaque epoch (ex: liora_1.nnue, liora_2.nnue, etc.)
        epoch_filename = f"liora_{epoch}.nnue"
        export_to_nnue(model, epoch_filename)

    print("\nTraining completed successfully! All checkpoints exported.")

if __name__ == "__main__":
    main()