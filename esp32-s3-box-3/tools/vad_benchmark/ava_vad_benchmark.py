#!/usr/bin/env python3
"""AVA-Speech VAD benchmark helper.

This script keeps the benchmark format simple:

truth_frames.csv:
  video_id,frame_id,start_s,end_s,label,is_speech

predictions.csv:
  video_id,frame_id,pred,inference_us

Labels are converted to fixed 20 ms frames by majority overlap. The AVA-Speech
speech classes CLEAN_SPEECH, SPEECH_WITH_MUSIC, and SPEECH_WITH_NOISE are
treated as speech=1. NO_SPEECH is speech=0.
"""

from __future__ import annotations

import argparse
import csv
import math
import pathlib
import statistics
import subprocess
import sys
import wave
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Tuple


SPEECH_LABELS = {"CLEAN_SPEECH", "SPEECH_WITH_MUSIC", "SPEECH_WITH_NOISE"}
ALL_LABELS = {"NO_SPEECH", *SPEECH_LABELS}


@dataclass
class Segment:
    video_id: str
    start_s: float
    end_s: float
    label: str


@dataclass
class FrameTruth:
    video_id: str
    frame_id: int
    start_s: float
    end_s: float
    label: str
    is_speech: int


def read_ava_labels(path: pathlib.Path) -> Dict[str, List[Segment]]:
    by_video: Dict[str, List[Segment]] = {}
    with path.open("r", newline="", encoding="utf-8-sig") as f:
        reader = csv.reader(f)
        for row in reader:
            if not row or row[0].startswith("#"):
                continue
            if len(row) < 4:
                raise ValueError(f"Bad AVA label row: {row}")
            video_id, start_s, end_s, label = row[:4]
            label = label.strip()
            if label not in ALL_LABELS:
                raise ValueError(f"Unknown AVA label {label!r} in row {row}")
            by_video.setdefault(video_id, []).append(
                Segment(video_id, float(start_s), float(end_s), label)
            )

    for segments in by_video.values():
        segments.sort(key=lambda s: (s.start_s, s.end_s))
    return by_video


def label_for_frame(segments: List[Segment], start_s: float, end_s: float) -> str:
    best_label = "NO_SPEECH"
    best_overlap = 0.0
    for seg in segments:
        if seg.end_s <= start_s:
            continue
        if seg.start_s >= end_s:
            break
        overlap = min(seg.end_s, end_s) - max(seg.start_s, start_s)
        if overlap > best_overlap:
            best_overlap = overlap
            best_label = seg.label
    return best_label


def make_truth_frames(
    labels: Dict[str, List[Segment]],
    video_ids: Iterable[str],
    frame_ms: int,
    clip_start_s: float,
    clip_duration_s: float,
) -> List[FrameTruth]:
    frame_s = frame_ms / 1000.0
    frame_count = int(math.ceil(clip_duration_s / frame_s))
    frames: List[FrameTruth] = []
    for video_id in video_ids:
        segments = labels.get(video_id, [])
        for frame_id in range(frame_count):
            start_s = clip_start_s + frame_id * frame_s
            end_s = min(clip_start_s + clip_duration_s, start_s + frame_s)
            label = label_for_frame(segments, start_s, end_s)
            frames.append(
                FrameTruth(
                    video_id=video_id,
                    frame_id=frame_id,
                    start_s=start_s,
                    end_s=end_s,
                    label=label,
                    is_speech=1 if label in SPEECH_LABELS else 0,
                )
            )
    return frames


def write_truth(path: pathlib.Path, frames: List[FrameTruth]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["video_id", "frame_id", "start_s", "end_s", "label", "is_speech"])
        for fr in frames:
            writer.writerow([fr.video_id, fr.frame_id, f"{fr.start_s:.3f}", f"{fr.end_s:.3f}", fr.label, fr.is_speech])


def read_truth(path: pathlib.Path) -> Dict[Tuple[str, int], FrameTruth]:
    truth: Dict[Tuple[str, int], FrameTruth] = {}
    with path.open("r", newline="", encoding="utf-8-sig") as f:
        for row in csv.DictReader(f):
            fr = FrameTruth(
                video_id=row["video_id"],
                frame_id=int(row["frame_id"]),
                start_s=float(row["start_s"]),
                end_s=float(row["end_s"]),
                label=row["label"],
                is_speech=int(row["is_speech"]),
            )
            truth[(fr.video_id, fr.frame_id)] = fr
    return truth


