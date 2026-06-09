#!/usr/bin/env python3
"""Feed AVA-Speech WAV frames to the ESP32 VAD serial test firmware."""

from __future__ import annotations

import argparse
import csv
import io
import struct
import time
import wave
import zipfile
from pathlib import Path
from typing import Dict, Iterable, List, Tuple


SPEECH_LABELS = {"CLEAN_SPEECH", "SPEECH_WITH_MUSIC", "SPEECH_WITH_NOISE"}
FRAME_MS = 20
SAMPLE_RATE = 16000
FRAME_SAMPLES = SAMPLE_RATE * FRAME_MS // 1000
FRAME_BYTES = FRAME_SAMPLES * 2
MAGIC = b"VADF"


def iter_zip_wavs(zip_path: Path, limit_files: int = 0, limit_per_class: int = 0) -> Iterable[Tuple[str, str, bytes]]:
    with zipfile.ZipFile(zip_path) as z:
        names = [name for name in z.namelist() if name.endswith(".wav")]
        names.sort()
        if limit_per_class:
            picked: List[str] = []
            per_class: Dict[str, int] = {}
            for name in names:
                parts = name.split("/")
                if len(parts) < 3:
                    continue
                label = parts[1]
                count = per_class.get(label, 0)
                if count >= limit_per_class:
                    continue
                picked.append(name)
                per_class[label] = count + 1
            names = picked
        if limit_files:
            names = names[:limit_files]
        for name in names:
            parts = name.split("/")
            if len(parts) < 3:
                continue
            label = parts[1]
            sample_id = Path(name).stem
            yield sample_id, label, z.read(name)


def read_wav_pcm(wav_bytes: bytes) -> bytes:
    with wave.open(io.BytesIO(wav_bytes), "rb") as wav:
        if wav.getnchannels() != 1 or wav.getframerate() != SAMPLE_RATE or wav.getsampwidth() != 2:
            raise ValueError(
                f"Need 16 kHz mono s16 WAV, got channels={wav.getnchannels()} "
                f"rate={wav.getframerate()} width={wav.getsampwidth()}"
            )
        return wav.readframes(wav.getnframes())


def safe_div(num: float, den: float) -> float:
    return num / den if den else 0.0


def p95(values: List[float]) -> float:
    if not values:
        return 0.0
    values = sorted(values)
    idx = int(0.95 * len(values) + 0.999999) - 1
    return values[max(0, min(idx, len(values) - 1))]


def wait_ready(ser, timeout_s: float) -> None:
    deadline = time.time() + timeout_s
    buf = b""
    while time.time() < deadline:
        chunk = ser.read(1)
        if chunk:
            buf += chunk
            if b"VAD_READY" in buf:
                return
            if len(buf) > 256:
                buf = buf[-256:]
    raise TimeoutError("ESP32 did not print VAD_READY. Check firmware mode, port, and baud rate.")


def read_vadr_line(ser, frame_id: int) -> str:
    deadline = time.time() + ser.timeout
    last_line = ""
    while time.time() < deadline:
        line = ser.readline().decode("ascii", errors="replace").strip()
        if not line:
            continue
        last_line = line
        if line.startswith("VADR,"):
            parts = line.split(",", 3)
            if len(parts) >= 2 and int(parts[1]) == frame_id:
                return line
    raise RuntimeError(f"Bad ESP32 response for frame {frame_id}: {last_line!r}")


