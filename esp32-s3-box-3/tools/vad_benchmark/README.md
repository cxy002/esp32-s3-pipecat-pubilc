# AVA-Speech VAD Benchmark

This folder follows `vad 参考.pdf`:

1. Convert AVA-Speech audio to 16 kHz mono 16-bit PCM.
2. Convert AVA labels to 20 ms frame labels.
3. Run VAD on every frame.
4. Compute Precision, Recall, F1, per-condition Recall, and P95 inference time.
5. Compare ESP32/Vocat with WebRTC and, later, Silero.

## Dataset Notes

AVA-Speech labels have 4 classes:

- `NO_SPEECH`
- `CLEAN_SPEECH`
- `SPEECH_WITH_MUSIC`
- `SPEECH_WITH_NOISE`

For this VAD benchmark:

- `CLEAN_SPEECH`, `SPEECH_WITH_MUSIC`, `SPEECH_WITH_NOISE` -> `speech=1`
- `NO_SPEECH` -> `speech=0`

AVA labels are absolute timestamps from the original YouTube video. The labeled
region is usually 15:00 to 30:00, so the default benchmark clip start is `900s`
and duration is `900s`.

## Prepare Files

Recommended layout:

```text
data/ava_speech/
  ava_speech_labels_v1.csv
  audio16k/
    VIDEO_ID_1.wav
    VIDEO_ID_2.wav
  work/
```

Each WAV must be:

- 16 kHz
- mono
- signed 16-bit PCM
- starts at the AVA labeled clip start, usually 900 seconds

If you have a source video, convert it like this:

```powershell
ffmpeg -y -ss 900 -i .\raw_videos\VIDEO_ID.mp4 -t 900 -ac 1 -ar 16000 -sample_fmt s16 .\audio16k\VIDEO_ID.wav
```

## Build Frame Labels

```powershell
python .\tools\vad_benchmark\ava_vad_benchmark.py prepare `
  --labels .\data\ava_speech\ava_speech_labels_v1.csv `
  --audio-dir .\data\ava_speech\audio16k `
  --out .\data\ava_speech\work\truth_frames.csv `
  --limit 3
```

Remove `--limit 3` after the flow works.

Output:

```csv
video_id,frame_id,start_s,end_s,label,is_speech
abc123,0,900.000,900.020,NO_SPEECH,0
abc123,1,900.020,900.040,CLEAN_SPEECH,1
```

## Run WebRTC Baseline

Install dependency:

```powershell
python -m pip install webrtcvad
```

Run:

```powershell
python .\tools\vad_benchmark\ava_vad_benchmark.py webrtc `
  --audio-dir .\data\ava_speech\audio16k `
  --out .\data\ava_speech\work\webrtc_pred.csv `
  --aggressiveness 2 `
  --limit 3
```

Evaluate:

```powershell
python .\tools\vad_benchmark\ava_vad_benchmark.py eval `
  --truth .\data\ava_speech\work\truth_frames.csv `
  --pred .\data\ava_speech\work\webrtc_pred.csv
```

## ESP32/Vocat Prediction Format

When ESP32 VAD is wired in, make the PC capture/save one row per frame:

```csv
video_id,frame_id,pred,inference_us
abc123,0,0,820
abc123,1,1,790
```

Then evaluate:

```powershell
python .\tools\vad_benchmark\ava_vad_benchmark.py eval `
  --truth .\data\ava_speech\work\truth_frames.csv `
  --pred .\data\ava_speech\work\vocat_esp32_pred.csv
```

## Run ESP32 Serial VAD Test

The current ESP32 test firmware enables `ENABLE_VAD_SERIAL_TEST` in
`src/board_config.h`. In this mode the app does not start 4G; it waits for
20 ms PCM frames on UART0 at 921600 baud and returns one VAD decision per
frame.

Install the host dependency:

```powershell
python -m pip install pyserial
```

Flash the firmware, close `idf_monitor`, then run a small AVA-Speech segment
test:

```powershell
python .\tools\vad_benchmark\esp32_vad_serial.py `
  --port COM22 `
  --zip .\data\ava_speech\hf_mirror\dataset.zip `
  --out .\data\ava_speech\work\esp32_vad_pred.csv `
  --limit-files 100
```

Start with `--limit-files 100`. Full AVA over serial is much slower because
every 20 ms PCM frame is transferred to the ESP32.

## First Acceptance Target

Use these first-pass checks:

- ESP32 P95 inference time is below 20 ms.
- ESP32 F1 is not obviously worse than WebRTC.
- Clean / Noise / Music recall is reported separately.
- Real desktop robot tests are still needed after AVA-Speech:
  - 1 m front speech
  - 3 m front speech
  - keyboard/table noise
  - background music
  - robot TTS playback
  - user barge-in during TTS
