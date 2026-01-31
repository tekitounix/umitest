# UMI ドキュメント

**バージョン:** 0.10.0
**更新日:** 2025-01-24

UMI (Universal Musical Instruments) プラットフォームのドキュメントです。

---

## 実装状況

| 機能 | 状態 | 備考 |
|------|------|------|
| カーネル/アプリ分離 | ✓ | v0.10.0 |
| 非特権モード実行 | ✓ | MPU + RTOS |
| Ed25519署名検証 | ✓ | フルスクラッチ |
| USB Audio | ✓ | Full-Speed |
| MIDI over USB | ✓ | |
| Web (WASM) | ◇ | 将来仕様 |
| DAW プラグイン | ◇ | VST3/AU/CLAP |
| コルーチン | ✓ | C++20 coroutines |

- ✓ = 実装済み
- ◇ = 将来仕様（設計済み）

---

## クイックナビゲーション

| 読みたい人 | 推奨ドキュメント |
|------------|-----------------|
| **UMIを初めて使う** | [specs/ARCHITECTURE.md](specs/ARCHITECTURE.md) |
| **アプリを作りたい** | [guides/APPLICATION.md](guides/APPLICATION.md) → [reference/](reference/) |
| **BSPを作りたい** | [reference/API_BSP.md](reference/API_BSP.md) → [reference/API_KERNEL.md](reference/API_KERNEL.md) |
| **UMI自体を開発** | [specs/](specs/) → [development/](development/) |

---

## ディレクトリ構成

```
docs/
├── README.md                    # このファイル（目次）
│
├── guides/                      # ガイド
│   ├── APPLICATION.md           # アプリケーション実装ガイド
│   └── TESTING.md               # テスト戦略
│
├── reference/                   # APIリファレンス
│   ├── API_APPLICATION.md       # アプリケーションAPI
│   ├── API_UI.md                # UI API（Input/Output/Canvas）
│   ├── API_BSP.md               # BSP I/O型定義
│   ├── API_DSP.md               # DSPモジュール
│   ├── API_KERNEL.md            # Kernel API（syscall等）
│   └── UMIDSP_GUIDE.md          # DSPライブラリガイド
│
├── specs/                       # 仕様書
│   ├── NOMENCLATURE.md          # 命名体系（用語定義）
│   ├── ARCHITECTURE.md          # アーキテクチャ概要
│   ├── UMIP.md                  # Processor仕様
│   ├── UMIC.md                  # Controller仕様
│   ├── UMIM.md                  # バイナリ形式仕様
│   ├── SECURITY.md              # セキュリティ分析
│   ├── CONCEPTS.md              # C++20 Concepts設計
│   └── UMIM_NATIVE_SPEC.md      # ネイティブバイナリ仕様
│
├── development/                 # 開発者向け
│   ├── CODING_STYLE.md          # コーディングスタイル
│   ├── LIBRARY_PACKAGING.md     # ライブラリ構造
│   ├── SIMULATION.md            # シミュレーションバックエンド
│   ├── DEBUG_GUIDE.md           # デバッグガイド
│   ├── IMPLEMENTATION_PLAN.md   # 実装計画
│   └── USB_AUDIO_REDESIGN_PLAN.md # USB Audio設計
│
└── rust/                        # Rust関連
    └── RUST.md                  # Rust実装メモ
```

---

## 主要ドキュメント

### アーキテクチャ

- **[specs/ARCHITECTURE.md](specs/ARCHITECTURE.md)** - UMIの設計思想、統一main()モデル、Processor/Control Task、実装状況

### API リファレンス

| ドキュメント | 内容 | 状態 |
|-------------|------|------|
| [reference/API_APPLICATION.md](reference/API_APPLICATION.md) | `main()`, `register_processor()`, `wait_event()` | ✓ |
| [reference/API_KERNEL.md](reference/API_KERNEL.md) | Syscall、IRQ、MPU、SpscQueue | ✓ |
| [reference/API_DSP.md](reference/API_DSP.md) | オシレータ、フィルタ、エンベロープ | ✓ |
| [reference/UMIDSP_GUIDE.md](reference/UMIDSP_GUIDE.md) | DSPライブラリ使用ガイド | ✓ |
| [reference/API_UI.md](reference/API_UI.md) | `Input`, `Output`, `Canvas` | ◇ |
| [reference/API_BSP.md](reference/API_BSP.md) | `HwType`, `ValueType`, `Curve` | ◇ |

### 仕様書

| ドキュメント | 内容 | 状態 |
|-------------|------|------|
| [specs/NOMENCLATURE.md](specs/NOMENCLATURE.md) | **命名体系**（用語定義、命名規則） | ✓ |
| [specs/UMIP.md](specs/UMIP.md) | UMI-Processor（DSP処理ユニット）仕様 | ✓ |
| [specs/UMIM.md](specs/UMIM.md) | UMI-Module バイナリ形式（.umia） | ✓ |
| [specs/SECURITY.md](specs/SECURITY.md) | セキュリティリスク分析、MPU保護 | ✓ |
| [specs/CONCEPTS.md](specs/CONCEPTS.md) | C++20 Concepts設計 | ✓ |
| [specs/UMIC.md](specs/UMIC.md) | UMI-Controller（Control Task）仕様 | ◇ |

### ガイド

| ドキュメント | 内容 |
|-------------|------|
| [guides/APPLICATION.md](guides/APPLICATION.md) | ユースケース別実装例（シンセ、エフェクト等） |
| [guides/TESTING.md](guides/TESTING.md) | テスト戦略（Host/WASM/Renode） |

### 開発者向け

| ドキュメント | 内容 |
|-------------|------|
| [development/CODING_STYLE.md](development/CODING_STYLE.md) | コーディングスタイル（clang-format設定） |
| [development/LIBRARY_PACKAGING.md](development/LIBRARY_PACKAGING.md) | ライブラリ構造規約 |
| [development/SIMULATION.md](development/SIMULATION.md) | WASM/Renodeシミュレーション |
| [development/DEBUG_GUIDE.md](development/DEBUG_GUIDE.md) | デバッグガイド |
| [development/IMPLEMENTATION_PLAN.md](development/IMPLEMENTATION_PLAN.md) | 実装計画と進捗 |

---

## 変更履歴

| バージョン | 日付 | 変更内容 |
|------------|------|----------|
| 0.10.0 | 2025-01-24 | 非特権アプリ実行、RTOS、Ed25519、ドキュメント再編 |
| 3.0.0-draft | 2025-01 | 統一main()モデル、UMIP/UMIC/UMIM仕様 |
| 2.0.0 | 2025-01 | パラメータをメンバ変数化、Controller任意化 |
