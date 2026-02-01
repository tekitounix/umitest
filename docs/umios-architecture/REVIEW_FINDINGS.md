# umios-architecture ドキュメントレビュー結果

**レビュー日**: 2026-02-01
**対象**: `docs/umios-architecture/` 内の全ドキュメント (00〜09)
**目的**: ドキュメント間の矛盾、曖昧な記述、改善すべき点の洗い出し

---

## 1. 矛盾点

### 1.1 SharedMemory 構造体と共有メモリの実体が不一致

**関連**: 07-memory.md (L107-142) vs 04-param-system.md (L389-411) vs 01-audio-context.md (L196-237)

07-memory.md の `SharedMemory` 構造体には `std::atomic<float> params[MAX_PARAMS]` (128B) が定義されているが、01-audio-context.md と 04-param-system.md では `SharedParamState` (164B: float×32 + changed_flags + version) という別の構造体が定義されている。

同様に、`SharedChannelState` (64B) と `SharedInputState` (32B) は 01/04 では `AudioContext` のメンバとして明確に定義されているが、07-memory.md の `SharedMemory` にはこれらが含まれていない。代わりにフラットな `button_pressed`, `button_current`, `led_state` 等が直接定義されている。

**問題**: SharedMemory が「実際の実装に近い低レベル定義」で、SharedParamState 等が「論理的な抽象構造」だとしても、両者の対応関係が明記されていない。読者はどちらが正式な定義か判断できない。

### 1.2 MIDI CC 値の正規化方式が 2 種類存在

**関連**: 04-param-system.md (L200) vs 02-processor-controller.md (L291, L352) / 03-event-system.md (L254)

- **ParamMapping 経路** (04-param-system.md L200): `float normalized = float(ump.cc_value()) / 127.0f` → 0.0〜1.0 の float
- **ControlEvent 経路** (02-processor-controller.md L291): `cc_value << 9` → 0x0000〜0xFE00 の uint16_t

これは「異なる経路で異なる正規化」という設計意図の可能性があるが、同一の CC 値が経路によって異なる表現になることが明示されていない。特に、`ROUTE_PARAM | ROUTE_CONTROL` で両経路に同時送信される場合、値の対応関係を理解する必要があるが、その説明がない。

### 1.3 AppHeader のサイズ記述の矛盾

**関連**: 07-memory.md (L67) vs 09-app-binary.md (L17, L61)

- 07-memory.md: `AppHeader (256B)` と記載
- 09-app-binary.md: 構造体定義が 128B、本文でも「128 バイト」と明記

**問題**: 07-memory.md の Flash レイアウト図が誤り。

### 1.4 SRAM レイアウトのアドレスとサイズの不整合

**関連**: 07-memory.md (L17-48, L53-58)

SRAM レイアウト図:
- App Stack: 0x2001C000 – 0x20020000 = 16KB
- 予約: 0x20018000 – 0x2001C000 = 16KB
- App Data: 0x2000C000 – 0x20018000 = **48KB**

しかし MPU 保護テーブル:
- App Data: 0x2000C000 – 0x20013FFF = **32KB**
- App Stack: 0x20014000 – 0x20017FFF = **16KB**

MPU テーブルのアドレス範囲とレイアウト図のアドレスが一致していない。レイアウト図では App Data が 0x20018000 まで（48KB）だが、MPU テーブルでは 0x20013FFF まで（32KB）。App Stack もレイアウト図では 0x2001C000 開始だが、MPU テーブルでは 0x20014000 開始。

さらに「アプリ利用可能: ~48KB (.data 32KB + stack/heap 16KB)」(L217) という記載は、レイアウト図の「予約 16KB」(0x20018000-0x2001C000) を含めるかどうかで解釈が変わる。

### 1.5 WaitEvent の mask=0 と mask=0xFFFFFFFF の意味が矛盾

**関連**: 06-syscall.md (L164-169) vs 02-processor-controller.md (L437)

06-syscall.md:
- `mask = 0`: 全イベント待機
- `mask = 0xFFFFFFFF`: （記載なし、だがビットマスクなら全ビット立ち = 全イベント待機）

`mask = 0` で全イベント待機は、ビットマスクの慣例（0 = なにも待たない）に反する。これが仕様ならば特殊ケースとして明示すべき。02-processor-controller.md L437 では `umi::wait_event(umi::event::ControlEvent | umi::event::Timer, 16000)` とビットマスクとして正しく使用している一方、L164 のデフォルト引数は `0xFFFFFFFF` で矛盾する。

