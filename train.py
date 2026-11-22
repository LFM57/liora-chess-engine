#!/usr/bin/env python3
"""
train.py - High-Performance PyTorch Trainer for Liora NNUE
Architecture: (768 -> 256)x2 -> 1 with SCReLU activation
Uses: Sparse EmbeddingBag, Memory-Mapped Binary Dataset, Vectorized Batches
Outputs: liora_[epoch].nnue, liora.nnue
"""

import os
import sys
import time
import math
import shutil
import struct
import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import Dataset, DataLoader

NNUE_INPUTS = 768        # 12 piece types (including both kings) * 64 squares
NNUE_L1 = 256
NNUE_QA = 181            # Feature quantization scale (181^2 = 32761 <= 32767)
NNUE_QB = 64             # Output weight quantization scale
NNUE_SCALE = 400         # Centipawn output scale
PADDING_IDX = 768        # Index 768 is used as a zero-weight padding slot
MAX_PIECES = 32          # Max pieces on a chess board (including both kings)
MAX_CP_SCORE = 3000.0    # Filter threshold for extreme tactical blowouts

PIECE_MAP_W = {'P': 0, 'N': 1, 'B': 2, 'R': 3, 'Q': 4, 'K': 5, 'p': 6, 'n': 7, 'b': 8, 'r': 9, 'q': 10, 'k': 11}
PIECE_MAP_B = {'p': 0, 'n': 1, 'b': 2, 'r': 3, 'q': 4, 'k': 5, 'P': 6, 'N': 7, 'B': 8, 'R': 9, 'Q': 10, 'K': 11}

# Binary record definition (136 bytes per position)
ENTRY_DTYPE = np.dtype([
    ('side', np.uint8),
    ('target', np.float32),
    ('w_feats', np.uint16, MAX_PIECES),
    ('b_feats', np.uint16, MAX_PIECES)
], align=True)


KNIGHT_OFFSETS = ((-2, -1), (-2, 1), (-1, -2), (-1, 2), (1, -2), (1, 2), (2, -1), (2, 1))
KING_OFFSETS = ((-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1))
ORTHO_DIRS = ((1, 0), (-1, 0), (0, 1), (0, -1))
DIAG_DIRS = ((1, 1), (1, -1), (-1, 1), (-1, -1))


def is_in_check(board, ksq, side):
    """Returns True if `side` (0=White, 1=Black) has its king at `ksq` in check."""
    kr = ksq >> 3
    kf = ksq & 7

    if side == 0:
        e_pawn, e_knight, e_king = 'p', 'n', 'k'
        e_ortho, e_diag = ('r', 'q'), ('b', 'q')
        pr = kr + 1
    else:
        e_pawn, e_knight, e_king = 'P', 'N', 'K'
        e_ortho, e_diag = ('R', 'Q'), ('B', 'Q')
        pr = kr - 1

    # Enemy pawn attacks
    if 0 <= pr < 8:
        if kf > 0 and board[(pr << 3) | (kf - 1)] == e_pawn:
            return True
        if kf < 7 and board[(pr << 3) | (kf + 1)] == e_pawn:
            return True

    # Enemy knight attacks
    for dr, df in KNIGHT_OFFSETS:
        r, f = kr + dr, kf + df
        if 0 <= r < 8 and 0 <= f < 8 and board[(r << 3) | f] == e_knight:
            return True

    # Enemy king adjacency
    for dr, df in KING_OFFSETS:
        r, f = kr + dr, kf + df
        if 0 <= r < 8 and 0 <= f < 8 and board[(r << 3) | f] == e_king:
            return True

    # Enemy rook / queen orthogonal rays
    for dr, df in ORTHO_DIRS:
        r, f = kr + dr, kf + df
        while 0 <= r < 8 and 0 <= f < 8:
            ch = board[(r << 3) | f]
            if ch is not None:
                if ch in e_ortho:
                    return True
                break
            r += dr
            f += df

    # Enemy bishop / queen diagonal rays
    for dr, df in DIAG_DIRS:
        r, f = kr + dr, kf + df
        while 0 <= r < 8 and 0 <= f < 8:
            ch = board[(r << 3) | f]
            if ch is not None:
                if ch in e_diag:
                    return True
                break
            r += dr
            f += df

    return False


