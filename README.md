# Aurora

**位完美（bit-perfect）HiFi 音乐播放器 · CardputerZero**

Aurora 把 CardputerZero 变成口袋 HiFi 播放器。音频直接经 ALSA 送进 ES8389 DAC，
绕过系统混音器——I2S 码流与源文件逐位相同。

## 特性

- **位完美直通**：解码输出格式/采样率与硬件一致时，SoC→DAC 码流无任何处理
- **实时格式徽章**：每首曲目显示 codec / 位深 / 采样率 / 声道数，并如实标注
  `★ 位完美直通` / `重采样 → 96kHz` / `有损源`
- **硬件音量**：音量走 ES8389 DAC 音量寄存器（0dB 封顶防削波），不触碰数字码流
- **曲库扫描**：~/Music 与 ~/Downloads，支持 FLAC / MP3 / WAV / OGG / AIFF
- **键盘优先 UI**：320×170 屏，↑↓ 选择，Enter 播放，4-8 底部功能键，长按 ESC 退出
- **明暗主题**，跨会话持久化

## 采样率支持（真机实测）

| 源采样率 | 输出 |
|---|---|
| 44.1 / 48 / 88.2 / 96 kHz | 位完美直通 |
| 176.4 / 192 kHz | 重采样到 96kHz（板级时钟上限，UI 如实标注） |

## 构建

```bash
# 交叉编译 + 打包 .deb（WSL）
cmake --workflow --preset cp0-cross-package
```

产物：`dist/aurora_0.1.0-rev1_arm64.deb`

## 按键

| 键 | 曲库页 | 播放页 |
|---|---|---|
| ↑ ↓ | 选择曲目 | 音量 ±5% |
| Enter | 播放选中 | 播放 / 暂停 |
| ← → | — | 快退 / 快进 10s |
| ESC | 提示长按退出 | 返回曲库 |
| 长按 ESC | 退出 | 退出 |
| 4-8 | 曲库 / 播放 / 上曲 / 下曲 / 主题 | 同左 |
| fn+H | 帮助 | 帮助 |
| fn+J | 截屏 | 截屏 |

## 自测

```bash
AURORA_TEST_KEYS="down,enter" ./aurora   # 注入按键序列
```

## 许可

MIT
