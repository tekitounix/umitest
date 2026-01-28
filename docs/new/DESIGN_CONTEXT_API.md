# Context API 設計検討履歴

## 最終決定

**Processor/Controller分離設計**を採用。

詳細は [PLAN_AUDIOCONTEXT_REFACTOR.md](PLAN_AUDIOCONTEXT_REFACTOR.md) を参照。

---

## 検討経緯

### 初期案: OSがAudioContext渡す（案A）

```cpp
void process(umi::AudioContext& ctx) {
    float val = ctx.get_param(0);
    ctx.set_led(0x01);
}
```

OSがAudioContextを構築してprocess()に渡す。

**問題**: ControlTaskでは別のアクセス方法が必要。

### 次案: ProcessorがContext保持（案B）

```cpp
struct MyProcessor {
    umi::Context ctx;  // Processorが保持
    void process(umi::AudioBuffer& buf) {
        float val = ctx.get_param(0);
        ctx.set_led(0x01);
    }
};
```

Processorがパラメータ/イベント/LED用のContextをメンバとして保持。

**問題**:
- ctxが共有メモリへのビュー（ポインタ）だと、Processorが状態を所有していない
- ctxが状態のコピーを保持すると、メモリ2倍＋同期コスト

### 検討: プラグインでの一般的な方法

調査結果：
- **VST3/CLAP**: ホストがパラメータを所有、process時にイベントで渡す
- **AUv3**: AUParameterTree（フレームワーク）が所有、Processorは参照
- **JUCE**: Processorがパラメータを保持（内部で同期）

### 重要な気づき

> 共有メモリは基本的にハードウェア状態のフレームバッファ、
> Processorの状態は信号処理の状態フレームなので一対一で対応するわけではない。
> よって分ける必要がある。

| | 共有メモリ | Processor状態 |
|--|----------|--------------|
| 内容 | ハードウェアI/O | 信号処理状態 |
| 所有者 | OS | Processor |
| 対応 | 1:1ではない | - |

### LFO → LED の例での検討

ProcessorのLFO状態をLEDに反映させる方法：

**A: process()内で直接更新**
```cpp
void process(...) {
    lfo_phase += ...;
    io.set_led(lfo_phase < 0.5f);  // process内でI/O操作
}
```

**B: Controllerに状態を渡して更新**
```cpp
void process(...) {
    lfo_phase += ...;
    // LEDには触らない
}

// Controller（~60Hz）
void update() {
    shared->led_state = proc.lfo_phase < 0.5f;
}
```

調査結果: **Bが一般的**。
- オーディオスレッドは処理に専念
- UI更新レート（60Hz）とオーディオレート（48kHz）が違う
- Atomic + CASでスレッド間通信

### 最終結論

**ControllerがProcessorインスタンスを持つから、直接Processorの状態を扱える。**

よって：
- **Processor**: 信号処理のみ、I/Oアクセスなし
- **Controller**: Processorの状態を読んで共有メモリに反映

---

## 採用設計

```cpp
struct MyProcessor {
    float lfo_phase = 0.0f;  // 信号処理状態

    void process(umi::AudioBuffer& buf, const umi::InputState& in) {
        lfo_phase += buf.dt * in.get_param(0);
        // LEDには触らない
    }
};

int main() {
    static MyProcessor proc;
    auto* shared = umi::syscall::get_shared();

    umi::syscall::register_processor(proc);

    while (true) {
        umi::syscall::wait_event_timeout(16000);
        // Processorの状態を読んでI/Oに反映
        shared->led_state.store(proc.lfo_phase < 0.5f ? 1 : 0);
    }
}
```

**メリット**:
- Processorはプラットフォーム非依存
- I/O同期はControllerのみがプラットフォーム依存
- メモリコピー不要（共有メモリを直接操作）
- 同期ポイントが明確（Controllerの更新ループ）
