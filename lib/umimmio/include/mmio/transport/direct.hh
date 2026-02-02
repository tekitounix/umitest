#pragma once

#include "../mmio.hh"  // RegOpsとDirectTransportTagが必要
#include <concepts>

namespace mm {

/// @brief DirectTransport - memory-mapped I/O
template <typename CheckPolicy>
class DirectTransport : private RegOps<DirectTransport<CheckPolicy>, CheckPolicy> {
    friend class RegOps<DirectTransport<CheckPolicy>, CheckPolicy>;
public:
    using RegOps<DirectTransport<CheckPolicy>, CheckPolicy>::write;
    using RegOps<DirectTransport<CheckPolicy>, CheckPolicy>::read;
    using RegOps<DirectTransport<CheckPolicy>, CheckPolicy>::modify;
    using RegOps<DirectTransport<CheckPolicy>, CheckPolicy>::is;
    using RegOps<DirectTransport<CheckPolicy>, CheckPolicy>::flip;
    using TransportTag = DirectTransportTag;

    template <typename Reg>
    auto reg_read(Reg /*reg*/) const noexcept -> typename Reg::RegValueType {
        using T = typename Reg::RegValueType;
        return *reinterpret_cast<volatile const T*>(Reg::address);
    }

    template <typename Reg>
    void reg_write(Reg /*reg*/, typename Reg::RegValueType value) const noexcept {
        using T = typename Reg::RegValueType;
        
        // Alignment check (compile-time controlled)
        if constexpr (CheckPolicy::value) {
            static_assert((Reg::address % alignof(T)) == 0, "Misaligned register access");
        }
        
        *reinterpret_cast<volatile T*>(Reg::address) = value;
    }
};

// デフォルトテンプレート引数のためのエイリアス
template <typename CheckPolicy = std::true_type>
using DirectTransportT = DirectTransport<CheckPolicy>;

// DirectTransport concept - DirectTransportのみ許可
template <typename T>
concept DirectTransportType = std::same_as<T, DirectTransport<std::true_type>> || 
                              std::same_as<T, DirectTransport<std::false_type>> ||
                              std::same_as<T, DirectTransportT<>>;

} // namespace mm