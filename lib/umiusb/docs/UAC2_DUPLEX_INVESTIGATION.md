# UAC2 Async Duplex 調査報告

## 問題の概要

STM32F4-Discovery (DWC2 OTG FS, Full Speed 12Mbps) で UAC2 Async Audio OUT + Audio IN の
duplex 動作を macOS で試みると、Audio OUT の再生が頻繁に途切れる。
Audio OUT のみ（Audio IN 無効）では問題なく再生できる。

## 最終結論

### 根本原因: DWC2 iso IN フレーム偶奇パリティの反転

`ep_write` の isochronous IN パケット送信で、フレーム偶奇（even/odd）パリティ設定が
**逆**だった。

```
誤: odd FNSOF → SD0PID (even),  even FNSOF → SODDFRM (odd)
正: odd FNSOF → SODDFRM (odd),  even FNSOF → SD0PID (even)
```

SOF callback 時点で DSTS.FNSOF は**現在のフレーム番号**を示す。パケットを現在のフレームで
送るには、そのフレームの偶奇に合わせたパリティを設定する必要がある。

#### 影響の連鎖

1. パリティ不一致で DWC2 がパケットを送信できず、DIEPCTL.EPENA がクリアされない
2. 次の SOF で `is_ep_busy()` (EPENA チェック) が true → **50% のフレームで送信スキップ**
3. Audio IN パケットレートが 1000/秒 → **約 430/秒** に低下
4. macOS は implicit feedback で IN パケットレートからデバイスクロックを推定
5. macOS が「デバイスは約 21kHz で動作している」と判断し、OUT パケット供給レートを半減
6. OUT リングバッファへの供給が消費に追いつかず、再生が頻繁に途切れる

#### 修正（stm32_otg.hh）

```cpp
// 修正前（誤）
if ((Regs::reg(Regs::DSTS) & otg::DSTS_FNSOF_ODD) != 0) {
    diepctl |= otg::DEPCTL_SD0PID;    // ← 逆
} else {
    diepctl |= otg::DEPCTL_SODDFRM;   // ← 逆
}

// 修正後（正）
if ((Regs::reg(Regs::DSTS) & otg::DSTS_FNSOF_ODD) != 0) {
    diepctl |= otg::DEPCTL_SODDFRM;   // odd frame → SODDFRM
} else {
    diepctl |= otg::DEPCTL_SD0PID;    // even frame → SD0PID/SEVNFRM
}
```

## 有効だった設計変更

### Implicit feedback の採用

duplex 時に explicit feedback EP を省略し、Audio IN を implicit feedback endpoint (0x25)
として設定。XMOS リファレンスデザインと Apple TN2274 の推奨に準拠。

- `use_implicit_fb = (SYNC_MODE == Async) && HAS_AUDIO_OUT && HAS_AUDIO_IN`
- descriptor: feedback EP 省略、Audio IN sync type = 0x25 (Async + Implicit FB)
- ランタイム: feedback 計算・送信コードを無効化

根本原因（パリティ）が修正されていれば explicit feedback でも動く可能性はあるが、
macOS が Async IN を implicit feedback として解釈する TN2274 の挙動を考えると、
explicit FB を残すと二重のフィードバックソースで混乱するリスクがある。

### FB_PACKET_SIZE = 3 (10.14 format)

UAC2 仕様は 16.16 (4 bytes) を規定するが、macOS xHCI は Full Speed で
wMaxPacketSize > 3 の isochronous EP に babble error を出す。
implicit feedback モードでは使われないが、OUT-only に戻す場合に必要。

### is_ep_busy チェックの削除

`send_audio_in_now` 内の `hal.is_ep_busy()` を削除。パリティ修正後は
EPENA が毎フレームで正常にクリアされるため、`ep_write` 内の EPENA チェックで十分。

### Audio IN write の int32_t 直接パス

`write_audio_in_overwrite(int16_t*)` → `write_audio_in(int32_t*)` に変更。
i16→i32 変換を省略し、DMA callback 内の処理時間を 121µs → 96µs に短縮。

