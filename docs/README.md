# UMI ドキュメント

UMI (Universal Musical Instruments) プラットフォームのドキュメントです。

---

## クイックナビゲーション

| 読みたい人 | 推奨ドキュメント |
|------------|-----------------|
| **UMIを初めて使う** | [getting-started/](getting-started/) → [specs/ARCHITECTURE.md](specs/ARCHITECTURE.md) |
| **アプリを作りたい** | [guides/APPLICATION.md](guides/APPLICATION.md) → [reference/](reference/) |
| **BSPを作りたい** | [reference/API_BSP.md](reference/API_BSP.md) → [reference/API_KERNEL.md](reference/API_KERNEL.md) |
| **UMI自体を開発** | [specs/](specs/) → [development/](development/) |

---

## ディレクトリ構成

```
docs/
├── README.md                    # このファイル（目次）
│
├── getting-started/             # 入門
│   ├── QUICKSTART.md            # 5分で始めるUMI（準備中）
│   └── CONCEPTS.md              # 基本概念（準備中）
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
│   └── API_KERNEL.md            # Kernel API
│
├── specs/                       # 仕様書
│   ├── ARCHITECTURE.md          # アーキテクチャ概要
│   ├── UMIP.md                  # Processor仕様
│   ├── UMIC.md                  # Controller仕様
│   ├── UMIM.md                  # バイナリ形式仕様
│   └── SECURITY.md              # セキュリティ分析
│
├── development/                 # 開発者向け
│   ├── CODING_STYLE.md          # コーディングスタイル
│   ├── LIBRARY_PACKAGING.md     # ライブラリ構造
│   └── SIMULATION.md            # シミュレーションバックエンド
│
└── archived/                    # アーカイブ
    └── pre-reorganization/      # 整理前のバックアップ
```

---

## 主要ドキュメント

### アーキテクチャ

- **[specs/ARCHITECTURE.md](specs/ARCHITECTURE.md)** - UMIの設計思想、統一main()モデル、Processor/Control Task

### API リファレンス

| ドキュメント | 内容 |
|-------------|------|
| [reference/API_APPLICATION.md](reference/API_APPLICATION.md) | `main()`, `register_processor()`, `wait_event()`, コルーチン |
| [reference/API_UI.md](reference/API_UI.md) | `Input`, `Output`, `Canvas`, 共有メモリモデル |
| [reference/API_BSP.md](reference/API_BSP.md) | `HwType`, `ValueType`, `Curve`, ファクトリ関数 |
| [reference/API_DSP.md](reference/API_DSP.md) | オシレータ、フィルタ、エンベロープ |
| [reference/API_KERNEL.md](reference/API_KERNEL.md) | Syscall、IRQ、優先度、SpscQueue |

### 仕様書

| ドキュメント | 内容 |
|-------------|------|
| [specs/UMIP.md](specs/UMIP.md) | UMI-Processor（DSP処理ユニット）仕様 |
| [specs/UMIC.md](specs/UMIC.md) | UMI-Controller（Control Task）仕様 |
| [specs/UMIM.md](specs/UMIM.md) | UMI-Module バイナリ形式（.umim / .umiapp） |
| [specs/SECURITY.md](specs/SECURITY.md) | セキュリティリスク分析、MPU保護 |

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

---

## 旧ドキュメントからの対応表

| 旧ファイル | 新ファイル |
|-----------|-----------|
| API.md | このREADME.md |
| API_UI.md | reference/API_UI.md + reference/API_BSP.md |
| API_APPLICATION.md | reference/API_APPLICATION.md |
| API_DSP.md | reference/API_DSP.md |
| API_KERNEL.md | reference/API_KERNEL.md |
| ARCHITECTURE.md | specs/ARCHITECTURE.md |
| UMIP_SPEC.md | specs/UMIP.md |
| UMIC_SPEC.md | specs/UMIC.md |
| UMIM_SPEC.md | specs/UMIM.md |
| SECURITY_ANALYSIS.md | specs/SECURITY.md |
| USE_CASES.md | guides/APPLICATION.md |
| TEST_STRATEGY.md | guides/TESTING.md |
| CODING_STYLE.md | development/CODING_STYLE.md |
| LIBRARY_PACKAGING.md | development/LIBRARY_PACKAGING.md |
| SIMULATION_BACKENDS.md | development/SIMULATION.md |

---

## 整理計画

詳細な整理計画は [DOC_REORGANIZATION_PLAN.md](DOC_REORGANIZATION_PLAN.md) を参照してください。