---

## 2. 曖昧・未定義の箇所

### 2.1 EventRouter の実行タイミングとブロック境界の関係

**関連**: 00-overview.md (L88-89), 03-event-system.md (L273-288), 08-backend-adapters.md (L56-64)

EventRouter は System Task (優先度 1) で動作すると記載されているが、ブロック境界での swap は Audio Task が管理するのか System Task が管理するのか不明確。

- 08-backend-adapters.md: Audio Task が「DMA → process() → DMA」の一連のフローを管理
- 03-event-system.md: 「Audio ブロック境界で swap」

DMA 64 サンプルごとに Audio Task が起床し、4 回蓄積して 256 サンプルで process() を呼ぶ構造 (07-memory.md L184) において、EventRouter が System Task で動作しているなら、AudioEventQueue への push と Audio Task による pop のタイミング制御がどう行われるかが記述されていない。

### 2.2 process() 内での syscall 禁止と output_events の関係

**関連**: 01-audio-context.md (L260)

「syscall: 特権遷移コスト（output_events 経由で MIDI 送信すること）」と記載。process() 内で syscall が禁止なのはリアルタイム安全性の理由だが、これはコスト問題ではなく、SVC 例外によるコンテキストスイッチがデッドライン違反を起こす可能性があるためのはず。理由の記述が不正確。

### 2.3 InputMode の CONTROL_ONLY の命名

**関連**: 03-event-system.md (L302-307)

```
PARAM_ONLY = 1,     // InputParamMapping 経由のみ
EVENT_ONLY = 2,     // ControlEventQueue のみ
CONTROL_ONLY = 3,   // PARAM_ONLY + EVENT_ONLY
```

`CONTROL_ONLY` は `PARAM_ONLY + EVENT_ONLY` の組み合わせと記載されているが、値が `3` (= 1 | 2) であることからビットフラグとして設計されていると推測できる。しかし `CONTROL_ONLY` という名前は「PARAM と EVENT の両方」という意味を伝えない。`BOTH` や `FULL` の方が直感的。

### 2.4 umidi::Event のサイズの明記

**関連**: 01-audio-context.md (L100-115), 04-param-system.md (L398)

umidi::Event の構造は定義されている（sample_pos: uint16_t, type: uint8_t, status: uint8_t, data: uint32_t）が、`sizeof` が明記されていない。04-param-system.md のメモリ使用量テーブルでは `umidi::Event 8B × 64` (L398) とあるので 8B と推定できるが、構造体定義のそばに明記すべき。

### 2.5 ControlEvent の MIDI 型と process() の input_events の関係

**関連**: 02-processor-controller.md (L267), 03-event-system.md (L204-212)

MIDI イベントは process() の `input_events` (umidi::Event) と Controller の ControlEvent (MIDI 型) の両方で受信できるが、同一メッセージが両方に届く条件（`ROUTE_AUDIO | ROUTE_CONTROL_RAW` の同時指定時）について、Event と ControlEvent で構造体が異なるため、アプリ側でどう統合するかのガイダンスがない。

### 2.6 SharedMemory の Shared Audio / Shared MIDI / Shared HwState 境界のメンバー配置

**関連**: 07-memory.md (L107-142, L148-151)

SharedMemory 構造体には `// === オーディオバッファ (Shared Audio 8KB 領域) ===` 等のコメントがあるが、各メンバーの実サイズを合計すると:
- Shared Audio 領域: audio_input (2048) + audio_output (2048) + mic_input (1024) + sample_rate(4) + buffer_size(4) + dt(4) + sample_position(8) = **5140B** (8KB に収まるが領域の半分しか使っていない)
- Shared MIDI 領域: Event×64 = 512B + write_idx(4) + read_idx(4) + params(128) = **648B** (2KB に収まるがかなり余裕がある)

実際に 8KB / 2KB / 1KB の境界にどのメンバーが配置されるかは、リンカスクリプトの `_shared_audio_start` 等のアドレスと構造体レイアウトの対応が不明確。構造体が連続メモリに配置されるなら、コメントの領域区分と実際の配置は一致しない。

### 2.7 _start() のローダとの責務分担

**関連**: 09-app-binary.md (L117-137)

ローダの動作 (L119-120):
- `.data` セクションを SRAM にコピー
- `.bss` 領域をゼロ初期化

_start() のコメント (L131-132):
- `.data の初期化（必要に応じて）`
- `.bss のゼロ初期化`