def run(args: argparse.Namespace) -> None:
    try:
        import serial  # type: ignore
    except ImportError as exc:
        raise SystemExit("Install pyserial first: python -m pip install pyserial") from exc

    args.out.parent.mkdir(parents=True, exist_ok=True)

    counts = {"tp": 0, "fp": 0, "fn": 0, "tn": 0}
    per_label: Dict[str, Dict[str, int]] = {
        "CLEAN_SPEECH": {"tp": 0, "fn": 0, "frames": 0},
        "SPEECH_WITH_MUSIC": {"tp": 0, "fn": 0, "frames": 0},
        "SPEECH_WITH_NOISE": {"tp": 0, "fn": 0, "frames": 0},
        "NO_SPEECH": {"tn": 0, "fp": 0, "frames": 0},
    }
    inference_us: List[float] = []
    frames = 0
    files = 0

    with serial.Serial(args.port, args.baud, timeout=args.timeout) as ser, args.out.open(
        "w", newline="", encoding="utf-8"
    ) as f:
        ser.reset_input_buffer()
        time.sleep(0.5)
        ser.reset_input_buffer()
        if args.wait_ready:
            wait_ready(ser, args.ready_timeout)

        writer = csv.writer(f)
        writer.writerow(["sample_id", "label", "frame_id", "truth", "pred", "inference_us", "rms", "zcr", "peak"])

        global_frame_id = 0
        for sample_id, label, wav_bytes in iter_zip_wavs(args.zip, args.limit_files, args.limit_per_class):
            truth = 1 if label in SPEECH_LABELS else 0
            pcm = read_wav_pcm(wav_bytes)
            frame_count = len(pcm) // FRAME_BYTES
            files += 1

            for local_frame_id in range(frame_count):
                frame = pcm[local_frame_id * FRAME_BYTES : (local_frame_id + 1) * FRAME_BYTES]
                packet = MAGIC + struct.pack("<I", global_frame_id) + frame
                ser.write(packet)
                line = read_vadr_line(ser, global_frame_id)

                _, frame_id_s, pred_s, infer_s, rms_s, zcr_s, peak_s = line.split(",")
                frame_id = int(frame_id_s)
                if frame_id != global_frame_id:
                    raise RuntimeError(f"Frame id mismatch: sent {global_frame_id}, got {frame_id}")

                pred = int(pred_s)
                infer = float(infer_s)
                rms = int(rms_s)
                zcr = int(zcr_s)
                peak = int(peak_s)
                inference_us.append(infer)
                frames += 1

                writer.writerow([sample_id, label, local_frame_id, truth, pred, infer, rms, zcr, peak])

                if truth and pred:
                    counts["tp"] += 1
                    per_label[label]["tp"] += 1
                elif truth and not pred:
                    counts["fn"] += 1
                    per_label[label]["fn"] += 1
                elif not truth and pred:
                    counts["fp"] += 1
                    per_label[label]["fp"] += 1
                else:
                    counts["tn"] += 1
                    per_label[label]["tn"] += 1
                per_label[label]["frames"] += 1
                global_frame_id += 1

            if args.progress and files % args.progress == 0:
                print(f"processed_files: {files} frames: {frames}")

    precision = safe_div(counts["tp"], counts["tp"] + counts["fp"])
    recall = safe_div(counts["tp"], counts["tp"] + counts["fn"])
    f1 = safe_div(2 * precision * recall, precision + recall)

    print(f"files: {files}")
    print(f"frames: {frames}")
    for key in ["tp", "fp", "fn", "tn"]:
        print(f"{key}: {counts[key]}")
    print(f"precision: {precision:.4f}")
    print(f"recall: {recall:.4f}")
    print(f"f1: {f1:.4f}")
    print(f"p95_inference_us: {p95(inference_us):.4f}")
    for label in ["CLEAN_SPEECH", "SPEECH_WITH_MUSIC", "SPEECH_WITH_NOISE"]:
        c = per_label[label]
        print(f"{label}_recall: {safe_div(c['tp'], c['tp'] + c['fn']):.4f} frames={c['frames']}")
    c = per_label["NO_SPEECH"]
    print(f"NO_SPEECH_false_positive_rate: {safe_div(c['fp'], c['fp'] + c['tn']):.4f} frames={c['frames']}")
    print(f"pred_csv: {args.out}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM22")
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--zip", type=Path, default=Path("data/ava_speech/hf_mirror/dataset.zip"))
    parser.add_argument("--out", type=Path, default=Path("data/ava_speech/work/esp32_vad_pred.csv"))
    parser.add_argument("--limit-files", type=int, default=100)
    parser.add_argument("--limit-per-class", type=int, default=0)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--wait-ready", action="store_true")
    parser.add_argument("--ready-timeout", type=float, default=20.0)
    parser.add_argument("--progress", type=int, default=25)
    run(parser.parse_args())


if __name__ == "__main__":
    main()
