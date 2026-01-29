# ストレージ・ファイルシステム設計

## 前提

- PCとのデータやり取りは基本的にSysEx経由（USB MIDI）
- .umiuファイル（UMI-UNIT）やサンプルデータの保存にはファイルシステムが必要
- 電源遮断耐性が必須（書き込み中の電断でデータが壊れてはならない）
- 将来的にSDカード経由でのPC直接読み書きも考慮する

## 階層構造

```
App (非同期syscall: fs::open/read/write/close → 即return → WaitEvent(FS)で完了取得)
  ↓
StorageService (SystemTask内、要求キューから逐次処理)
  ↓
ファイルシステム (littlefs / FATfs)
  ↓
BlockDevice (erase: DMA+WaitEvent、read/prog: busy wait)
  ↓
┌──────────────┬──────────────┬──────────────┬──────────────┐
│ InternalFlash│ SPI Flash    │ FRAM/EEPROM  │ SD (SPI/SDIO)│
└──────────────┴──────────────┴──────────────┴──────────────┘
```

## BlockDevice インターフェース

```cpp
struct BlockDevice {
    uint32_t block_size;      // 最小消去単位（Flash: 4KB-128KB, FRAM: 1byte, SD: 512B）
    uint32_t write_size;      // 最小書き込み単位（Flash: 1-256B, FRAM: 1B）
    uint32_t total_size;
    bool needs_erase;         // Flash系: true, FRAM/EEPROM: false

    // SystemTask内で呼ぶ（呼び出し元から見ると同期的）
    int32_t read(uint32_t addr, void* buf, uint32_t len);
    int32_t write(uint32_t addr, const void* buf, uint32_t len);
    int32_t erase(uint32_t addr, uint32_t len);  // needs_erase==trueの場合のみ
};
```

### BlockDevice内部のブロッキング方式

BlockDeviceのAPIは呼び出し元（littlefs）から見ると同期的だが、内部の待ち方は操作ごとに異なる:

| SPI操作 | 待ち時間 | 待ち方式 | 理由 |
|---------|---------|---------|------|
| read | 数十μs | busy wait | コンテキストスイッチ（数μs）に対して待ちが短すぎる |
| prog (page write) | 数百μs〜1ms | busy wait | UIフレーム（16ms）に対して十分短い |
| erase (sector) | 数ms〜数十ms | DMA + WaitEvent | 明確にCPU浪費。Blocked化してControlTaskにCPUを渡す |

erase待ち中の流れ:

```
SystemTask: SPI eraseコマンド発行 → WaitEvent(SpiComplete) → Blocked
  → ControlTaskにCPU渡る（UIアニメーション等）
SPI完了IRQ: notify(SystemTask, SpiComplete)
SystemTask: Resume → littlefs続行
```

littlefsのコールバック構造上、erase()コールバック内でWaitEventしても問題ない。
littlefsの内部状態はスタック上に保持されており、SystemTaskは1つなのでリエントランシーの問題もない。

eraseのBlocked化によるオーバーヘッド: コンテキストスイッチ2回（数μs×2）。
erase待ち時間（数ms〜数十ms）に対して0.1%以下。無視できる。

## アプリからのアクセス

FS syscallは**非同期一本化**。要求発行と完了取得を分離する:

```
App: fs::write(fd, data, len) → SVC → 要求キューに積む → 即return
  → アプリはUIアニメーション等を継続可能
  → 完了を待つ場合: wait_event(event::FS) → Blocked → 完了通知で復帰
```

### 非同期一本化の理由

- syscallは最小プリミティブであるべき。同期/非同期の選択はアプリ層の判断事項
- ControlTaskはUI/表示も担う。同期syscallだとFS操作中にUIがフリーズする
- 同期的に使いたい場合はアプリ側ユーティリティで「発行+wait」をまとめればよい
- syscallレベルで同期/非同期の2形態を持つ意味がない（同期は非同期のラッパーにすぎない）

### 全体の流れ

```
ControlTask: fs::write() → SVC → 要求キュー → 即return → UIアニメーション継続
SystemTask:  要求受信 → littlefs → BlockDevice::erase()
  → SPI eraseコマンド発行 → WaitEvent(SpiComplete) → Blocked
ControlTask: CPU取得 → UIアニメーション更新
SPI完了IRQ: notify(SystemTask, SpiComplete)
SystemTask:  Resume → littlefs続行 → ... → 完了 → notify(ControlTask, FS)
ControlTask: WaitEvent(FS)で結果取得
```

どのレイヤーにもbusy wait（erase以外のread/progは短時間のため許容）がなく、
長時間の待ちはすべてBlocked（CPU解放）で処理される。

## ファイルシステム選択

### Flash系（内蔵Flash / SPI Flash）: littlefs