ローダが既に行っている処理を _start() でも行うのか、二重初期化なのか、条件付きなのかが曖昧。「必要に応じて」とあるが、その条件が不明。

---

## 3. 改善提案

### 3.1 SharedMemory と SharedParamState/SharedChannelState/SharedInputState の関係を明確化

**優先度: 高** — 実装に直結するため最優先で解決すべき

**現状の問題:**
07-memory.md の `SharedMemory` はフラットな構造体（`std::atomic<float> params[32]`, `button_pressed` 等）として定義されているが、01-audio-context.md と 04-param-system.md では `SharedParamState`（`float values[32]` + `changed_flags` + `version`）という別構造体が定義されている。両者は同じ共有メモリ領域を指しているはずだが、メンバーが一致しない。

**方針:**
SharedMemory の完全な構造体定義は **新規章 (10-shared-memory.md)** に集約する。SharedMemory は複数の章にまたがる中核データ構造であり、特定の章の付録ではなく独立した章として扱うのが適切。

**具体的な修正内容:**

1. **10-shared-memory.md を新設**
   - SharedMemory 構造体の完全な定義（全メンバー、サイズ、アライメント）
   - SharedParamState / SharedChannelState / SharedInputState の正式定義（現在 01/04 に分散しているもの）
   - 各メンバーの書き込み権限（カーネルのみ / アプリ読み取り専用 / 双方向）
   - メモリ領域（Shared Audio / Shared MIDI / Shared HwState）との対応

2. **07-memory.md からは SharedMemory 構造体定義を削除し、アドレスマップに徹する**
   - リンカシンボル、SRAM/Flash/CCM の物理アドレス配置、MPU リージョン設定のみ
   - SharedMemory の構造は「詳細は 10-shared-memory.md を参照」とリンク

3. **01-audio-context.md / 04-param-system.md では必要なメンバーのみ抜粋して引用**
   - AudioContext が参照する SharedParamState 等は、構造体の全定義ではなく process() で使うメンバーのみ抜粋
   - 「完全な定義は 10-shared-memory.md を参照」と明記
   - 例: 01-audio-context.md では SharedParamState の `values[]` と `changed_flags` のみ示し、メモリレイアウト上の配置やアライメントの詳細は省略

**10-shared-memory.md の構成案:**
```
# 10 — SharedMemory 仕様

## 概要
カーネルとアプリ間で共有するメモリ領域の構造体定義。

## SharedMemory 構造体（完全定義）
- 全メンバー一覧、sizeof、アライメント
- 各メンバーの書き込み権限表

## 内包構造体
### SharedParamState（パラメータ値）
### SharedChannelState（MIDI チャンネル状態）
### SharedInputState（ハードウェア入力状態）

## メモリ領域との対応
- Shared Audio (8KB) に配置されるメンバー
- Shared MIDI (2KB) に配置されるメンバー
- Shared HwState (1KB) に配置されるメンバー
→ 物理アドレスは 07-memory.md を参照

## アクセスパターン
- Audio Task (process()) からのアクセス → AudioContext 経由（01 参照）
- Controller (main()) からのアクセス → syscall / 直接読み取り
- EventRouter からの書き込み → System Task 内で排他なし（SPSC 前提）
```

この構成により:
- SharedMemory の詳細な定義を 1 箇所に集約できる
- 07-memory.md は物理アドレスマップに専念できる
- 01/04 は「使い方」に集中し、定義の重複を排除できる

### 3.2 値の正規化フローを一箇所にまとめた図を追加

**優先度: 高** — CC 値の扱いが経路別に異なる点はアプリ開発者の混乱源

**現状の問題:**
同じ CC#74 値=100 が RouteFlags によって以下の異なる表現になるが、その全体像が一箇所にまとまっていない:
- `ROUTE_PARAM`: 100/127 = 0.787 → denormalize → 4536.0 Hz (04-param-system.md L182-193)
- `ROUTE_CONTROL`: 100<<9 = 0xC800 (02-processor-controller.md L291)
- `ROUTE_AUDIO`: umidi::Event{.data = (0xB0<<24 | 74<<8 | 100)} — 生値のまま
- `ROUTE_CONTROL_RAW`: UMP32{0x20B04A64} — UMP32 のまま

**提案:**
03-event-system.md の「経路分類」セクション直後に「値の正規化一覧」セクションを新設する。以下の内容を含める:

