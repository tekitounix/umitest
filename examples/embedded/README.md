# UMI-OS Embedded Examples

組み込み（ARM Cortex-M）向けのサンプルコード。

## ファイル

| ファイル | 説明 |
|----------|------|
| `example_app.cc` | Processor + Coroutine + Event の統合サンプル |
| `vector_table_example.cc` | 動的ベクターテーブルの使用例（RAM配置） |

## ビルド

```bash
# STM32F4 ターゲット
xmake build firmware
```

## 実行

Renode エミュレータまたは実機で実行。

```bash
# Renode テスト
xmake run renode_test
```