| 特性 | 内容 |
|------|------|
| 電源遮断耐性 | COW（Copy-on-Write）方式。書き込み中の電断でもデータ破損しない |
| ウェアレベリング | 動的ウェアレベリング内蔵 |
| RAM使用量 | 小（バッファ数個分） |
| BlockDevice互換 | read/prog/erase の3関数で接続。上記BlockDeviceとほぼ同形 |

littlefsはFlash向けに設計されており、電断耐性とウェアレベリングを標準で備える。
.umiuファイル、パラメータ、サンプルデータすべてをファイルとして統一的に管理できる。

### SDカード: FATfs

SDカードをPCで直接読み書きしたい場合はFAT形式が必須。

| 特性 | 内容 |
|------|------|
| PC互換性 | FAT32。PCでそのまま読み書き可能 |
| 電源遮断耐性 | FAT自体にはない。ジャーナリングなし |
| 用途 | サンプルデータの大量転送、.umiuの配布 |

### 併用構成

```
内蔵/SPI Flash → littlefs（電断耐性、ウェアレベリング）
  用途: パラメータ、プリセット、.umiu、小規模サンプル

SDカード → FATfs（PC互換）
  用途: 大容量サンプル、バックアップ、.umiu配布
```

両方ともBlockDevice上に構築されるため、StorageServiceは透過的にアクセスできる。
パスで区別:

```
/flash/modules/osc_saw.umiu     → littlefs (SPI Flash)
/flash/presets/default.json      → littlefs (SPI Flash)
/sd/samples/kick.wav             → FATfs (SD)
/sd/modules/fx_delay.umiu        → FATfs (SD)
```

## 電源遮断耐性

### littlefs（Flash系）

COW方式により、書き込み途中で電断しても:
- 旧データは消去前にコピー済み → 旧データが残る
- メタデータはアトミックに更新 → 中途半端な状態にならない

追加対策不要。littlefsの標準動作で保証される。

### FATfs（SD）

FAT自体に電断耐性はない。対策:

- **書き込み後は即close/sync** — 長時間openしない
- **重要データはFlash（littlefs）に置く** — SDは「あると便利」な位置づけ
- **SDからFlashへのインポート** — .umiuをSDからlittlefsにコピーして使用

## PCとのやりとり

| 方式 | 経路 | FS形式 | 用途 |
|------|------|--------|------|
| **SysEx転送** | USB MIDI → SystemTask → littlefs | littlefs | 基本。.umiu、プリセット、小規模サンプル |
| **SDカード** | PC → SD → FATfs | FAT32 | 大容量サンプル、一括配布 |
| **SysEx DFU** | USB MIDI → SystemTask → Flash直接 | — | ファームウェア/アプリ更新 |

SysExが基本経路。SDは大容量データや、PCで直接ファイルを扱いたい場合の補助。

## メディア別特性

| メディア | block_size | write_size | needs_erase | 寿命 | FS | 備考 |
|----------|-----------|-----------|-------------|------|-----|------|
| 内蔵Flash (STM32F4) | 16KB-128KB | 1-4B | true | ~10K回 | littlefs | セクタサイズ不均一 |
| SPI Flash (W25Q) | 4KB | 256B (page) | true | ~100K回 | littlefs | 均一セクタ |
| FRAM (FM25V) | 1B | 1B | false | 10^12回 | — | KVS直接。FS不要 |
| EEPROM (I2C) | 1B | 32-128B (page) | false | ~1M回 | — | KVS直接。FS不要 |
| SD (SPI/SDIO) | 512B | 512B | false* | — | FATfs | PC互換必要 |

FRAM/EEPROMは容量が小さく（数KB〜数百KB）、FSのオーバーヘッドが不釣り合い。
キーバリューストア（固定サイズスロット or 簡易ログ構造）で直接管理する。

## 実装方針

### リファレンスソースの管理

littlefs、FATfsともにオリジナルソースを `.refs/` にサブモジュールとしてクローンし、
必要なソースを `lib/` 以下にコピーしてumiのコーディングスタイルに書き直して使用する。

```
.refs/
  littlefs/              ← git submodule (リファレンス)
  fatfs/                 ← git submodule (リファレンス)

lib/umios/storage/
  block_device.hh        ← BlockDeviceインターフェース（FS下層）
  storage_service.hh     ← StorageService（FS上層、SystemTask内）
  fs/
    lfs/                 ← littlefsをumiスタイルに書き直したもの
    fat/                 ← FATfsをumiスタイルに書き直したもの
```

`block_device.hh`と`storage_service.hh`はFS実装ではなく、それぞれFSの下層・上層にあたるため`storage/`直下に配置。
FS実装は`fs/`サブディレクトリにまとめることで、レイヤーの違いが構造に反映される。

### 書き直しの理由

- umiのコーディングスタイル（C++23、lower_case関数名、CamelCaseクラス等）に統一
- 不要な機能を削除しコンパクト化
- BlockDeviceインターフェースに合わせた接続
- オリジナルのC APIをC++に変換

## アプリからの使用例

