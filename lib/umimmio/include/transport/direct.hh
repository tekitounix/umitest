#pragma once

#include "../umimmio.hh"  // RegOpsとDirectTransportTagが必要
#include <concepts>

namespace mm {

/// @brief DirectTransport - memory-mapped I/O
template <typename CheckPolicy = std::true_type, typename ErrorPolicy = AssertOnError>
class DirectTransport : private RegOps<DirectTransport<CheckPolicy, ErrorPolicy>, CheckPolicy, ErrorPolicy> {
    friend class RegOps<DirectTransport<CheckPolicy, ErrorPolicy>, CheckPolicy, ErrorPolicy>;
public:
    using RegOps<DirectTransport<CheckPolicy, ErrorPolicy>, CheckPolicy, ErrorPolicy>::write;
    using RegOps<DirectTransport<CheckPolicy, ErrorPolicy>, CheckPolicy, ErrorPolicy>::read;
    using RegOps<DirectTransport<CheckPolicy, ErrorPolicy>, CheckPolicy, ErrorPolicy>::modify;
    using RegOps<DirectTransport<CheckPolicy, ErrorPolicy>, CheckPolicy, ErrorPolicy>::is;
    using RegOps<DirectTransport<CheckPolicy, ErrorPolicy>, CheckPolicy, ErrorPolicy>::flip;
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
template <typename CheckPolicy = std::true_type, typename ErrorPolicy = AssertOnError>
using DirectTransportT = DirectTransport<CheckPolicy, ErrorPolicy>;

// DirectTransport concept - DirectTransportのみ許可
template <typename T>
concept DirectTransportType = std::same_as<T, DirectTransport<std::true_type, AssertOnError>> || 
                              std::same_as<T, DirectTransport<std::false_type, AssertOnError>> ||
                              std::same_as<T, DirectTransportT<>>;

} // namespace mm
