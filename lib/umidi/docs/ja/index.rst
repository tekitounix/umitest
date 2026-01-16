umidi ドキュメント
==================

umidiは、ARM Cortex-Mマイクロコントローラ向けに最適化された
高性能MIDIライブラリです。

特徴
----

- **UMP-Opt形式**: 単一のuint32_tで効率的な比較が可能
- **ゼロアロケーション**: 事前確保バッファのみで動作
- **型安全メッセージ**: コンパイル時の型チェック
- **MIDI 1.0 & 2.0**: UMPによる両プロトコル対応
- **組込み優先**: 制約環境向け設計

クイックスタート
----------------

.. code-block:: cpp

   #include <umidi/umidi.hh>

   umidi::Parser parser;
   umidi::UMP32 ump;

   void process_byte(uint8_t byte) {
       if (parser.parse(byte, ump)) {
           if (ump.is_note_on()) {
               handle_note_on(ump.note(), ump.velocity());
           }
       }
   }

.. toctree::
   :maxdepth: 2
   :caption: 目次:

   README
   design
   examples

索引
====

* :ref:`genindex`
* :ref:`search`