def read_predictions(path: pathlib.Path) -> Dict[Tuple[str, int], Tuple[int, Optional[float]]]:
    predictions: Dict[Tuple[str, int], Tuple[int, Optional[float]]] = {}
    with path.open("r", newline="", encoding="utf-8-sig") as f:
        for row in csv.DictReader(f):
            inference = row.get("inference_us", "")
            predictions[(row["video_id"], int(row["frame_id"]))] = (
                int(row["pred"]),
                float(inference) if inference not in ("", None) else None,
            )
    return predictions


def safe_div(num: float, den: float) -> float:
    return num / den if den else 0.0


def evaluate(truth_path: pathlib.Path, pred_path: pathlib.Path) -> Dict[str, float]:
    truth = read_truth(truth_path)
    preds = read_predictions(pred_path)

    tp = fp = fn = tn = 0
    per_label = {label: {"tp": 0, "fn": 0} for label in SPEECH_LABELS}
    inference_us: List[float] = []
    missing = 0

    for key, fr in truth.items():
        pred_entry = preds.get(key)
        if pred_entry is None:
            pred = 0
            missing += 1
            infer = None
        else:
            pred, infer = pred_entry
        if infer is not None:
            inference_us.append(infer)

        if fr.is_speech and pred:
            tp += 1
            per_label[fr.label]["tp"] += 1
        elif fr.is_speech and not pred:
            fn += 1
            per_label[fr.label]["fn"] += 1
        elif not fr.is_speech and pred:
            fp += 1
        else:
            tn += 1

    precision = safe_div(tp, tp + fp)
    recall = safe_div(tp, tp + fn)
    f1 = safe_div(2 * precision * recall, precision + recall)
    result = {
        "frames": len(truth),
        "missing_predictions": missing,
        "tp": tp,
        "fp": fp,
        "fn": fn,
        "tn": tn,
        "precision": precision,
        "recall": recall,
        "f1": f1,
        "p95_inference_us": percentile(inference_us, 95),
    }
    for label, counts in per_label.items():
        name = label.lower().replace("speech_with_", "").replace("clean_speech", "clean")
        result[f"{name}_recall"] = safe_div(counts["tp"], counts["tp"] + counts["fn"])
    return result


def percentile(values: List[float], q: float) -> float:
    if not values:
        return 0.0
    values = sorted(values)
    idx = int(math.ceil(q / 100.0 * len(values))) - 1
    return values[max(0, min(idx, len(values) - 1))]


def print_result(result: Dict[str, float]) -> None:
    for key in [
        "frames",
        "missing_predictions",
        "tp",
        "fp",
        "fn",
        "tn",
        "precision",
        "recall",
        "f1",
        "clean_recall",
        "music_recall",
        "noise_recall",
        "p95_inference_us",
    ]:
        value = result.get(key, 0)
        if isinstance(value, float):
            print(f"{key}: {value:.4f}")
        else:
            print(f"{key}: {value}")


def run_ffmpeg_to_wav(input_path: pathlib.Path, output_path: pathlib.Path, clip_start_s: float, duration_s: float) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        "ffmpeg",
        "-y",
        "-ss",
        str(clip_start_s),
        "-i",
        str(input_path),
        "-t",
        str(duration_s),
        "-ac",
        "1",
        "-ar",
        "16000",
        "-sample_fmt",
        "s16",
        str(output_path),
    ]
    subprocess.run(cmd, check=True)


def discover_video_ids(audio_dir: pathlib.Path) -> List[str]:
    ids = []
    for path in sorted(audio_dir.glob("*.wav")):
        ids.append(path.stem)
    return ids


