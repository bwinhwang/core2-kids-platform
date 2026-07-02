# audio_fx —— 幼儿应用音效引擎(NS4168 / esp_codec_dev)

程序化合成(正弦+包络)、事件驱动队列、**不阻塞调用方**;不依赖外部 PCM 资源。
内置一套跨应用的"反馈音效词汇表"(与 `haptics`/`ledstrip_fx` 词汇一一对应):
`SND_HELLO / SND_BUMP_LIGHT|MED|HARD / SND_NEAR / SND_COLLECT / SND_WIN`。

## 防爆音纪律(实机验证,勿破坏)

1. **codec 整局保持 open**——不要每个音效 close+open、不要反复 toggle SPK_EN(会咔哒);
2. 每段首尾 **6ms 淡入淡出**;
3. **只用一种采样率**(16kHz/mono/16bit),永不 reopen。

## 用法

```c
audio_fx_init();                 // 开机一次(core2_board_init 已代管)
audio_fx_play(SND_WIN);          // 内置音效,非阻塞(队列满丢弃)
audio_fx_set_volume(60);         // 0~100;幼儿应用在应用层限上限(≲75dBA @25cm 实测)

// 新应用自定义音序(无需改本组件;freq_hz=0 为停顿,总长 >~400ms 截断)
audio_fx_play_notes((audio_note_t[]){ {440,100,50}, {0,40,0}, {880,150,60} }, 3);
```

## 相关板级事实

NS4168 走 I2S(BCLK=12/LRCK=0/DATA=2),SPK_EN=AXP192 IO2,由 BSP 管;
**NS4168 监听右声道**、I2S mono 默认左槽会没声——BSP 已处理,绕开 BSP 自写 I2S 时
见 `docs/platform/Core2_v1_0.md` §3 踩坑记录。