### syscallは非同期（要求発行 → 即return）

アプリはsyscall経由でファイルとしてアクセスする。BlockDeviceやFSを直接触ることはない。
各syscallは要求を発行して即returnする。完了を待つ場合は`wait_event(event::FS)`を使う。

```cpp
#include <umios/app/syscall.hh>

using namespace umi::syscall;

// --- パラメータ保存（非同期syscall + 完了待ち） ---
struct SynthParams {
    float cutoff;
    float resonance;
    float attack;
    float release;
};

void save_params(const SynthParams& params) {
    fs::open("/flash/presets/current.dat", fs::WRONLY | fs::CREAT);
    wait_event(event::FS);  // open完了待ち
    int32_t fd = fs::result();  // 結果取得
    if (fd < 0) return;

    fs::write(fd, &params, sizeof(params));
    wait_event(event::FS);

    fs::close(fd);
    wait_event(event::FS);  // close時にlittlefsがアトミックにコミット → 電断安全
}
```

### 同期的ユーティリティ

毎回`wait_event`を書くのは冗長なので、アプリ側ライブラリで同期ラッパーを提供:

```cpp
// アプリ側ユーティリティ（syscallではない）
namespace umi::fs {
    int32_t open_sync(const char* path, uint32_t flags) {
        syscall::fs::open(path, flags);
        syscall::wait_event(event::FS);
        return syscall::fs::result();
    }
    int32_t write_sync(int32_t fd, const void* buf, uint32_t len) {
        syscall::fs::write(fd, buf, len);
        syscall::wait_event(event::FS);
        return syscall::fs::result();
    }
    void close_sync(int32_t fd) {
        syscall::fs::close(fd);
        syscall::wait_event(event::FS);
    }
}
```

同期ラッパーを使えばコードは簡潔になる:

```cpp
using namespace umi::fs;

void save_params(const SynthParams& params) {
    int32_t fd = open_sync("/flash/presets/current.dat", fs::WRONLY | fs::CREAT);
    if (fd < 0) return;
    write_sync(fd, &params, sizeof(params));
    close_sync(fd);
}

bool load_params(SynthParams& params) {
    int32_t fd = open_sync("/flash/presets/current.dat", fs::RDONLY);
    if (fd < 0) return false;
    int32_t n = read_sync(fd, &params, sizeof(params));
    close_sync(fd);
    return n == sizeof(params);
}
```

### 非同期の利点: UI継続

FS操作中もUIアニメーションを継続したい場合:

```cpp
void save_params_async(const SynthParams& params) {
    fs::open("/flash/presets/current.dat", fs::WRONLY | fs::CREAT);
    // FS完了を待たずUIループを回す
    while (true) {
        uint32_t ev = wait_event(event::FS | event::VSync);
        if (ev & event::VSync) update_display();  // アニメーション継続
        if (ev & event::FS) break;                // FS完了
    }
    int32_t fd = fs::result();
    if (fd < 0) return;

    fs::write(fd, &params, sizeof(params));
    while (true) {
        uint32_t ev = wait_event(event::FS | event::VSync);
        if (ev & event::VSync) update_display();
        if (ev & event::FS) break;
    }

    fs::close(fd);
    while (true) {
        uint32_t ev = wait_event(event::FS | event::VSync);
        if (ev & event::VSync) update_display();
        if (ev & event::FS) break;
    }
}
```

### SDカードからのインポート

```cpp
// SDカード上の.umiuをFlash（littlefs）にコピー（同期ラッパー使用）
void import_umiu(const char* name) {
    char src[64], dst[64];
    snprintf(src, sizeof(src), "/sd/modules/%s", name);
    snprintf(dst, sizeof(dst), "/flash/modules/%s", name);

    int32_t fd_src = open_sync(src, fs::RDONLY);
    int32_t fd_dst = open_sync(dst, fs::WRONLY | fs::CREAT);
    if (fd_src < 0 || fd_dst < 0) {
        if (fd_src >= 0) close_sync(fd_src);
        if (fd_dst >= 0) close_sync(fd_dst);
        return;
    }

    uint8_t buf[512];
    int32_t n;
    while ((n = read_sync(fd_src, buf, sizeof(buf))) > 0) {
        write_sync(fd_dst, buf, n);
    }
    close_sync(fd_dst);
    close_sync(fd_src);
}
```

### 注意点

- アプリはBlockDeviceやFSを直接操作しない。すべてsyscall（`fs::open`/`read`/`write`/`close`）経由
- FS syscallは非同期。要求発行→即return→`wait_event(event::FS)`で完了取得
- 同期的に使いたい場合はアプリ側ユーティリティ（`open_sync`等）で対応
- パス先頭の`/flash/`と`/sd/`でStorageServiceが自動的にlittlefs/FATfsを振り分ける
- `fs::close()`がlittlefsのアトミックコミットをトリガーするため、電断耐性は自動的に確保される
