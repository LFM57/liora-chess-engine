#!/usr/bin/env python3
"""
Automated UCI_Elo calibration harness for Liora using cutechess-cli.

Supports:
  1. Local / in-VM execution (`run`):
     - Stages Linux engine binaries in native Linux storage (/tmp or /content)
       before `chmod +x` (safe on FAT32/exFAT mounts).
     - Optionally bootstraps cutechess-cli v1.5.1 AppImage, 8moves_v3.pgn,
       and official Stockfish Linux binary.
     - Runs paired-opening matches (`-games N -repeat`, never `-games N -rounds M`)
       with standard adjudication flags and computes Elo + 95% CI per anchor.
     - Supports Stockfish UCI_LimitStrength/UCI_Elo reference sweep AND
       cross-checks against fixed Liora reference binaries.
  2. Google Colab orchestration (`colab`):
     - Provisions or reuses a Colab session via `colab` CLI.
     - Uploads C sources, NNUE weights, and optional reference binaries.
     - Compiles Liora natively on the Colab VM and runs the calibration sweep.
     - Downloads structured JSON results and optionally stops the session.
"""

import argparse
import json
import math
import os
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request
import zipfile
from pathlib import Path
from typing import Dict, List, Optional, Tuple


DEFAULT_ANCHORS = [1200, 1400, 1600, 1800, 2100, 2400, 2750, 3000]
CUTECHESS_APPIMAGE_URL = (
    "https://github.com/cutechess/cutechess/releases/download/v1.5.1/"
    "Cute_Chess-1.5.1-x86_64.AppImage"
)
BOOK_ZIP_URL = (
    "https://raw.githubusercontent.com/official-stockfish/books/master/"
    "8moves_v3.pgn.zip"
)
STOCKFISH_API_URL = (
    "https://api.github.com/repos/official-stockfish/Stockfish/releases/latest"
)
SOURCE_FILES = [
    "board.c",
    "defs.h",
    "incbin.h",
    "main.c",
    "Makefile",
    "nnue.c",
    "nnue.h",
    "nnue_embed.c",
    "polyglot.c",
    "polykeys.h",
    "search.c",
    "calibrate_elo.py",
]


def log(msg: str) -> None:
    ts = time.strftime("%H:%M:%S")
    print(f"[{ts}] {msg}", flush=True)


def run_cmd(
    cmd: List[str],
    cwd: Optional[str] = None,
    capture: bool = False,
    check: bool = True,
) -> subprocess.CompletedProcess:
    if capture:
        return subprocess.run(
            cmd,
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=check,
        )
    return subprocess.run(cmd, cwd=cwd, check=check)