def run_webrtc_vad(audio_dir: pathlib.Path, video_ids: List[str], out_csv: pathlib.Path, frame_ms: int, aggressiveness: int) -> None:
    try:
        import webrtcvad  # type: ignore
    except ImportError as exc:
        raise SystemExit("Install py-webrtcvad first: python -m pip install webrtcvad") from exc

    vad = webrtcvad.Vad(aggressiveness)
    out_csv.parent.mkdir(parents=True, exist_ok=True)
    with out_csv.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["video_id", "frame_id", "pred", "inference_us"])
        for video_id in video_ids:
            wav_path = audio_dir / f"{video_id}.wav"
            with wave.open(str(wav_path), "rb") as wav:
                if wav.getnchannels() != 1 or wav.getframerate() != 16000 or wav.getsampwidth() != 2:
                    raise ValueError(f"{wav_path} must be 16 kHz mono 16-bit PCM")
                bytes_per_frame = int(16000 * frame_ms / 1000) * 2
                frame_id = 0
                while True:
                    pcm = wav.readframes(bytes_per_frame // 2)
                    if len(pcm) < bytes_per_frame:
                        break
                    pred = 1 if vad.is_speech(pcm, 16000) else 0
                    writer.writerow([video_id, frame_id, pred, ""])
                    frame_id += 1


def cmd_prepare(args: argparse.Namespace) -> None:
    labels = read_ava_labels(args.labels)
    if args.video_ids:
        video_ids = [v.strip() for v in args.video_ids.split(",") if v.strip()]
    else:
        video_ids = discover_video_ids(args.audio_dir)
    if args.limit:
        video_ids = video_ids[: args.limit]
    if not video_ids:
        raise SystemExit("No video ids found. Put {video_id}.wav files in --audio-dir or pass --video-ids.")
    frames = make_truth_frames(labels, video_ids, args.frame_ms, args.clip_start_s, args.clip_duration_s)
    write_truth(args.out, frames)
    print(f"Wrote {len(frames)} truth frames for {len(video_ids)} videos to {args.out}")


def cmd_webrtc(args: argparse.Namespace) -> None:
    video_ids = [v.strip() for v in args.video_ids.split(",") if v.strip()] if args.video_ids else discover_video_ids(args.audio_dir)
    if args.limit:
        video_ids = video_ids[: args.limit]
    run_webrtc_vad(args.audio_dir, video_ids, args.out, args.frame_ms, args.aggressiveness)
    print(f"Wrote WebRTC predictions for {len(video_ids)} videos to {args.out}")


def cmd_eval(args: argparse.Namespace) -> None:
    print_result(evaluate(args.truth, args.pred))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(required=True)

    prepare = sub.add_parser("prepare", help="Convert AVA-Speech interval labels to 20 ms frame truth.")
    prepare.add_argument("--labels", type=pathlib.Path, required=True)
    prepare.add_argument("--audio-dir", type=pathlib.Path, required=True)
    prepare.add_argument("--out", type=pathlib.Path, required=True)
    prepare.add_argument("--video-ids", default="", help="Comma-separated video ids. Defaults to *.wav stems in --audio-dir.")
    prepare.add_argument("--limit", type=int, default=0)
    prepare.add_argument("--frame-ms", type=int, default=20)
    prepare.add_argument("--clip-start-s", type=float, default=900.0)
    prepare.add_argument("--clip-duration-s", type=float, default=900.0)
    prepare.set_defaults(func=cmd_prepare)

    webrtc = sub.add_parser("webrtc", help="Run py-webrtcvad baseline on prepared WAV files.")
    webrtc.add_argument("--audio-dir", type=pathlib.Path, required=True)
    webrtc.add_argument("--out", type=pathlib.Path, required=True)
    webrtc.add_argument("--video-ids", default="")
    webrtc.add_argument("--limit", type=int, default=0)
    webrtc.add_argument("--frame-ms", type=int, default=20, choices=[10, 20, 30])
    webrtc.add_argument("--aggressiveness", type=int, default=2, choices=[0, 1, 2, 3])
    webrtc.set_defaults(func=cmd_webrtc)

    evaluate_parser = sub.add_parser("eval", help="Evaluate predictions against truth_frames.csv.")
    evaluate_parser.add_argument("--truth", type=pathlib.Path, required=True)
    evaluate_parser.add_argument("--pred", type=pathlib.Path, required=True)
    evaluate_parser.set_defaults(func=cmd_eval)
    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