1. **経路別変換表**: 上記の 4 経路それぞれで CC 値がどう変換されるか
2. **変換の理由**: なぜ経路ごとに異なるか（PARAM は連続値制御に最適化、CONTROL はハードウェア入力との統一、AUDIO は MIDI 処理の自由度確保）
3. **同時送信時の注意**: `ROUTE_PARAM | ROUTE_CONTROL` で同一 CC を両経路に送る場合、Processor が読む `params.values[n]` と Controller が受け取る `INPUT_CHANGE.value` は異なるスケールであることを明記
4. **コード例**: 各経路での値の読み取りと、必要な場合の相互変換方法

### 3.3 バッファサイズ階層とタスク間のデータフロー図を追加

**優先度: 高** — 現状最も理解が困難な箇所

**現状の問題:**
07-memory.md にバッファサイズの階層表はあるが、DMA 転送から process() 呼び出しに至るまでの「時間軸上の動作」が記述されていない。特に以下が不明:

- DMA Half/Complete 割り込みは誰が受けるか（Audio Task? ISR から直接通知?）
- DMA 転送をどこで蓄積するか（SharedMemory の audio_input に直接? 別のステージングバッファ?）
- 蓄積中に EventRouter は何をしているか（並行して MIDI を AudioEventQueue に push?）
- process() 呼び出しのトリガーは何か

**提案:**
08-backend-adapters.md の「組み込みバックエンド」セクションに、タスク間の時間軸上のデータフローを示すシーケンス図を追加する。DMA バッファサイズやアプリバッファサイズは HW 構成や設計判断で変わりうるため、具体的な数値は「現在の構成例」として注釈付きで示す:

```
例: DMA 64 サンプル × 4 回蓄積 = 256 サンプルで process() 呼び出しの場合

時刻    DMA ISR          Audio Task         System Task (EventRouter)
─────┬───────────────┬─────────────────┬──────────────────────────
t0   │ Half完了 #1   │                 │ MIDI受信 → RawInputQueue
     │ → 通知        │ 64サンプル蓄積  │ → RouteTable参照
t1   │ Complete #1   │                 │ → AudioEventQueue push
     │ → 通知        │ 128サンプル蓄積 │ → SharedParamState更新
t2   │ Half完了 #2   │                 │
     │ → 通知        │ 192サンプル蓄積 │
t3   │ Complete #2   │                 │
     │ → 通知        │ 256サンプル蓄積 │
     │               │ ── ブロック境界 ──│── ダブルバッファswap ──
     │               │ AudioContext構築 │
     │               │ process() 呼出  │
     │               │ output_events処理│
     │               │ 出力→DMAバッファ │
```

> **注**: DMA バッファサイズ (64)、蓄積回数 (4)、アプリバッファサイズ (256) は現在の STM32F4 構成の例。これらの値は HW 構成やレイテンシ要件に応じて変更可能であり、07-memory.md の「バッファサイズの階層」で定義される。

また、以下の設計判断がドキュメントから読み取れないため、明記すべき:
- Audio Task は DMA 蓄積完了を待って起床するのか、毎回起床して蓄積カウンタを確認するのか
- ダブルバッファ swap の主体は Audio Task か System Task か（現状は曖昧）
- process() 実行中に次の DMA 転送が発生した場合のバッファ管理方式（ダブルバッファ? トリプルバッファ?）

### 3.4 07-memory.md の SRAM レイアウト図と MPU テーブルを統一

**優先度: 高** — アドレスの矛盾は実装バグに直結する

**現状の問題:**
レイアウト図とMPU テーブルでアドレスが食い違っている（詳細は §1.4 参照）。根本原因として、レイアウト図は「論理的な領域分割」を示し、MPU テーブルは「MPU で保護可能な 2^n アラインメント境界」を示している可能性がある。STM32F4 の MPU は 2^n サイズ・アラインメントの制約があるため、論理レイアウトと MPU リージョンが完全に一致しないことは想定される。

**提案:**
1. レイアウト図を 1 つに統合し、「MPU リージョン」と「論理領域」の両方を重ねて表示する
2. MPU の 2^n 制約により論理領域と MPU リージョンがずれる箇所を注釈で説明する
3. 0x20014000–0x20018000 の「予約」領域が App Stack の MPU リージョン (0x20014000–0x20017FFF) に含まれるのか、アクセス不可領域なのかを明確にする

```
0x20020000 ┌───────────────────────────────────┐ ← _estack
           │  App Stack (16KB)                 │  MPU Region 1: RW 非特権
           │                                   │  (0x2001C000–0x2001FFFF)
0x2001C000 ├───────────────────────────────────┤
           │  (予約 / ガードページ)              │  ← MPU 保護外? 要確認
0x20018000 ├───────────────────────────────────┤
           │  App Data (32KB)                  │  MPU Region 0: RW 非特権
           │  .data / .bss / heap              │  (0x2000C000–0x20013FFF)?
           │                                   │  ← ここもアドレス要確認
0x2000C000 ├───────────────────────────────────┤
           ...
```