def parse_fen_fast(fen_str, filter_checks=True, stats=None):
    """Fast single-pass FEN parser returning original and optionally horizontally mirrored positions."""
    parts = fen_str.split()
    if len(parts) < 2:
        return None
    board_str = parts[0]
    side = 0 if parts[1] == 'w' else 1
    castling = parts[2] if len(parts) > 2 else "-"
    allow_mirror = (castling == "-")

    rank = 7
    file = 0
    w_ksq = -1
    b_ksq = -1
    board = [None] * 64
    pieces = []

    for ch in board_str:
        if ch == '/':
            rank -= 1
            file = 0
        elif '1' <= ch <= '8':
            file += ord(ch) - 48
        else:
            if rank < 0 or rank > 7 or file < 0 or file > 7:
                return None
            sq = (rank << 3) | file
            board[sq] = ch
            if ch == 'K':
                w_ksq = sq
            elif ch == 'k':
                b_ksq = sq
            pieces.append((ch, sq))
            file += 1

    if w_ksq == -1 or b_ksq == -1:
        return None

    # Filter out in-check positions (engine never calls static Evaluate() in check)
    if filter_checks and is_in_check(board, w_ksq if side == 0 else b_ksq, side):
        if stats is not None:
            stats['in_check'] += 1
        return None

    w_feats = []
    b_feats = []
    w_feats_m = []
    b_feats_m = []

    for ch, sq in pieces:
        p_w = PIECE_MAP_W.get(ch)
        if p_w is not None:
            w_feats.append(p_w * 64 + sq)
            if allow_mirror:
                w_feats_m.append(p_w * 64 + (sq ^ 7))

            p_b = PIECE_MAP_B[ch]
            b_feats.append(p_b * 64 + (sq ^ 56))
            if allow_mirror:
                b_feats_m.append(p_b * 64 + (sq ^ 63))

    w_arr = np.full(MAX_PIECES, PADDING_IDX, dtype=np.uint16)
    b_arr = np.full(MAX_PIECES, PADDING_IDX, dtype=np.uint16)
    w_arr[:len(w_feats)] = w_feats[:MAX_PIECES]
    b_arr[:len(b_feats)] = b_feats[:MAX_PIECES]

    mirr = None
    if allow_mirror:
        w_arr_m = np.full(MAX_PIECES, PADDING_IDX, dtype=np.uint16)
        b_arr_m = np.full(MAX_PIECES, PADDING_IDX, dtype=np.uint16)
        w_arr_m[:len(w_feats_m)] = w_feats_m[:MAX_PIECES]
        b_arr_m[:len(b_feats_m)] = b_feats_m[:MAX_PIECES]
        mirr = (side, w_arr_m, b_arr_m)

    return (side, w_arr, b_arr), mirr


def convert_txt_to_bin(txt_path="dataset.txt", bin_path="dataset.bin", chunk_size=100000):
    """One-time preprocessor: Converts dataset.txt into a fast binary memory map."""
    print(f"[*] Preprocessing '{txt_path}' -> '{bin_path}'...")
    start_time = time.time()

    chunk = []
    total_valid = 0
    stats = {'in_check': 0, 'mate': 0, 'score_cap': 0}

    with open(txt_path, 'r', encoding='utf-8') as fin, open(bin_path, 'wb') as fout:
        for line_idx, line in enumerate(fin, 1):
            line_str = line.strip()
            parts = line_str.split('|') if '|' in line_str else line_str.split(',')
            if len(parts) < 2:
                continue

            fen = parts[0].strip()
            eval_str = parts[1].strip()

            # Filter out checkmate annotations (e.g. Kaggle's "#+3" or "#-1")
            if '#' in eval_str:
                stats['mate'] += 1
                continue
            try:
                score = float(eval_str)
            except ValueError:
                continue

            # Filter out extreme tactical blowouts before computing evaluation targets
            if abs(score) > MAX_CP_SCORE:
                stats['score_cap'] += 1
                continue

            parsed = parse_fen_fast(fen, filter_checks=True, stats=stats)
            if parsed is None:
                continue

            orig, mirr = parsed

            # Evaluation target blended with game outcome (WDL) from White's perspective
            eval_target = 1.0 / (1.0 + np.exp(-score / 400.0))
            if len(parts) >= 3:
                try:
                    wdl = float(parts[2].strip())  # 1.0 = White win, 0.5 = Draw, 0.0 = Black win
                    LAMBDA = 0.7                   # 70% search eval, 30% game result
                    target = LAMBDA * eval_target + (1.0 - LAMBDA) * wdl
                except ValueError:
                    target = eval_target
            else:
                target = eval_target

            # Flip target to side-to-move perspective
            if orig[0] == 1:
                target = 1.0 - target

            chunk.append((orig[0], target, orig[1], orig[2]))
            if mirr is not None:
                chunk.append((mirr[0], target, mirr[1], mirr[2]))

            if len(chunk) >= chunk_size:
                records = np.zeros(len(chunk), dtype=ENTRY_DTYPE)
                for i, (s, t, w, b) in enumerate(chunk):
                    records[i]['side'] = s
                    records[i]['target'] = t
                    records[i]['w_feats'] = w
                    records[i]['b_feats'] = b
                fout.write(records.tobytes())
                total_valid += len(chunk)
                chunk.clear()
                print(f"\rParsed {line_idx:,} lines ({total_valid:,} positions saved)...", end="", flush=True)

        if chunk:
            records = np.zeros(len(chunk), dtype=ENTRY_DTYPE)
            for i, (s, t, w, b) in enumerate(chunk):
                records[i]['side'] = s
                records[i]['target'] = t
                records[i]['w_feats'] = w
                records[i]['b_feats'] = b
            fout.write(records.tobytes())
            total_valid += len(chunk)

    elapsed = time.time() - start_time
    print(
        f"\n[+] Done! Preprocessed {total_valid:,} positions in {elapsed:.1f}s "
        f"(filtered: {stats['in_check']:,} in-check, {stats['mate']:,} mate, {stats['score_cap']:,} |score|>{int(MAX_CP_SCORE)})."
    )