def stage_executable(src_path: Path, stage_dir: Path, alias: Optional[str] = None) -> Path:
    """Copy a binary to native Linux storage and ensure executable bit is set."""
    src_path = src_path.resolve()
    if not src_path.is_file():
        raise FileNotFoundError(f"Engine binary not found: {src_path}")
    stage_dir.mkdir(parents=True, exist_ok=True)
    dst_name = alias if alias else src_path.name
    dst_path = stage_dir / dst_name
    if dst_path.resolve() != src_path:
        shutil.copy2(src_path, dst_path)
    mode = dst_path.stat().st_mode
    dst_path.chmod(mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    return dst_path


def cpu_has_flag(flag: str) -> bool:
    try:
        with open("/proc/cpuinfo", "r", encoding="utf-8", errors="ignore") as f:
            text = f.read()
        return f" {flag} " in text or f"\t{flag} " in text or f" {flag}\n" in text
    except OSError:
        return False


def download_file(url: str, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    log(f"Downloading {url} -> {dst}")
    req = urllib.request.Request(url, headers={"User-Agent": "liora-calibrate-elo/1.0"})
    with urllib.request.urlopen(req, timeout=120) as resp, open(dst, "wb") as out:
        shutil.copyfileobj(resp, out)


def bootstrap_cutechess(work_dir: Path) -> Path:
    extracted = work_dir / "squashfs-root" / "usr" / "bin" / "cutechess-cli"
    if extracted.is_file():
        extracted.chmod(extracted.stat().st_mode | 0o111)
        return extracted

    which = shutil.which("cutechess-cli")
    if which:
        return Path(which)

    appimage = work_dir / "Cute_Chess.AppImage"
    if not appimage.is_file():
        download_file(CUTECHESS_APPIMAGE_URL, appimage)
    appimage.chmod(appimage.stat().st_mode | 0o111)
    log("Extracting Cute_Chess.AppImage...")
    run_cmd([str(appimage), "--appimage-extract"], cwd=str(work_dir), capture=True)
    if not extracted.is_file():
        raise RuntimeError(f"Failed to extract cutechess-cli at {extracted}")
    extracted.chmod(extracted.stat().st_mode | 0o111)
    return extracted


def bootstrap_book(work_dir: Path) -> Path:
    book_path = work_dir / "8moves_v3.pgn"
    if book_path.is_file() and book_path.stat().st_size > 1000:
        return book_path

    zip_path = work_dir / "8moves_v3.pgn.zip"
    download_file(BOOK_ZIP_URL, zip_path)
    with zipfile.ZipFile(zip_path, "r") as zf:
        zf.extractall(work_dir)
    if not book_path.is_file():
        raise RuntimeError(f"8moves_v3.pgn not found after unzipping {zip_path}")
    return book_path


def bootstrap_stockfish(work_dir: Path) -> Path:
    sf_bin = work_dir / "stockfish_bin"
    if sf_bin.is_file():
        sf_bin.chmod(sf_bin.stat().st_mode | 0o111)
        return sf_bin

    # Prefer official GitHub Stockfish release so UCI_Elo (1320..3190 CCRL Blitz) is modern.
    arch_pref = "avx2" if cpu_has_flag("avx2") else "sse41-popcnt"
    direct_candidates = [
        "stockfish-linux-x86-64-universal.tar.gz",
        f"stockfish-ubuntu-x86-64-{arch_pref}.tar",
    ]
    tar_path: Optional[Path] = None
    for fname in direct_candidates:
        direct_url = (
            f"https://github.com/official-stockfish/Stockfish/releases/latest/download/{fname}"
        )
        candidate_tar = work_dir / fname
        try:
            download_file(direct_url, candidate_tar)
            tar_path = candidate_tar
            break
        except Exception as exc:
            log(f"Direct Stockfish download failed for {fname} ({exc})")

    if tar_path is None:
        log("Querying GitHub releases API for Linux x86-64 Stockfish asset...")
        req = urllib.request.Request(
            STOCKFISH_API_URL, headers={"User-Agent": "liora-calibrate-elo/1.0"}
        )
        with urllib.request.urlopen(req, timeout=60) as resp:
            rel = json.loads(resp.read().decode("utf-8"))
        asset_url = None
        asset_name = None
        for pattern in (
            lambda n: "linux-x86-64" in n and (n.endswith(".tar.gz") or n.endswith(".tar")),
            lambda n: f"ubuntu-x86-64-{arch_pref}" in n and (n.endswith(".tar.gz") or n.endswith(".tar")),
            lambda n: "ubuntu-x86-64" in n and (n.endswith(".tar.gz") or n.endswith(".tar")),
        ):
            for asset in rel.get("assets", []):
                name = asset.get("name", "")
                if pattern(name):
                    asset_url = asset.get("browser_download_url")
                    asset_name = name
                    break
            if asset_url:
                break
        if not asset_url or not asset_name:
            raise RuntimeError("Could not locate Linux x86-64 Stockfish release asset")
        tar_path = work_dir / asset_name
        download_file(asset_url, tar_path)

    sf_extract_dir = work_dir / "stockfish_extract"
    if sf_extract_dir.exists():
        shutil.rmtree(sf_extract_dir)
    sf_extract_dir.mkdir(parents=True, exist_ok=True)
    with tarfile.open(tar_path, "r:*") as tf:
        if sys.version_info >= (3, 12):
            tf.extractall(sf_extract_dir, filter="data")
        else:
            tf.extractall(sf_extract_dir)

    candidates = []
    for p in sf_extract_dir.rglob("*"):
        if p.is_file() and "stockfish" in p.name.lower() and not p.name.endswith(
            (".md", ".txt", ".tar", ".gz", ".nnue", ".zip", ".pdf", ".png")
        ):
            candidates.append(p)
    if not candidates:
        raise RuntimeError(f"No Stockfish binary found in {sf_extract_dir}")
    # Pick largest matching file (the compiled binary with embedded NNUE).
    chosen = max(candidates, key=lambda p: p.stat().st_size)
    shutil.copy2(chosen, sf_bin)
    sf_bin.chmod(sf_bin.stat().st_mode | 0o111)
    return sf_bin


def probe_uci_engine(engine_path: Path) -> Dict[str, object]:
    """Verify UCI handshake and parse engine name & UCI_Elo min/max if advertised."""
    proc = subprocess.run(
        [str(engine_path)],
        input="uci\nisready\nquit\n",
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=30,
        check=True,
    )
    out = proc.stdout
    if "uciok" not in out or "readyok" not in out:
        raise RuntimeError(f"UCI handshake failed for {engine_path}:\n{out[:500]}")

    info: Dict[str, object] = {
        "name": engine_path.name,
        "has_limit_strength": False,
        "elo_min": None,
        "elo_max": None,
    }
    for line in out.splitlines():
        if line.startswith("id name "):
            info["name"] = line[len("id name ") :].strip()
        if "option name UCI_LimitStrength" in line:
            info["has_limit_strength"] = True
        if "option name UCI_Elo" in line:
            m_min = re.search(r"\bmin\s+(-?\d+)", line)
            m_max = re.search(r"\bmax\s+(-?\d+)", line)
            if m_min:
                info["elo_min"] = int(m_min.group(1))
            if m_max:
                info["elo_max"] = int(m_max.group(1))
    return info


def compute_elo_stats(wins: int, losses: int, draws: int) -> Dict[str, Optional[float]]:
    """
    Compute Score, Elo difference, 95% CI margin, and Draw Rate using the
    exact formulas from `.agents/skills/cutechess-evaluator/SKILL.md` Section 7.
    """
    n = wins + losses + draws
    if n == 0:
        return {
            "games": 0,
            "points": 0.0,
            "score": None,
            "draw_rate": None,
            "delta_elo": None,
            "ci95": None,
        }

    points = wins + 0.5 * draws
    s = points / n
    draw_rate = (draws / n) * 100.0

    if s <= 0.0 or s >= 1.0:
        delta_elo = -800.0 if s <= 0.0 else 800.0
        ci95 = None
    else:
        delta_elo = -400.0 * math.log10((1.0 / s) - 1.0)
        if n > 1:
            var_s = (
                wins * ((1.0 - s) ** 2)
                + losses * ((0.0 - s) ** 2)
                + draws * ((0.5 - s) ** 2)
            ) / (n - 1)
            se_s = math.sqrt(var_s / n)
            ci95 = 1.96 * se_s * (400.0 / (math.log(10.0) * s * (1.0 - s)))
        else:
            ci95 = None

    return {
        "games": n,
        "points": points,
        "score": s,
        "draw_rate": draw_rate,
        "delta_elo": delta_elo,
        "ci95": ci95,
    }


def parse_cutechess_output(output: str, cand_name: str, opp_name: str) -> Tuple[int, int, int, Dict[str, int]]:
    pattern = re.compile(
        r"Score of\s+"
        + re.escape(cand_name)
        + r"\s+vs\s+"
        + re.escape(opp_name)
        + r":\s+(\d+)\s+-\s+(\d+)\s+-\s+(\d+)"
    )
    matches = list(pattern.finditer(output))
    if not matches:
        # Fallback: match any "Score of ...: W - L - D"
        fallback = list(re.finditer(r"Score of .+?:\s+(\d+)\s+-\s+(\d+)\s+-\s+(\d+)", output))
        if not fallback:
            raise RuntimeError(
                f"Could not parse cutechess-cli score for {cand_name} vs {opp_name}.\n"
                f"Tail of output:\n{output[-1200:]}"
            )
        m = fallback[-1]
    else:
        m = matches[-1]

    wins, losses, draws = int(m.group(1)), int(m.group(2)), int(m.group(3))
    anomalies = {
        "timeouts": len(re.findall(r"loses on time", output, re.IGNORECASE)),
        "illegal_moves": len(re.findall(r"illegal move", output, re.IGNORECASE)),
        "disconnects": len(re.findall(r"disconnect|stalls|crashed", output, re.IGNORECASE)),
    }
    return wins, losses, draws, anomalies


def run_match(
    cutechess: Path,
    book: Path,
    cand_bin: Path,
    cand_name: str,
    cand_opts: Dict[str, str],
    opp_bin: Path,
    opp_name: str,
    opp_opts: Dict[str, str],
    games: int,
    tc: str,
    timemargin: int,
    hash_mb: int,
    concurrency: int,
    pgn_path: Path,
    log_path: Path,
) -> Dict[str, object]:
    pgn_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    if pgn_path.exists():
        pgn_path.unlink()

    cmd = [
        str(cutechess),
        "-engine",
        f"name={cand_name}",
        f"cmd={cand_bin}",
        "proto=uci",
        f"option.Hash={hash_mb}",
    ]
    for k, v in cand_opts.items():
        cmd.append(f"option.{k}={v}")

    cmd.extend(
        [
            "-engine",
            f"name={opp_name}",
            f"cmd={opp_bin}",
            "proto=uci",
            f"option.Hash={hash_mb}",
        ]
    )
    for k, v in opp_opts.items():
        cmd.append(f"option.{k}={v}")

    # Crucial rule: `-games N -repeat` ONLY (never combine `-games N` with `-rounds M`).
    cmd.extend(
        [
            "-each",
            f"tc={tc}",
            f"timemargin={timemargin}",
            "-games",
            str(games),
            "-repeat",
            "-openings",
            f"file={book}",
            "format=pgn",
            "order=random",
            "-draw",
            "movenumber=40",
            "movecount=8",
            "score=15",
            "-resign",
            "movecount=6",
            "score=900",
            "twosided=true",
            "-pgnout",
            str(pgn_path),
            "-concurrency",
            str(concurrency),
        ]
    )

    log(f"Running {cand_name} vs {opp_name} ({games} games, tc={tc}, concurrency={concurrency})...")
    t0 = time.time()
    proc = run_cmd(cmd, capture=True, check=False)
    elapsed = time.time() - t0
    log_path.write_text(proc.stdout, encoding="utf-8")

    if proc.returncode != 0:
        raise RuntimeError(
            f"cutechess-cli exited with code {proc.returncode} for {cand_name} vs {opp_name}.\n"
            f"See {log_path}. Tail:\n{proc.stdout[-1200:]}"
        )

    wins, losses, draws, anomalies = parse_cutechess_output(proc.stdout, cand_name, opp_name)
    stats = compute_elo_stats(wins, losses, draws)
    return {
        "cand_name": cand_name,
        "opp_name": opp_name,
        "wins": wins,
        "losses": losses,
        "draws": draws,
        "elapsed_sec": round(elapsed, 2),
        "pgn_path": str(pgn_path),
        "log_path": str(log_path),
        "anomalies": anomalies,
        **stats,
    }


def parse_ref_engine_spec(spec: str) -> Dict[str, object]:
    """
    Parse `NAME=PATH:ELO[:OPT1=VAL1,OPT2=VAL2]`.
    Example: `v11.0_HCE=../dist/liora_v11.0_F_NNUE:2569`
             `v11.13_HCE=../dist/liora_v11.13_C_NNUE:2653:UseNNUE=false`
    """
    if "=" not in spec:
        raise ValueError(f"Invalid --ref-engine spec (missing '='): {spec}")
    name, rest = spec.split("=", 1)
    parts = rest.split(":")
    if len(parts) < 2:
        raise ValueError(f"Invalid --ref-engine spec (expected NAME=PATH:ELO[:OPTS]): {spec}")
    path = Path(parts[0])
    elo = float(parts[1])
    opts: Dict[str, str] = {}
    if len(parts) >= 3 and parts[2].strip():
        for kv in parts[2].split(","):
            if "=" not in kv:
                raise ValueError(f"Invalid option '{kv}' in --ref-engine spec: {spec}")
            k, v = kv.split("=", 1)
            opts[k.strip()] = v.strip()
    return {"name": name.strip(), "path": path, "elo": elo, "opts": opts}


def format_markdown_report(results: List[Dict[str, object]], tolerance: float) -> str:
    lines = []
    sf_rows = [r for r in results if r["category"] == "stockfish_anchor"]
    cc_rows = [r for r in results if r["category"] == "cross_check"]

    if sf_rows:
        lines.append("### Stockfish UCI_Elo Calibration Results")
        lines.append("")
        lines.append(
            "| Target Elo | Opponent | W-L-D | Score | Draw % | ΔElo ± 95% CI | Measured Elo | Error | Status |"
        )
        lines.append("| :---: | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |")
        prev_meas = None
        monotonic = True
        all_within = True
        for r in sf_rows:
            target = r["target_elo"]
            meas = r["measured_elo"]
            err = r["error_elo"]
            ci = r["ci95"]
            delta = r["delta_elo"]
            score_pct = f"{r['score'] * 100.0:.1f}%" if r["score"] is not None else "N/A"
            draw_pct = f"{r['draw_rate']:.1f}%" if r["draw_rate"] is not None else "N/A"
            ci_str = f"±{ci:.1f}" if ci is not None else "±N/A"
            delta_str = f"{delta:+.1f} {ci_str}" if delta is not None else "N/A"
            meas_str = f"{meas:.1f}" if meas is not None else "N/A"
            err_str = f"{err:+.1f}" if err is not None else "N/A"
            ok = (err is not None) and (abs(err) <= tolerance)
            if not ok:
                all_within = False
            if prev_meas is not None and meas is not None and meas <= prev_meas:
                monotonic = False
            prev_meas = meas
            status = "PASS" if ok else ("HIGH" if (err is not None and err > tolerance) else "LOW")
            wld = f"{r['wins']}-{r['losses']}-{r['draws']}"
            lines.append(
                f"| {target} | {r['opp_name']} ({r['ref_elo']:.0f}) | {wld} | "
                f"{score_pct} | {draw_pct} | {delta_str} | **{meas_str}** | {err_str} | {status} |"
            )
        lines.append("")
        lines.append(
            f"- **Within ±{tolerance:.0f} Elo:** {'YES' if all_within else 'NO'} | "
            f"**Strictly Monotonic:** {'YES' if monotonic else 'NO'}"
        )

    if cc_rows:
        lines.append("")
        lines.append("### Liora Reference Binary Cross-Check")
        lines.append("")
        lines.append(
            "| Target Elo | Reference Engine | W-L-D | Score | Draw % | ΔElo ± 95% CI | Implied Elo | Error |"
        )
        lines.append("| :---: | :--- | :---: | :---: | :---: | :---: | :---: | :---: |")
        for r in cc_rows:
            target = r["target_elo"]
            meas = r["measured_elo"]
            err = r["error_elo"]
            ci = r["ci95"]
            delta = r["delta_elo"]
            score_pct = f"{r['score'] * 100.0:.1f}%" if r["score"] is not None else "N/A"
            draw_pct = f"{r['draw_rate']:.1f}%" if r["draw_rate"] is not None else "N/A"
            ci_str = f"±{ci:.1f}" if ci is not None else "±N/A"
            delta_str = f"{delta:+.1f} {ci_str}" if delta is not None else "N/A"
            meas_str = f"{meas:.1f}" if meas is not None else "N/A"
            err_str = f"{err:+.1f}" if err is not None else "N/A"
            wld = f"{r['wins']}-{r['losses']}-{r['draws']}"
            lines.append(
                f"| {target} | {r['opp_name']} ({r['ref_elo']:.0f}) | {wld} | "
                f"{score_pct} | {draw_pct} | {delta_str} | **{meas_str}** | {err_str} |"
            )

    return "\n".join(lines)


def cmd_run(args: argparse.Namespace) -> int:
    work_dir = Path(args.work_dir).resolve()
    work_dir.mkdir(parents=True, exist_ok=True)
    stage_dir = work_dir / "bin"
    pgn_dir = Path(args.pgn_dir).resolve() if args.pgn_dir else (work_dir / "pgns")
    pgn_dir.mkdir(parents=True, exist_ok=True)

    # Resolve / bootstrap cutechess-cli and opening book
    if args.cutechess:
        cutechess = stage_executable(Path(args.cutechess), stage_dir, "cutechess-cli")
    elif args.bootstrap:
        cutechess = bootstrap_cutechess(work_dir)
    else:
        which = shutil.which("cutechess-cli")
        if not which:
            raise FileNotFoundError(
                "cutechess-cli not found in PATH. Pass --cutechess or --bootstrap."
            )
        cutechess = Path(which)

    if args.book:
        book = Path(args.book).resolve()
        if not book.is_file():
            raise FileNotFoundError(f"Opening book not found: {book}")
    elif args.bootstrap:
        book = bootstrap_book(work_dir)
    else:
        default_book = work_dir / "8moves_v3.pgn"
        if not default_book.is_file():
            raise FileNotFoundError(
                "Opening book 8moves_v3.pgn not found. Pass --book or --bootstrap."
            )
        book = default_book

    # Stage candidate Liora binary in native Linux storage
    liora_bin = stage_executable(Path(args.liora), stage_dir, "liora_candidate")
    liora_info = probe_uci_engine(liora_bin)
    log(
        f"Candidate UCI OK: {liora_info['name']} "
        f"(LimitStrength={liora_info['has_limit_strength']}, "
        f"Elo={liora_info['elo_min']}..{liora_info['elo_max']})"
    )

    # Parse anchors (supports `ELO` or `TARGET@OPP_ELO`)
    anchors: List[Tuple[int, Optional[int]]] = []
    if args.anchors.strip():
        for tok in args.anchors.split(","):
            tok = tok.strip()
            if not tok:
                continue
            if "@" in tok:
                t_s, o_s = tok.split("@", 1)
                anchors.append((int(t_s.strip()), int(o_s.strip())))
            else:
                anchors.append((int(tok), None))

    # Stage Stockfish if anchors are requested
    sf_bin: Optional[Path] = None
    sf_info: Optional[Dict[str, object]] = None
    if anchors:
        if args.stockfish:
            sf_bin = stage_executable(Path(args.stockfish), stage_dir, "stockfish_ref")
        elif args.bootstrap:
            sf_bin = bootstrap_stockfish(work_dir)
        else:
            which_sf = shutil.which("stockfish")
            if not which_sf:
                raise FileNotFoundError(
                    "stockfish not found in PATH. Pass --stockfish or --bootstrap."
                )
            sf_bin = stage_executable(Path(which_sf), stage_dir, "stockfish_ref")
        sf_info = probe_uci_engine(sf_bin)
        log(
            f"Stockfish UCI OK: {sf_info['name']} "
            f"(LimitStrength={sf_info['has_limit_strength']}, "
            f"Elo={sf_info['elo_min']}..{sf_info['elo_max']})"
        )

    # Parse & stage optional reference engines
    ref_engines: Dict[str, Dict[str, object]] = {}
    for spec in args.ref_engine or []:
        parsed = parse_ref_engine_spec(spec)
        staged = stage_executable(parsed["path"], stage_dir, f"ref_{parsed['name']}")
        info = probe_uci_engine(staged)
        parsed["staged_path"] = staged
        parsed["uci_name"] = info["name"]
        ref_engines[parsed["name"]] = parsed
        log(f"Ref engine UCI OK: {parsed['name']} -> {info['name']} (Elo={parsed['elo']:.0f})")

    # Concurrency default
    cores = os.cpu_count() or 2
    if args.concurrency:
        concurrency = args.concurrency
    else:
        concurrency = max(1, cores - 4) if cores >= 8 else max(1, cores - 1)

    log(
        f"Environment: {cores} vCPUs, concurrency={concurrency}, tc={args.tc}, "
        f"games={args.games}, hash={args.hash}MB"
    )

    results: List[Dict[str, object]] = []
    t_all = time.time()

    # 1. Stockfish anchor sweep
    if anchors and sf_bin and sf_info:
        sf_min = int(sf_info["elo_min"]) if sf_info["elo_min"] is not None else 1320
        sf_max = int(sf_info["elo_max"]) if sf_info["elo_max"] is not None else 3190

        for anchor, custom_sf_elo in anchors:
            req_sf = custom_sf_elo if custom_sf_elo is not None else anchor
            sf_elo = max(sf_min, min(sf_max, req_sf))
            cand_name = f"Liora_{anchor}"
            opp_name = f"SF_{sf_elo}"

            if anchor >= 3100:
                cand_opts = {"UCI_LimitStrength": "false"}
            else:
                cand_opts = {"UCI_LimitStrength": "true", "UCI_Elo": str(anchor)}

            opp_opts = {
                "Threads": "1",
                "UCI_LimitStrength": "true",
                "UCI_Elo": str(sf_elo),
            }

            res = run_match(
                cutechess=cutechess,
                book=book,
                cand_bin=liora_bin,
                cand_name=cand_name,
                cand_opts=cand_opts,
                opp_bin=sf_bin,
                opp_name=opp_name,
                opp_opts=opp_opts,
                games=args.games,
                tc=args.tc,
                timemargin=args.timemargin,
                hash_mb=args.hash,
                concurrency=concurrency,
                pgn_path=pgn_dir / f"sf_anchor_{anchor}_vs_{sf_elo}.pgn",
                log_path=pgn_dir / f"sf_anchor_{anchor}_vs_{sf_elo}.log",
            )
            measured = (
                (sf_elo + res["delta_elo"]) if res["delta_elo"] is not None else None
            )
            error = (measured - anchor) if measured is not None else None
            res.update(
                {
                    "category": "stockfish_anchor",
                    "target_elo": anchor,
                    "ref_elo": float(sf_elo),
                    "measured_elo": measured,
                    "error_elo": error,
                }
            )
            results.append(res)
            ci_str = f"±{res['ci95']:.1f}" if res["ci95"] is not None else "±N/A"
            log(
                f"  -> Anchor {anchor} vs SF {sf_elo}: {res['wins']}-{res['losses']}-{res['draws']} "
                f"(Δ={res['delta_elo']:+.1f} {ci_str}) => Measured={measured:.1f} "
                f"(err={error:+.1f}) [{res['elapsed_sec']}s] anomalies={res['anomalies']}"
            )

    # 2. Cross-check against Liora reference binaries
    if args.cross_check.strip():
        cc_games = args.cross_check_games if args.cross_check_games else args.games
        for item in args.cross_check.split(","):
            item = item.strip()
            if not item:
                continue
            anchor_str, ref_name = item.split(":", 1)
            anchor = int(anchor_str.strip())
            ref_name = ref_name.strip()
            if ref_name not in ref_engines:
                raise KeyError(
                    f"Cross-check references unknown engine '{ref_name}'. "
                    f"Known: {list(ref_engines.keys())}"
                )
            ref = ref_engines[ref_name]
            cand_name = f"Liora_{anchor}"
            opp_name = ref_name
            if anchor >= 3100:
                cand_opts = {"UCI_LimitStrength": "false"}
            else:
                cand_opts = {"UCI_LimitStrength": "true", "UCI_Elo": str(anchor)}

            res = run_match(
                cutechess=cutechess,
                book=book,
                cand_bin=liora_bin,
                cand_name=cand_name,
                cand_opts=cand_opts,
                opp_bin=ref["staged_path"],
                opp_name=opp_name,
                opp_opts=ref["opts"],
                games=cc_games,
                tc=args.tc,
                timemargin=args.timemargin,
                hash_mb=args.hash,
                concurrency=concurrency,
                pgn_path=pgn_dir / f"crosscheck_{anchor}_vs_{ref_name}.pgn",
                log_path=pgn_dir / f"crosscheck_{anchor}_vs_{ref_name}.log",
            )
            measured = (
                (ref["elo"] + res["delta_elo"]) if res["delta_elo"] is not None else None
            )
            error = (measured - anchor) if measured is not None else None
            res.update(
                {
                    "category": "cross_check",
                    "target_elo": anchor,
                    "ref_elo": float(ref["elo"]),
                    "measured_elo": measured,
                    "error_elo": error,
                }
            )
            results.append(res)
            ci_str = f"±{res['ci95']:.1f}" if res["ci95"] is not None else "±N/A"
            log(
                f"  -> Cross-check {anchor} vs {ref_name} ({ref['elo']:.0f}): "
                f"{res['wins']}-{res['losses']}-{res['draws']} "
                f"(Δ={res['delta_elo']:+.1f} {ci_str}) => Implied={measured:.1f} "
                f"(err={error:+.1f}) [{res['elapsed_sec']}s] anomalies={res['anomalies']}"
            )

    total_sec = round(time.time() - t_all, 2)
    report_md = format_markdown_report(results, args.tolerance)
    print("\n" + "=" * 78)
    print(report_md)
    print("=" * 78)
    log(f"Total calibration wall time: {total_sec:.1f}s")

    payload = {
        "meta": {
            "candidate": liora_info,
            "stockfish": sf_info,
            "tc": args.tc,
            "games": args.games,
            "hash_mb": args.hash,
            "concurrency": concurrency,
            "cores": cores,
            "tolerance": args.tolerance,
            "total_elapsed_sec": total_sec,
            "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        },
        "results": results,
        "report_md": report_md,
    }
    if args.json_out:
        out_path = Path(args.json_out).resolve()
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        log(f"Saved JSON report to {out_path}")

    return 0


def colab_exec_code(
    colab_bin: str,
    session: str,
    code: str,
    timeout: int = 120,
    capture: bool = False,
    check: bool = True,
) -> subprocess.CompletedProcess:
    with tempfile.NamedTemporaryFile("w", suffix=".py", delete=False, encoding="utf-8") as tf:
        tf.write(code)
        tmp_path = tf.name
    try:
        return run_cmd(
            [colab_bin, "exec", "-s", session, "--timeout", str(timeout), "-f", tmp_path],
            capture=capture,
            check=check,
        )
    finally:
        try:
            os.unlink(tmp_path)
        except OSError:
            pass


def cmd_colab(args: argparse.Namespace) -> int:
    """Orchestrate a calibration run on Google Colab using `colab` CLI."""
    colab_bin = shutil.which("colab")
    if not colab_bin:
        raise FileNotFoundError("`colab` CLI not found in PATH.")

    src_dir = Path(args.src_dir).resolve()
    nnue_path = Path(args.nnue).resolve()
    if not nnue_path.is_file():
        raise FileNotFoundError(f"NNUE file not found: {nnue_path}")

    session = args.session
    if not args.reuse:
        new_cmd = [colab_bin, "new", "-s", session]
        if args.gpu:
            new_cmd.extend(["--gpu", args.gpu])
        log(f"Provisioning Colab session '{session}' (gpu={args.gpu or 'CPU'})...")
        run_cmd(new_cmd)

    # Create compact tarball of C source files + calibrate_elo.py
    with tempfile.TemporaryDirectory() as tmpdir:
        tar_local = Path(tmpdir) / "liora_src.tar.gz"
        with tarfile.open(tar_local, "w:gz") as tf:
            for fname in SOURCE_FILES:
                fpath = src_dir / fname
                if not fpath.is_file():
                    raise FileNotFoundError(f"Missing required source file: {fpath}")
                tf.add(fpath, arcname=fname)

        log("Uploading source bundle to Colab...")
        run_cmd([colab_bin, "upload", "-s", session, str(tar_local), "/content/liora_src.tar.gz"])

        # Upload NNUE weights if not already present (always upload on fresh session)
        need_nnue = True
        if args.reuse:
            chk = colab_exec_code(
                colab_bin,
                session,
                "import os\nprint('NNUE_PRESENT' if os.path.isfile('/content/cal/liora.nnue') else 'NNUE_MISSING')\n",
                timeout=60,
                capture=True,
                check=False,
            )
            if "NNUE_PRESENT" in chk.stdout:
                need_nnue = False

        if need_nnue:
            log(f"Uploading NNUE weights ({nnue_path.name}) to Colab...")
            colab_exec_code(
                colab_bin,
                session,
                "import os\nos.makedirs('/content/cal', exist_ok=True)\n",
                timeout=60,
            )
            run_cmd([colab_bin, "upload", "-s", session, str(nnue_path), "/content/cal/liora.nnue"])

        # Upload any requested reference binaries
        remote_ref_specs: List[str] = []
        if args.upload_ref:
            colab_exec_code(
                colab_bin,
                session,
                "import os\nos.makedirs('/content/cal/refs', exist_ok=True)\n",
                timeout=60,
            )
            for spec in args.upload_ref:
                parsed = parse_ref_engine_spec(spec)
                remote_bin = f"/content/cal/refs/{parsed['name']}"
                need_upload = True
                if args.reuse:
                    chk = colab_exec_code(
                        colab_bin,
                        session,
                        f"import os\nprint('REF_OK' if os.path.isfile({remote_bin!r}) else 'REF_MISS')\n",
                        timeout=60,
                        capture=True,
                        check=False,
                    )
                    if "REF_OK" in chk.stdout:
                        need_upload = False
                if need_upload:
                    log(f"Uploading reference binary {parsed['name']} ({parsed['path']})...")
                    run_cmd([colab_bin, "upload", "-s", session, str(parsed["path"].resolve()), remote_bin])
                opts_str = ",".join(f"{k}={v}" for k, v in parsed["opts"].items())
                remote_spec = f"{parsed['name']}={remote_bin}:{parsed['elo']}"
                if opts_str:
                    remote_spec += f":{opts_str}"
                remote_ref_specs.append(remote_spec)

        # Build remote run arguments
        run_args = [
            "python3",
            "/content/cal/src_c/calibrate_elo.py",
            "run",
            "--bootstrap",
            "--work-dir",
            "/content/cal",
            "--liora",
            "/content/cal/src_c/liora",
            "--anchors",
            args.anchors,
            "--games",
            str(args.games),
            "--tc",
            args.tc,
            "--timemargin",
            str(args.timemargin),
            "--hash",
            str(args.hash),
            "--tolerance",
            str(args.tolerance),
            "--json-out",
            "/content/cal/results.json",
        ]
        if args.concurrency:
            run_args.extend(["--concurrency", str(args.concurrency)])
        if args.cross_check:
            run_args.extend(["--cross-check", args.cross_check])
        if args.cross_check_games:
            run_args.extend(["--cross-check-games", str(args.cross_check_games)])
        for rspec in remote_ref_specs:
            run_args.extend(["--ref-engine", rspec])

        runner_code = f"""
import os, subprocess, sys, tarfile

os.makedirs('/content/cal/src_c', exist_ok=True)
with tarfile.open('/content/liora_src.tar.gz', 'r:gz') as tf:
    if sys.version_info >= (3, 12):
        tf.extractall('/content/cal/src_c', filter='data')
    else:
        tf.extractall('/content/cal/src_c')

print('=== Building Liora natively on Colab ===', flush=True)
subprocess.run(['make', 'clean'], cwd='/content/cal/src_c', check=True)
build = subprocess.run(
    ['make', 'NNUE_FILE=/content/cal/liora.nnue'],
    cwd='/content/cal/src_c',
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
)
print(build.stdout, flush=True)
if build.returncode != 0:
    raise RuntimeError(f'Build failed with exit code {{build.returncode}}')

print('=== Starting calibrate_elo.py run ===', flush=True)
cmd = {run_args!r}
proc = subprocess.Popen(
    cmd,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
    bufsize=1,
)
assert proc.stdout is not None
for line in proc.stdout:
    print(line, end='', flush=True)
rc = proc.wait()
if rc != 0:
    raise RuntimeError(f'calibrate_elo.py run failed with exit code {{rc}}')
"""

        log(f"Executing calibration on Colab session '{session}' (timeout={args.timeout}s)...")
        colab_exec_code(colab_bin, session, runner_code, timeout=args.timeout)

    out_dir = Path(args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    local_json = out_dir / f"results_{time.strftime('%Y%m%d_%H%M%S')}.json"
    log(f"Downloading /content/cal/results.json -> {local_json}")
    run_cmd([colab_bin, "download", "-s", session, "/content/cal/results.json", str(local_json)])

    if args.stop:
        log(f"Stopping Colab session '{session}'...")
        run_cmd([colab_bin, "stop", "-s", session])

    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Automated Liora UCI_Elo calibration via cutechess-cli (local or Google Colab)."
    )
    sub = p.add_subparsers(dest="command", required=True)

    # `run` subcommand
    p_run = sub.add_parser("run", help="Run calibration locally (or inside a Colab VM).")
    p_run.add_argument("--liora", default="./liora", help="Path to candidate Liora binary.")
    p_run.add_argument("--cutechess", default=None, help="Path to cutechess-cli binary.")
    p_run.add_argument("--stockfish", default=None, help="Path to Stockfish binary.")
    p_run.add_argument("--book", default=None, help="Path to 8moves_v3.pgn opening book.")
    p_run.add_argument(
        "--ref-engine",
        action="append",
        default=[],
        help="Reference engine spec: NAME=PATH:ELO[:OPT1=VAL1,OPT2=VAL2] (repeatable).",
    )
    p_run.add_argument(
        "--anchors",
        default=",".join(str(a) for a in DEFAULT_ANCHORS),
        help="Comma-separated Elo anchors to test vs Stockfish (empty to skip).",
    )
    p_run.add_argument(
        "--cross-check",
        default="",
        help="Comma-separated ANCHOR:REF_NAME pairs for reference binary cross-checks.",
    )
    p_run.add_argument(
        "--cross-check-games",
        type=int,
        default=None,
        help="Optional override for game count in cross-check matches.",
    )
    p_run.add_argument("--games", type=int, default=100, help="Games per anchor match.")
    p_run.add_argument("--tc", default="5+0.05", help="Time control (e.g. 5+0.05 or 10+0.1).")
    p_run.add_argument("--timemargin", type=int, default=1000, help="Cutechess timemargin in ms.")
    p_run.add_argument("--hash", type=int, default=32, help="Hash MB per engine.")
    p_run.add_argument("--concurrency", type=int, default=None, help="Concurrent games.")
    p_run.add_argument(
        "--work-dir",
        default="/tmp/liora_elo_cal",
        help="Native Linux working directory for staging binaries & tools.",
    )
    p_run.add_argument("--pgn-dir", default=None, help="Directory to store PGNs & cutechess logs.")
    p_run.add_argument("--json-out", default=None, help="Output path for JSON results summary.")
    p_run.add_argument(
        "--bootstrap",
        action="store_true",
        help="Auto-download cutechess-cli AppImage, 8moves_v3.pgn, and Stockfish if missing.",
    )
    p_run.add_argument(
        "--tolerance",
        type=float,
        default=40.0,
        help="Target calibration tolerance in Elo (default: 40).",
    )
    p_run.set_defaults(func=cmd_run)

    # `colab` subcommand
    p_col = sub.add_parser("colab", help="Run calibration remotely on Google Colab via `colab` CLI.")
    p_col.add_argument("--session", default="liora-elo-cal", help="Colab session name.")
    p_col.add_argument("--gpu", default=None, help="Colab GPU type (e.g. G4, L4) or omit for CPU.")
    p_col.add_argument(
        "--reuse",
        action="store_true",
        help="Reuse existing running Colab session (upload updated code & recompile).",
    )
    p_col.add_argument(
        "--stop",
        action="store_true",
        help="Stop the Colab session immediately after downloading results.",
    )
    p_col.add_argument("--timeout", type=int, default=7200, help="Colab exec timeout in seconds.")
    p_col.add_argument(
        "--src-dir",
        default=str(Path(__file__).resolve().parent),
        help="Local src_c directory containing C sources and Makefile.",
    )
    p_col.add_argument(
        "--nnue",
        default=str(Path(__file__).resolve().parent / "../NNUE Weights/Gen 5/liora.nnue"),
        help="Local NNUE weights file to embed on Colab.",
    )
    p_col.add_argument(
        "--upload-ref",
        action="append",
        default=[],
        help="Reference binary to upload: NAME=LOCAL_PATH:ELO[:OPT1=VAL1,...] (repeatable).",
    )
    p_col.add_argument(
        "--out-dir",
        default="/tmp/liora_cal_results",
        help="Local directory for downloaded JSON results.",
    )
    p_col.add_argument(
        "--anchors",
        default=",".join(str(a) for a in DEFAULT_ANCHORS),
        help="Comma-separated Elo anchors to test vs Stockfish.",
    )
    p_col.add_argument("--cross-check", default="", help="Comma-separated ANCHOR:REF_NAME pairs.")
    p_col.add_argument("--cross-check-games", type=int, default=None)
    p_col.add_argument("--games", type=int, default=100)
    p_col.add_argument("--tc", default="5+0.05")
    p_col.add_argument("--timemargin", type=int, default=1000)
    p_col.add_argument("--hash", type=int, default=32)
    p_col.add_argument("--concurrency", type=int, default=None)
    p_col.add_argument("--tolerance", type=float, default=40.0)
    p_col.set_defaults(func=cmd_colab)

    return p


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