### 3.5 Syscall 一覧表に対応するアプリ API 関数名を追加

**優先度: 中** — 開発者体験の改善

**現状の問題:**
06-syscall.md の syscall テーブルには Nr と内部名（`SetAppConfig` 等）しかない。アプリ開発者が使う API 名（`umi::set_app_config()` 等）は 02-processor-controller.md の別テーブルにある。両者を突き合わせるのに 2 ドキュメントを行き来する必要がある。

**提案:**
06-syscall.md の各グループテーブルに「アプリ API」列を追加する:

| Nr | 名前 | アプリ API | 引数 | 戻り値 | 状態 |
|----|------|-----------|------|--------|------|
| 20 | `SetAppConfig` | `umi::set_app_config(cfg)` | `config: const AppConfig*` | 0 / エラー | 新設計 |

また、02-processor-controller.md の API 一覧テーブル (L222-238) にも逆方向のリンク（対応する syscall Nr）を追加すると双方向で参照できる。

### 3.6 「新設計」「将来」ステータスの明確化

**優先度: 中** — ドキュメント全体の信頼性に影響

**現状の問題:**
06-syscall.md の状態欄に 3 種のステータスがあるが定義がない:
- 「実装済み」: 現在のカーネルに存在する（Exit, Yield, RegisterProc, WaitEvent, GetTime, Sleep, GetShared, Log, Panic）
- 「新設計」: グループ 2 の構成系 syscall 全て（SetAppConfig, SetRouteTable 等）
- 「将来」: MIDI/SysEx 系、ファイルシステム系

**提案:**
06-syscall.md の冒頭に以下のステータス定義表を追加する:

| ステータス | 意味 | コードへの影響 |
|-----------|------|--------------|
| 実装済み | カーネルに実装が存在し、アプリから呼び出し可能 | 使用可 |
| 新設計 | 本ドキュメントで仕様を確定。実装は未着手または進行中 | 使用不可（コンパイルは通るがスタブ） |
| 将来 | 仕様は方向性のみ。詳細は未確定 | 使用不可（定義なし） |

さらに README.md にドキュメント全体のスタンスを明記する:

> 本ドキュメント群は **目標仕様** である。「実装済み」と明記された箇所は現在のコードと一致するが、「新設計」「将来」の箇所は未実装の設計目標を記述している。現在の実装との差異がある場合は各セクションの「旧ドキュメントとの差異」ブロックを参照のこと。

### 3.7 README.md に各章の依存関係と推奨読み順を追記

**優先度: 中** — 新規メンバーのオンボーディングに有用

**提案:**
README.md に以下の依存関係図と推奨読み順を追加する:

```
依存関係:

00-overview ─────────────────────── (全章の前提)
    │
    ├── 01-audio-context ◄───────── 02, 03, 04, 08 が参照
    │
    ├── 02-processor-controller ◄── 04 が参照
    │       │
    │       └── 03-event-system ◄── 04, 05 が参照
    │               │
    │               └── 04-param-system
    │
    ├── 05-midi ◄────────────────── 03 の RawInput/RouteTable を前提
    │
    ├── 06-syscall ◄─────────────── 02 の API 一覧を前提
    │
    ├── 07-memory ◄──────────────── 01, 04 の構造体定義を前提
    │
    ├── 08-backend-adapters ◄────── 01〜07 の全てを参照
    │
    └── 09-app-binary ◄──────────── 07 のメモリレイアウトを前提
```

**推奨読み順:**
1. 全体像を掴む: 00 → 01 → 02
2. イベント/パラメータの仕組み: 03 → 04
3. MIDI 統合: 05
4. OS インターフェース: 06 → 07
5. マルチターゲット: 08 → 09

### 3.8 全構造体に sizeof コメントを統一的に付与

**優先度: 低** — メモリ計算の検証容易性向上

**現状の問題:**
sizeof コメントの有無が構造体ごとにまちまち:
- あり: SharedParamState (164B), SharedChannelState (64B), SharedInputState (32B), ControlEvent (8B), RawInput (12B), ParamMapEntry (12B), RouteTable (272B), AppConfig (~2128B), InputConfig (8B), ParamSetRequest (8B), AppHeader (128B)
- なし: **umidi::Event**, **UMP32**, **StreamParser**, **AudioContext**, **SharedMemory** (合計サイズの明記なし)