class BinaryChessDataset(Dataset):
    def __init__(self, bin_path):
        self.data = np.memmap(bin_path, dtype=ENTRY_DTYPE, mode='r')

    def __len__(self):
        return len(self.data)

    def __getitem__(self, idx):
        row = self.data[idx]
        return row['w_feats'], row['b_feats'], row['side'], row['target']


def fast_collate(batch):
    # Vectorized batch aggregation
    w_feats = torch.from_numpy(np.array([x[0] for x in batch], dtype=np.int64))
    b_feats = torch.from_numpy(np.array([x[1] for x in batch], dtype=np.int64))
    sides = torch.tensor([x[2] for x in batch], dtype=torch.long)
    targets = torch.tensor([x[3] for x in batch], dtype=torch.float32)
    return w_feats, b_feats, sides, targets


class LioraNNUE(nn.Module):
    def __init__(self):
        super().__init__()
        # 768 real piece-square inputs (including both kings) + 1 dummy slot at 768 for zero padding
        self.ft = nn.EmbeddingBag(NNUE_INPUTS + 1, NNUE_L1, mode='sum', padding_idx=PADDING_IDX)
        self.ft_bias = nn.Parameter(torch.zeros(NNUE_L1))
        self.out = nn.Linear(2 * NNUE_L1, 1)

        # Break symmetry with random initialization while keeping padding slot strictly zeroed
        nn.init.normal_(self.ft.weight, mean=0.0, std=0.02)
        with torch.no_grad():
            self.ft.weight[PADDING_IDX].zero_()

        # Positive bias places initial activations safely inside the active SCReLU region (0.0, 1.0)
        nn.init.constant_(self.ft_bias, 0.5)

        # Output layer initialized with standard fan-in scaling
        nn.init.normal_(self.out.weight, mean=0.0, std=1.0 / math.sqrt(2 * NNUE_L1))
        nn.init.zeros_(self.out.bias)

    def forward(self, w_feats, b_feats, sides):
        # Sparse accumulation + SCReLU activation: shape [batch_size, 256]
        w_acc = torch.clamp(self.ft(w_feats) + self.ft_bias, 0.0, 1.0).square()
        b_acc = torch.clamp(self.ft(b_feats) + self.ft_bias, 0.0, 1.0).square()

        # Perspective selection
        sides_mask = (sides == 0).unsqueeze(1)
        us = torch.where(sides_mask, w_acc, b_acc)
        them = torch.where(sides_mask, b_acc, w_acc)

        x = torch.cat([us, them], dim=1)
        # Output directly in cp / 400 logit units (BCEWithLogitsLoss applies sigmoid internally)
        return self.out(x)


