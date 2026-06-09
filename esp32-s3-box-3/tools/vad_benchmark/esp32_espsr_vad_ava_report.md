# ESP32 ESP-SR VAD AVA-Speech 测试报告

测试日期：2026-06-08

## 测试对象

- 设备：ESP32-S3，串口 `COM22`
- 固件：Pipecat ESP32 VAD 串口测试固件
- VAD 实现：Espressif ESP-SR `esp_vad.h`
- VAD 模式：`VAD_MODE_0`
- 音频格式：16 kHz，单声道，signed 16-bit PCM
- 帧长：20 ms，每帧 320 个采样点
- 串口波特率：921600

## 数据集

- 数据集：AVA-Speech
- 数据来源：Hugging Face 镜像压缩包
- 输入文件：`data/ava_speech/hf_mirror/dataset.zip`
- 抽样方式：平衡抽样，每类 25 个 WAV 文件
- 测试文件数：100
- 测试帧数：15675

标签映射规则：

| AVA-Speech 标签 | VAD 真值 |
| --- | --- |
| `CLEAN_SPEECH` | 有人声 |
| `SPEECH_WITH_MUSIC` | 有人声 |
| `SPEECH_WITH_NOISE` | 有人声 |
| `NO_SPEECH` | 无人声 |

## 测试命令

```powershell
python tools\vad_benchmark\esp32_vad_serial.py `
  --port COM22 `
  --baud 921600 `
  --zip data\ava_speech\hf_mirror\dataset.zip `
  --out data\ava_speech\work\esp32_espsr_vad_balanced_25x4.csv `
  --limit-per-class 25 `
  --wait-ready `
  --ready-timeout 30 `
  --progress 10
```

## 总体结果

| 指标 | 数值 |
| --- | ---: |
| 测试文件数 | 100 |
| 测试帧数 | 15675 |
| TP，正确检测为有人声 | 12655 |
| FP，误检测为有人声 | 1779 |
| FN，漏检为无人声 | 666 |
| TN，正确检测为无人声 | 575 |
| Precision，精确率 | 0.8767 |
| Recall，召回率 | 0.9500 |
| F1 | 0.9119 |
| P95 推理耗时 | 206 us |

## 分类结果

| 类别 | 帧数 | 预测有人声 | 预测无人声 | 指标 | P95 推理耗时 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `CLEAN_SPEECH` | 3404 | 3156 | 248 | 召回率 0.9271 | 203 us |
| `SPEECH_WITH_MUSIC` | 2261 | 2133 | 128 | 召回率 0.9434 | 204 us |
| `SPEECH_WITH_NOISE` | 7656 | 7366 | 290 | 召回率 0.9621 | 208 us |
| `NO_SPEECH` | 2354 | 1779 | 575 | 误触发率 0.7557 | 206 us |

## 输出文件

- 预测结果 CSV：`data/ava_speech/work/esp32_espsr_vad_balanced_25x4.csv`
- 运行日志：`data/ava_speech/work/esp32_espsr_vad_balanced_25x4.log`

## 结论

ESP32 上的 ESP-SR VAD 推理速度很快，P95 推理耗时约为 206 us，远低于 20 ms 的单帧时间预算，可以满足实时处理要求。

在有人声类别上，召回率整体较高：

- 干净语音召回率：0.9271
- 带音乐语音召回率：0.9434
- 带噪声语音召回率：0.9621

当前主要问题是 `NO_SPEECH` 类别误触发偏高，误触发率为 0.7557。也就是说，在这组 AVA-Speech 样本里，很多无人声片段会被 `VAD_MODE_0` 判断成有人声。这个模式比较宽松，适合减少漏检，但容易增加误唤醒或误上传。

## 后续优化建议

1. 继续测试 `VAD_MODE_1`、`VAD_MODE_2`、`VAD_MODE_3`，对比误触发率和召回率。
2. 增加连续帧判定，例如连续 3 到 5 帧为 speech 才认为开始说话。
3. 增加结束判定，例如连续 10 到 20 帧 silence 才认为说话结束。
4. 如果要完全贴近小智效果，可以继续移植小智完整 AFE 链路，加入降噪、AEC 和 AFE 内部 VAD 状态。