**提案:**
以下の構造体に `// sizeof` コメントを追加する:

| 構造体 | 推定サイズ | 定義箇所 |
|--------|----------|---------|
| umidi::Event | 8B | 01-audio-context.md L100 |
| umidi::UMP32 | 4B | 05-midi.md L18 |
| StreamParser | ~5B | 05-midi.md L66 |
| AudioContext | ~80B (ポインタ/スパンサイズ依存) | 01-audio-context.md L13 |
| SharedMemory | ~5.5KB (要計算) | 07-memory.md L108 |

特に umidi::Event は AudioEventQueue のサイズ計算 (04-param-system.md L398: `8B × 64 = 512B`) の根拠となるため重要。

### 3.9 process() 内の禁止事項の理由をより正確に

**優先度: 低** — 技術的正確性の改善

**現状の記述** (01-audio-context.md L260):
> syscall: 特権遷移コスト（output_events 経由で MIDI 送信すること）

**問題:**
「コスト」が禁止の主な理由ではない。SVC 例外を Audio Task (優先度 0) 内で発行すると:
1. SVC ハンドラが実行され、スケジューラが介入する可能性がある
2. syscall によっては System Task への通知が発生し、プリエンプションが起きうる
3. Audio Task のデッドライン（DMA 転送完了までにバッファを埋める）に間に合わなくなるリスクがある

**提案する修正文:**
> syscall: SVC 例外によるスケジューラ介入がデッドライン違反を引き起こすリスクがある。MIDI 送信は output_events 経由で行い、Runtime に処理を委譲すること。

### 3.10 タスク表から FPU 列を削除し、スケジューラドキュメントに分離

**優先度: 中** — タスク設計の整合性に関わる

**現状の問題:**
00-overview.md L89 のタスク表に FPU 欄（Exclusive / LazyStack / Forbidden）があるが、これはスケジューラの FPU コンテキスト退避ポリシーであり、アーキテクチャ概要に属する情報ではない。また：

- 実際には複数タスクが FPU を使いたい場面が多い（EventRouter の float 演算等）
- 2 つ以上のタスクが FPU を使うなら退避は必須であり、"Forbidden" で済むケースは限られる
- FPU なしマイコンも対象であり、FPU 列自体が無意味になる

**提案:**

1. **00-overview.md のタスク表から FPU 列を削除する**
   - 概要レベルでは「タスク名・優先度・責務」で十分

2. **11-scheduler.md を新設し、FPU 退避ポリシーをそこに記載する**
   - スケジューラのコンテキストスイッチ実装の一部として FPU 退避ポリシー（Exclusive / LazyStack / Forbidden）を説明
   - FPU なしターゲットでの動作も含める

3. **FPU なし/あり版のコード設計方針を明記する**
   - 基本は FPU なしでも動作するコードを書く
   - FPU あり版は最適化パスとして用意（`#if __FPU_PRESENT` 等）
   - 04-param-system.md 等のコード例が float 前提なら、整数版の代替を併記するか、FPU 前提であることを注記する

**11-scheduler.md の構成案:**
```
# 11 — スケジューラ

## 概要
タスクスケジューリング、コンテキストスイッチ、優先度管理。

## タスク優先度と実行モデル
（TODO: 00-overview.md のタスク表を詳細化）

## コンテキストスイッチ
（TODO: レジスタ退避/復元の仕組み）

## FPU コンテキスト退避ポリシー
- Exclusive: 常に FPU レジスタを保存/復元
- LazyStack: Cortex-M Lazy Stacking を利用、実際の使用時のみ退避
- Forbidden: FPU レジスタの退避を行わない（使うと他タスクの状態を破壊）
- ポリシーはコンパイル時に選択（ポリシーベース設計）
- FPU なしターゲットではこのポリシー自体が無効

## タイマーとスリープ
（TODO）

## 割り込みとタスク通知
（TODO）
```

### 3.11 イベントバッファの容量設計を明記

**優先度: 中** — アプリ開発者のエッジケース対策に必要

**現状の問題:**
01-audio-context.md L193 で「スパンの容量を超えて書き込んではならない（超過分は無視される）」と記載されているが、スパンの容量自体が明記されていない。

**設計方針:**