## 調査過程で棄却された仮説

| 仮説 | 検証方法 | 結果 |
|------|----------|------|
| IsoINIncomplete (IISOIXFR) がOUT受信を阻害 | pyocd で iisoixfr カウンタ読み取り | count=0、原因ではない |
| DMA callback overrun (CPU処理時間不足) | 168MHz で処理時間計算 | 753µs < 1,333µs、余裕あり |
| リングバッファの underrun/overrun | pyocd でカウンタ確認 | 両方 0、バッファ管理は正常 |
| macOS が duplex でゼロデータを送信 | raw0_nonzero カウンタ | 非ゼロ率 ~30%、パケットは来るがレート不足 |
| USB帯域不足 | 計算 | 820B / 1350B、十分な余裕 |
| Audio IN の Sync type が原因 | Async→Synchronous→Implicit FB | Sync type 変更だけでは改善しない |
| バッファサイズ不足 | 1024→2048 に拡大 | 改善なし（供給レート不足が本質） |

## 問題特定の決め手となった計測

pyocd による実時間レート計測:

```
期待値:
  SOF:              1000/秒
  Audio IN 送信:    1000/秒
  Audio OUT 受信:   1000/秒
  DMA callback:      750/秒 (48kHz / 64 frames)

修正前の実測値:
  SOF:              ~1000/秒 (正常)
  send_audio_in:     ~430/秒 (SOF の半分!)
  ep3_in_count:      ~430/秒 (send と一致)
  on_rx_processing:  ~430/秒 (OUT パケットも半減)
  DMA callback:      ~750/秒 (正常)

→ sof_streaming (486,437) の約半分しか send_audio_in_count (243,227) がない
→ is_ep_busy() で半分スキップされていた
→ ep_write 内の ep3_epena_busy = 0 なので、スキップは audio_interface 側のチェック
```

## DWC2 (STM32 OTG FS) 技術メモ

### FIFO 割り当て (320 ワード)

```
RxFIFO:  176w @ 0     (Audio OUT 294B 受信)
TxFIFO0:  24w @ 176   (EP0 Control)
TxFIFO1:  16w @ 200   (MIDI IN)
TxFIFO2:   8w @ 216   (Feedback 3B) ※implicit FB 時は未使用
TxFIFO3:  96w @ 224   (Audio IN 294B)
合計: 320w ✓
```

### Iso IN パリティの正しい理解

DWC2 OTG FS の isochronous IN 転送では:
- SOF callback で DSTS.FNSOF を読むと**現在のフレーム番号**が得られる
- パケットは**現在のフレーム**で送信される
- SODDFRM: 奇数フレームで送信を指定
- SD0PID/SEVNFRM: 偶数フレームで送信を指定
- パリティ不一致時、パケットは送信されず EPENA がクリアされない

### I2S クロック (PLLI2S)

```
HSE = 8MHz, PLLI2SN = 258, PLLI2SR = 3
I2SCLK = 8MHz × 258 / 8 / 3 = 86MHz
I2SDIV = 3, ODD = 1
Fs = 86MHz / (256 × (2×3 + 1)) = 47,991Hz (48kHz の -0.019%)
```

## 参考資料

- [Apple TN2274: USB Audio on the Mac](https://developer.apple.com/library/archive/technotes/tn2274/_index.html)
- [XMOS sw_usb_audio reference design](https://github.com/xmos/sw_usb_audio)
- [TinyUSB implicit feedback discussion #2475](https://github.com/hathach/tinyusb/discussions/2475)
- [Linux DWC2 parity fix](https://lore.kernel.org/all/5602C796.1090201@broadcom.com/T/)
- [CM108 DataSheet v1.6](https://rats.fi/wp-content/uploads/2016/04/CM108_DataSheet_v1.6.pdf)
- [ChromeOS USB Headset Spec](https://developers.google.com/chromeos/peripherals/cc-headset-usb-v1)
- [Apple Developer Forums: USB Audio 2.0 explicit feedback](https://developer.apple.com/forums/thread/661450)
