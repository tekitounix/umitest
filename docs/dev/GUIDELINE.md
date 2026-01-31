# C++実装仕様

## 1. データ構造の分離 (Data/State Separation)
「共有データ(Arc)」と「占有データ(Owner)」の分離。

### 実装パターン
クラスへのカプセル化（OOP）を避け、データのライフサイクルに基づいて構造体を物理的に分割する。

```cpp
#include <array>
#include <atomic>
#include <cstdint>

// 1. パラメータ (Immutable / Shared)
// 全ボイスで共有される計算係数
struct FilterCoeffs {
    float a0, a1, b1, b2;
    float cutoff;
};

// 2. 状態 (Mutable / Owned)
// ボイスごとに個別に確保されるバッファ
struct FilterState {
    float z1 = 0.0f;
    float z2 = 0.0f;
};
```

---

## 2. 共有リソースの管理 (Double Buffering)
Rustの `Arc<T>` によるスレッド間共有を、アトミックインデックスを用いたダブルバッファリングで再現する。

### 実装パターン
2面の静的バッファを用意し、読み取り(DSP)と書き込み(UI)のアクセス先をインデックスで分離することで、ロックフリーなデータ共有を実現する。

```cpp
class PolySynth {
    // 共有パラメータプール (2面)
    std::array<FilterCoeffs, 2> sharedPool;
    
    // 現在DSPが参照しているインデックス (0 or 1)
    std::atomic<uint8_t> activeIndex { 0 };

    // 各ボイスの内部状態 (固定数)
    std::array<FilterState, 8> voices;

public:
    // --- DSP Thread (Reader) ---
    void process(float* buffer, size_t n) {
        // インデックスのアトミックロード (Wait-free)
        uint8_t idx = activeIndex.load(std::memory_order_acquire);
        
        // const参照として取得 (書き換え禁止を強制)
        const auto& coeffs = sharedPool[idx];

        for (size_t i=0; i<n; ++i) {
            float in = buffer[i];
            for (auto& v : voices) {
                // coeffsはconstのため、計算のみに使用
                float out = in * coeffs.a0 + v.z1 * coeffs.b1;
                v.z1 = out; // 状態のみ更新
            }
        }
    }

    // --- UI Thread (Writer) ---
    void updateParams(float newCutoff) {
        // 裏バッファのインデックスを算出
        uint8_t current = activeIndex.load(std::memory_order_relaxed);
        uint8_t next = current ^ 1;

        // 裏バッファへの書き込み (競合なし)
        sharedPool[next].cutoff = newCutoff;
        sharedPool[next].a0 = /* 係数再計算 */;

        // インデックスのアトミック更新 (公開)
        activeIndex.store(next, std::memory_order_release);
    }
};
```

---

## 3. オブジェクト参照の管理 (Generational Arena)
Rustのライフタイム管理とダングリングポインタ対策を、世代付きインデックス（Handle）で再現する。

### 実装パターン
生ポインタの保持を禁止し、`index` と `generation` を持つハンドル構造体を通じてオブジェクトへアクセスする。

```cpp
// 参照用ハンドル
struct Handle {
    uint16_t index;      // 配列添字
    uint16_t generation; // 世代ID
};

// 静的アリーナコンテナ
template <typename T, size_t MAX_SIZE>
class StaticArena {
    struct Slot {
        T data;
        uint16_t generation = 0;
        bool active = false;
    };
    
    std::array<Slot, MAX_SIZE> pool;

public:
    // 生成 (Allocation)
    Handle spawn() {
        for (size_t i = 0; i < MAX_SIZE; ++i) {
            if (!pool[i].active) {
                pool[i].active = true;
                pool[i].generation++;
                return Handle { (uint16_t)i, pool[i].generation };
            }
        }
        return { 0, 0 }; // 枯渇エラー
    }

    // 削除 (Deallocation)
    void kill(Handle h) {
        if (h.index < MAX_SIZE && pool[h.index].generation == h.generation) {
            pool[h.index].active = false;
        }
    }

    // 参照解決 (Dereference)
    // 世代不一致時はnullptrを返し、不正アクセスを防ぐ
    T* get(Handle h) {
        if (h.index >= MAX_SIZE) return nullptr;
        Slot& s = pool[h.index];
        
        if (s.active && s.generation == h.generation) {
            return &s.data;
        }
        return nullptr;
    }
};
```

---

## 4. 構造変更と通信 (SPSC Ring Buffer)

### 実装パターン
`std::vector`等の可変長コンテナ操作を避け、コマンドキューを通じて処理依頼を行う。

```cpp
struct Command {
    enum Type { NOTE_ON, NOTE_OFF, PARAM_SET };
    uint8_t targetIdx;
    float value;
};

template <typename T, size_t SIZE>
class LockFreeRingBuffer {
    std::array<T, SIZE> buffer;
    std::atomic<size_t> head {0}, tail {0};

public:
    // Writer (UI)
    bool push(const T& item) {
        size_t t = tail.load(std::memory_order_relaxed);
        size_t next = (t + 1) % SIZE;
        if (next == head.load(std::memory_order_acquire)) return false;
        buffer[t] = item;
        tail.store(next, std::memory_order_release);
        return true;
    }

    // Reader (DSP)
    bool pop(T& item) {
        size_t h = head.load(std::memory_order_relaxed);
        if (h == tail.load(std::memory_order_acquire)) return false;
        item = buffer[h];
        head.store((h + 1) % SIZE, std::memory_order_release);
        return true;
    }
};
```