1. **OS 側がバッファを確保する**
   - 初期化時（`process()` ループ開始前）に heap または静的領域で確保。リアルタイムパスでの確保ではない
   - テンプレートパラメータで OS 側の最大上限を設定可能
   - `RegisterProc` 時にアプリが希望容量を伝達。OS の上限を超える要求は clamp

2. **アプリ側は span のサイズで実際の容量を確認する**
   - `ctx.output_events.size()` が実際に使える容量
   - 容量不足の場合は複数ブロックに分散して送信

3. **input_events にも同じパターンを適用する**
   - output_events と input_events（AudioEventQueue）で統一的な容量管理

**提案:**
以下を 01-audio-context.md に追記する:
- output_events / input_events の容量は OS 側が初期化時に決定する
- アプリは `RegisterProc` で希望容量を伝達可能（OS の上限で clamp）
- `ctx.output_events.size()` / `ctx.input_events.size()` で実際の容量を確認して使う
- 溢れた場合の推奨対策（優先度の低いイベントを間引く等）

### 3.12 _start() とローダの初期化責務の境界を明確化

**優先度: 中** — 実装上の二重初期化が存在するため、ドキュメントと実装の両方を修正すべき

**現状の実装（確認済み）:**

| 処理 | ローダ (`loader.cc` L298-317) | `_start()` (`crt0.cc` L52-64) |
|------|------|------|
| `.data` コピー | AppHeader のセクションサイズで Flash→RAM コピー | リンカシンボル (`_sidata`→`_sdata`) で同じ処理 |
| `.bss` ゼロ初期化 | AppHeader の bss_size でゼロ埋め | リンカシンボル (`_sbss`→`_ebss`) で同じ処理 |
| `__init_array` | 行わない | 行う |
| `main()` 呼び出し | 行わない | 行う |

`.data`/`.bss` の初期化が**二重に実行されている**。XIP (Flash 直接実行) 構成ではコピー元アドレスが一致するため実害はないが、冗長であり、責務の境界が不明確。

**方針:**
`.data`/`.bss` 初期化は `_start()` (crt0) の責任とする。これは ARM の標準的な crt0 の慣例に従う。

- ローダの責任: メモリ領域の確保、Flash イメージの配置確認、MPU 設定、エントリポイント計算、`_start()` へのジャンプ
- `_start()` の責任: `.data` コピー、`.bss` ゼロ初期化、`__init_array`、`main()` 呼び出し

ローダが `.data`/`.bss` まで初期化すると、アプリのリンカシンボル (`_sidata` 等) とローダの AppHeader ベースのセクション情報が密結合になる。`_start()` に一本化することで、ローダはアプリの内部レイアウトを知る必要がなくなる。

**具体的な修正:**

1. **`loader.cc` の `copy_sections()`**: `.data` コピーと `.bss` ゼロ初期化を削除。`.text` の配置（XIP アドレス記録）とエントリポイント計算のみに限定
2. **`crt0.cc` の `_start()`**: 現状のまま（`.data`/`.bss` 初期化 + `__init_array` + `main()`）
3. **09-app-binary.md**: ローダと `_start()` の責務境界を明記

```
ローダ                          _start() (crt0)
──────────────────────────      ──────────────────────────
1. AppHeader 検証               1. .data コピー (Flash→RAM)
2. CRC / 署名検証               2. .bss ゼロ初期化
3. メモリ領域確保               3. __init_array (グローバルコンストラクタ)
4. MPU 設定                     4. main() 呼び出し
5. PSP 設定                     5. umi::exit() で終了
6. _start() にジャンプ
```

---

## 4. 軽微な指摘

| 箇所 | 内容 |
|------|------|
| 00-overview.md L89 | タスク表の FPU 列を削除し、11-scheduler.md に分離すべき（§3.10 参照） |
| 03-event-system.md L306 | `CONTROL_ONLY = 3` — コメントに `PARAM_ONLY + EVENT_ONLY` とあるが `PARAM_AND_EVENT` の方が正確 |
| 04-param-system.md L299 | InputConfig の例 `{KNOB_CUTOFF, InputMode::CONTROL_ONLY, 0, 1000, 655}` — smoothing=1000 は ms 単位? threshold=655 は 0-65535 の値? 単位が不明確 |
| 05-midi.md L260 | `SysExAssembler` のバッファ 256B × 2 = 520B とあるが、構造体に complete(bool) と length(uint16_t) もあるので ~520B は概算としてはよいが正確ではない |
| 07-memory.md L82 | Flash セクター配分 `sector 0-5 (256KB)` — 実際は 16K×4 + 64K×1 + 128K×1 = 240KB。sector 0-5 が 256KB になるには 128K×2 が必要だが sector 5 は 128K のため不一致 |
| 09-app-binary.md L22 | APP_MAGIC `0x414D4955` を "UMIA" (little-endian) としているが、ASCII で U=0x55, M=0x4D, I=0x49, A=0x41 なので little-endian 読みだと "UMIA" = 0x4149_4D55。0x414D4955 は "AMIU" になる |