def export_to_nnue(model, filename="liora.nnue"):
    print(f"Exporting quantized weights to {filename}...")
    ft_w = model.ft.weight.data[:NNUE_INPUTS].cpu().numpy()  # [768, 256]
    ft_b = model.ft_bias.data.cpu().numpy()                  # [256]
    out_w = model.out.weight.data.cpu().numpy()              # [1, 512]
    out_b = model.out.bias.data.cpu().numpy()                # [1]

    q_ft_w = np.clip(np.round(ft_w * NNUE_QA), -32767, 32767).astype(np.int16)
    q_ft_b = np.clip(np.round(ft_b * NNUE_QA), -32767, 32767).astype(np.int16)
    q_out_w = np.clip(np.round(out_w * NNUE_QB), -32767, 32767).astype(np.int16)
    q_out_b = np.int32(np.round(out_b[0] * NNUE_QA * NNUE_QA * NNUE_QB))

    with open(filename, "wb") as f:
        f.write(q_ft_b.tobytes())
        f.write(q_ft_w.tobytes())
        f.write(struct.pack("<i", q_out_b))
        f.write(q_out_w.tobytes())
    print(f"[+] Done! {filename} is ready.")


def needs_bin_rebuild(txt_path, bin_path, force_rebuild=False):
    """Checks whether dataset.bin is missing, invalid, forced, or older than dataset.txt / train.py."""
    if not os.path.exists(bin_path):
        return True
    bin_size = os.path.getsize(bin_path)
    if bin_size == 0 or (bin_size % ENTRY_DTYPE.itemsize) != 0:
        return True
    if not os.path.exists(txt_path):
        return False
    if force_rebuild:
        return True
    bin_mtime = os.path.getmtime(bin_path)
    if os.path.getmtime(txt_path) > bin_mtime:
        return True
    script_path = os.path.abspath(__file__)
    if os.path.exists(script_path) and os.path.getmtime(script_path) > bin_mtime:
        return True
    return False


def main():
    txt_path = "dataset.txt"
    bin_path = "dataset.bin"
    force_rebuild = ("--rebuild" in sys.argv) or ("--force" in sys.argv)

    if needs_bin_rebuild(txt_path, bin_path, force_rebuild=force_rebuild):
        if not os.path.exists(txt_path):
            print(f"Error: Neither valid {bin_path} nor {txt_path} found.")
            return
        convert_txt_to_bin(txt_path, bin_path)

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Training on device: {device}")

    # Memory-mapped dataset (instant startup)
    dataset = BinaryChessDataset(bin_path)
    
    # Batch size 8192 for maximum GPU utilization
    BATCH_SIZE = 8192
    num_workers = min(4, os.cpu_count() or 1)
    loader = DataLoader(
        dataset,
        batch_size=BATCH_SIZE,
        shuffle=True,
        collate_fn=fast_collate,
        num_workers=num_workers,
        pin_memory=(device.type == "cuda"),
        persistent_workers=(num_workers > 0)
    )

    model = LioraNNUE().to(device)
    optimizer = optim.AdamW(model.parameters(), lr=0.001, weight_decay=1e-4)
    scheduler = optim.lr_scheduler.ExponentialLR(optimizer, gamma=0.95)
    criterion = nn.BCEWithLogitsLoss()

    epochs = 20
    print(f"\nStarting training on {len(dataset):,} positions ({len(loader)} batches/epoch)...")

    for epoch in range(1, epochs + 1):
        total_loss = 0.0
        model.train()
        t0 = time.time()

        for step, (w_f, b_f, sides, targets) in enumerate(loader, 1):
            w_f = w_f.to(device, non_blocking=True)
            b_f = b_f.to(device, non_blocking=True)
            sides = sides.to(device, non_blocking=True)
            targets = targets.to(device, non_blocking=True)

            optimizer.zero_grad(set_to_none=True)
            preds = model(w_f, b_f, sides).squeeze(-1)
            loss = criterion(preds, targets)
            loss.backward()
            optimizer.step()

            total_loss += loss.item()

            if step % 200 == 0 or step == len(loader):
                elapsed = time.time() - t0
                speed = (step * BATCH_SIZE) / elapsed
                print(f"\rEpoch {epoch}/{epochs} | Step {step}/{len(loader)} | Loss: {loss.item():.5f} | {speed:,.0f} pos/sec", end="")

        avg_loss = total_loss / len(loader)
        epoch_time = time.time() - t0
        current_lr = scheduler.get_last_lr()[0]
        print(f"\nEpoch {epoch}/{epochs} Complete - Avg Loss: {avg_loss:.5f} - LR: {current_lr:.6f} - Time: {epoch_time:.1f}s")

        scheduler.step()

        epoch_filename = f"liora_{epoch}.nnue"
        export_to_nnue(model, epoch_filename)
        shutil.copyfile(epoch_filename, "liora.nnue")
        print("[+] Updated default network file: liora.nnue")

    print("\nTraining completed successfully! All checkpoints exported.")


if __name__ == "__main__":
    main()
