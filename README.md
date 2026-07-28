# 簡易キーチェンジャー
再生中の音声の音程を、リアルタイムで上下させるための DirectShow フィルター

詳細についてはドキュメントを参照。

- ヘルプ：EasyKeyChanger_JPN.html
- ソースコードについて：EasyKeyChanger_Src_ReadMe_JPN.txt

## 入力音声形式

リニア PCM（16 / 24 / 32 bit 整数、32 bit 浮動小数点）、1 〜 8 チャンネル、
サンプリングレート 4 kHz 〜 384 kHz に対応。
リニア PCM 以外（圧縮音声など）は受け付けないため、音声デコーダーの後ろに組み込まれる。

## MPC-BE での設定

- オプション → 外部フィルターで本フィルターを追加し、「優先する」にチェックを入れる。
- **併せて ffdshow Audio Processor も外部フィルターに追加し、「優先する」にする。**
  理由は下記「MPC-BE のオーディオスイッチャーについて」を参照。
- 音声デコーダーは MPC-BE 内蔵のものでも LAV Audio Decoder でもよい。
  LAV Audio Decoder の出力設定も既定のままでよい。
- 外部フィルターの並び順は結果に影響しない。
- 音声トラックが複数ある動画では、トラックごとに本フィルターが組み込まれるが、
  キーなどの設定は全体で共有される。

### MPC-BE のオーディオスイッチャーについて

MPC-BE のオーディオスイッチャーは、出力先として接続できるフィルターを
**CLSID のホワイトリストで限定している**（`CStreamSwitcherOutputPin::CheckConnect`、
src/filters/switcher/AudioSwitcher/StreamSwitcher.cpp）。
音声レンダラーと、以下のフィルターだけが接続を許可される。

- Infinite Pin Tee Filter（CLSID_InfTee、Windows 標準）
- XySubFilter AutoLoader
- ffdshow Audio Processor
- AC3Filter
- DC-DSP Filter
- CyberLink TimeStretch Filter
- Acon Digital Media EffectChainer
- DMO ラッパーフィルター（Windows 標準。ソース中のコメントは
  「Resampler DMO」だが、実体は CLSID_DMOWrapperFilter なので
  あらゆる DMO が該当する）

本フィルターはこのリストに載っていないため、オーディオスイッチャーの直後には
組み込まれない（メディアタイプの交渉より前に E_FAIL で拒否される）。
一方、上記いずれかのフィルターを間に挟めば、そのフィルターの出力ピンには
制限が無いため、本フィルターが組み込まれる。

なお、Infinite Pin Tee Filter は Windows 標準だが、橋渡しには使えない
（常に未接続の予備出力ピンを持つ仕様のため、MPC-BE のグラフ構築が
　終わらなくなり再生時にフリーズする）。動作を確認できているのは
ffdshow Audio Processor。

```
Source → 音声デコーダー → オーディオスイッチャー → ffdshow 等 → 簡易キーチェンジャー → 音声レンダラー
```

根本的な解決には、MPC-BE 側のホワイトリストに本フィルターの CLSID
`{28948BE5-05CB-4386-80FF-1805EF781731}` を追加してもらう必要がある
（同種のフィルターである CyberLink TimeStretch Filter が既に追加されている前例あり）。