---

## 5. 影響範囲マトリクス

各改善提案を実施した場合に修正が必要なドキュメント・コードの一覧。

| 提案 | 修正対象 | 状態 | 備考 |
|------|---------|------|------|
| 3.1 SharedMemory 集約 | **10-shared-memory.md** (新設), **07-memory.md**, **01-audio-context.md**, **04-param-system.md**, **03-event-system.md** | **修正済み** | 03 も AudioEventQueue のサイズ等で SharedMemory を参照 |
| 3.2 正規化フロー | **03-event-system.md** | **修正済み** | — |
| 3.3 データフロー図 | **08-backend-adapters.md** | **修正済み** | タスク間シーケンス図を追加 |
| 3.4 SRAM/MPU 統一 | **07-memory.md**, **09-app-binary.md** | **修正済み** | MPU テーブル統一、論理/MPU 範囲の注釈追加 |
| 3.5 Syscall API 名 | **06-syscall.md**, **02-processor-controller.md** | **修正済み** | — |
| 3.6 ステータス定義 | **06-syscall.md**, **README.md** | **修正済み** | — |
| 3.7 依存関係図 | **README.md** | **修正済み** | 10, 11 含む依存関係図と推奨読み順を追加 |
| 3.8 sizeof コメント | **01-audio-context.md**, **05-midi.md**, **07-memory.md** | **修正済み** | — |
| 3.9 process() 禁止理由 | **01-audio-context.md** | **修正済み** | — |
| 3.10 FPU/スケジューラ | **00-overview.md**, **11-scheduler.md** (新設), **08-backend-adapters.md** | **修正済み** | FPU 列削除、11-scheduler.md 参照追加 |
| 3.11 イベントバッファ | **01-audio-context.md**, **06-syscall.md** | **修正済み** | RegisterProc に event_capacity パラメータ追加 |
| 3.12 _start/loader | ~~loader.cc~~, ~~09-app-binary.md~~ | **修正済み** | 前回セッションで対応 |
| 4.1 FPU 列 | **00-overview.md** | **修正済み** | §3.10 で対応 |
| 4.2 CONTROL_ONLY コメント | **03-event-system.md** | **修正済み** | `PARAM_AND_EVENT` に修正 |
| 4.3 InputConfig 単位 | **04-param-system.md** | **修正済み** | smoothing/threshold の単位を明記 |
| 4.4 SysExAssembler サイズ | **05-midi.md** | **修正済み** | 概算値の内訳注釈修正 |
| 4.5 Flash セクター配分 | **07-memory.md** | **修正済み** | sector 0-5 = 240KB に修正 |
| 4.6 APP_MAGIC エンディアン | **09-app-binary.md** | **修正済み** | メモリ上バイト列表記に修正 |

### 新設ドキュメント一覧

| ファイル | 提案 | 内容 |
|---------|------|------|
| 10-shared-memory.md | 3.1 | SharedMemory 構造体の完全定義 |
| 11-scheduler.md | 3.10 | スケジューラ、FPU 退避ポリシー（FPU セクションのみ記載、他は TODO） |

### 修正回数の多いドキュメント

複数の提案で修正が必要なドキュメントは、まとめて修正すると効率的:

| ドキュメント | 関連提案 |
|------------|---------|
| 01-audio-context.md | 3.1, 3.8, 3.9, 3.11 |
| 06-syscall.md | 3.5, 3.6, 3.11 |
| 07-memory.md | 3.1, 3.4, 3.8 |
| README.md | 3.6, 3.7 |

---

## 6. まとめ

ドキュメント全体としては、設計の意図と構造がよく整理されている。特に以下の点は優れている:

- 三層モデル（Audio / Control / System）の責務分離が明確
- RouteTable によるデータ駆動ルーティングの設計
- ダブルバッファによるロックフリーな設定変更

主な改善点は:
1. **07-memory.md と他ドキュメントの構造体定義の統一**（最優先）
2. **SRAM レイアウトと MPU テーブルのアドレス不整合の修正**
3. **値の正規化フローの経路別整理**
4. **ドキュメントのステータス（仕様 vs 現状記録）の明確化**
