# UMI-SysEx 概念モデル整理（ドラフト）

バージョン: 0.1.0 (Draft)
ステータス: 設計段階
最終更新: 2026-01-29

---

## 1. 目的

UMI-SysEx の**概念モデル**を明確化し、
データ構造と実装提案を分離して整理する。

---

## 2. 概念モデル（何が何を持つか）

### 2.1 Step（ステップ）

```
Step = values[]
```

- **純粋な値の配列**であり、IDを持たない
- 各要素の意味は外側の Pattern が決める
- Step 単体では意味を持たないデータ

**ポイント**
- Dense の場合: Step の index が位置
- Sparse の場合: values[0] が POSITION となり、位置は値で表現される

**対応する構造体**: なし（Pattern 内の配列要素として格納）

**ファイル交換用ラッパー**: `StepFile`

```c
struct StepFile {
    uint8_t  magic[4];          // "UXST"
    uint16_t version;
    uint8_t  standard_def_id;   // StandardDefId (0=custom)
    uint8_t  param_count;       // パラメータ数
    uint32_t total_size;
    uint32_t crc32;
    uint16_t step_index;        // 対象ステップのインデックス
    uint16_t reserved;
    ParamDef defs[param_count]; // standard_def_id == 0 の場合のみ
    uint16_t values[param_count]; // ステップデータ
};
```

**用途**
- 特定ステップの外部編集（エディタから1ステップだけ送信）
- パターン内の部分更新
- ステップ単位のコピー＆ペースト

---

### 2.2 Pattern（パターン）★フレーズ交換単位

```
Pattern = StepSequence + ParameterSetDefinition
        = Step[] + ParamDef[]
```

- **StepSequence**: Step の配列（純粋データ）
- **ParameterSetDefinition**: Step の values[] の順序と意味を決める定義
  - ParamDef[] そのもの、または StandardDefId による参照

**役割**
1. ステップデータを保持する
2. ステップに意味を与える（パラメータとの対応を明示）
3. **交換可能な最小単位**として自己完結する

**Dense と Sparse**

| 表現 | 特徴 |
|------|------|
| Dense | index = 位置、全ステップを持つ |
| Sparse | values[0] = POSITION、イベント列のみ持つ |

**Pattern が持つもの**
- ParamDef[] または StandardDefId（ステップの意味）
- Step[]（データ）
- length_steps（論理長）
- sparse フラグ
- preset_ref（音色参照、オプション）

**Pattern が持たないもの**
- テンポ → Song の責務
- 拍子 → Song の責務
- MIDIチャンネル → Track の責務

**対応する構造体**: `Pattern`

```c
struct Pattern {
    uint8_t  standard_def_id;   // StandardDefId (0=custom)
    uint8_t  param_count;       // パラメータ数
    uint16_t flags;             // bit0: sparse
    uint16_t length_steps;      // 論理長（パターンが表すステップ範囲）
    uint16_t step_count;        // 実際のステップ数
    uint16_t preset_ref;        // Preset への参照 (0xFFFF=なし、Track の preset_ref をオーバーライド)
    ParamDef defs[param_count]; // standard_def_id == 0 の場合のみ
    uint16_t steps[step_count][param_count]; // 全て16bit固定
};
```

**ファイル交換用ラッパー**: `PatternFile`

```c
struct PatternFile {
    uint8_t  magic[4];          // "UXPN"
    uint16_t version;
    uint16_t reserved;
    uint32_t total_size;
    uint32_t crc32;
    Pattern  pattern;           // 本体
};
```

---

### 2.3 Track（トラック）

```
Track = PatternReference[] + 再生属性
      = [Pattern0, Pattern1, Pattern2, ...] + 属性
```

- **PatternReference[]**: どの Pattern をどの順番で再生するかの参照配列
- **再生属性**: MIDIチャンネル、音色、ミュート状態など

**役割**
- 複数の Pattern の**再生順序**を決める
- Pattern そのものは持たない（参照のみ）
- 音色・チャンネルなどの固定属性を持つ

**対応する構造体**: `TrackEntry` + `PatternRef`

```c
struct TrackEntry {
    uint8_t  track_id;
    uint8_t  midi_channel;      // 0-15
    uint16_t flags;             // bit0: muted, bit1: solo
    uint16_t pattern_ref_count; // 参照するパターン数
    uint16_t preset_ref;        // Preset への参照 (0xFFFF=なし)
    uint8_t  time_scale;        // クロック倍率 (0x40=1x)
    uint8_t  direction;         // PlayDirection
    uint16_t reserved;
    uint32_t pattern_refs_offset; // PatternRef[] へのオフセット
};

struct PatternRef {
    uint16_t pattern_id;        // PatternPool 内の ID
    uint16_t repeat_count;      // 繰り返し回数 (0=無限)
    uint16_t start_step;        // 開始ステップ (0=先頭から)
    uint16_t reserved;
};
```

---

### 2.4 Song（ソング）

```
Song = Track[] + グローバル属性
```

- **Track[]**: トラックの配列
- **グローバル属性**: テンポ、拍子、セクション配置など

**役割**
- 複数の Track を束ねる
- 楽曲全体の構造を定義する

**対応する構造体**: `SongHeader`

```c
struct SongHeader {
    uint8_t  magic[4];          // "UXSG"
    uint16_t version;
    uint8_t  track_count;
    uint8_t  flags;
    uint32_t tempo_x100;        // デフォルトテンポ × 100
    uint16_t time_sig_num;
    uint16_t time_sig_den;
    uint32_t track_dir_offset;  // TrackEntry[] へのオフセット
    uint32_t tempo_map_offset;  // テンポマップへのオフセット (0=なし)
    uint32_t total_size;
    uint32_t crc32;
    uint8_t  name_length;
    uint8_t  reserved[3];
};
```

---

### 2.5 Project（プロジェクト）

```
Project = Song[] + SharedPools
```

- **Song[]**: ソングへの参照
- **SharedPools**: 独立交換可能な資源プール

**SharedPools の構成**
- PatternPool: 共有パターン
- PresetPool: 共有プリセット
- SamplePool: 共有サンプル
- MappingPool: 共有マッピング

**役割**
- 複数の Song を管理
- 資源の共有と参照管理

**対応する構造体**: `ProjectHeader` + `PoolHeader` + `PoolEntry`

```c
struct ProjectHeader {
    uint8_t  magic[4];          // "UXPJ"
    uint16_t version;
    uint8_t  song_count;
    uint8_t  flags;
    uint16_t default_preset_ref; // デフォルト Preset
    uint16_t reserved;
    uint32_t song_dir_offset;    // SongRef[] へのオフセット
    uint32_t pattern_pool_offset;
    uint32_t preset_pool_offset;
    uint32_t sample_pool_offset; // 0=なし
    uint32_t mapping_pool_offset;// 0=なし
    uint32_t total_size;
    uint32_t crc32;
    uint8_t  name_length;
    uint8_t  reserved2[3];
};

struct PoolHeader {
    uint8_t  magic[4];          // "UXPL"
    uint8_t  resource_type;     // ResourceType
    uint8_t  reserved;
    uint16_t entry_count;
    uint32_t entries_offset;    // PoolEntry[] へのオフセット
    uint32_t total_size;
};

struct PoolEntry {
    uint16_t id;                // Pool 内 ID
    uint16_t flags;
    uint32_t data_offset;       // 実データへのオフセット
    uint32_t data_size;
};
```
