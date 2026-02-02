/// @file mmio.hh
/// @brief Memory-mapped I/O library with transport layer architecture
/// @author Shota Moriguchi @tekitounix
/// @date 2025
/// @license MIT

#pragma once
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

/// @namespace mm
/// @brief Memory-mapped I/O abstractions
namespace mm {

/// @typedef Addr
/// @brief Memory address type
using Addr = std::uintptr_t;

// Forward declarations
template <class RegionT, typename T>
struct DynamicValue;
template <class FieldT, auto EnumValue>
struct Value;

/// @name Bit width constants
/// @{
constexpr std::size_t bits8 = 8U;   ///< 8-bit width
constexpr std::size_t bits16 = 16U; ///< 16-bit width
constexpr std::size_t bits32 = 32U; ///< 32-bit width
constexpr std::size_t bits64 = 64U; ///< 64-bit width
/// @}

/// @brief Select the smallest unsigned integer type that can hold Bits
template <std::size_t Bits>
using UintFit =
    std::conditional_t<(Bits <= bits8), std::uint8_t,
    std::conditional_t<(Bits <= bits16), std::uint16_t,
    std::conditional_t<(Bits <= bits32), std::uint32_t, std::uint64_t>>>;

/// @name Access policies
/// @{
struct RW { static constexpr bool can_read = true, can_write = true; };
struct RO { static constexpr bool can_read = true, can_write = false; };
struct WO { static constexpr bool can_read = false, can_write = true; };
struct Inherit {};
/// @}

/// @name Transport tags
/// @{
struct DirectTransportTag {};
struct I2CTransportTag {};
struct SPITransportTag {};
/// @}

/// @brief Device - top level with transport constraints
template <class Access = RW, typename... AllowedTransports>
struct Device {
    using AccessType = Access;
    using AllowedTransportsType = std::conditional_t<
        sizeof...(AllowedTransports) == 0,
        std::tuple<DirectTransportTag>,  // Default to direct transport only
        std::tuple<AllowedTransports...>
    >;
    static constexpr Addr base_address = 0;
};

/// @brief Block - memory region within device
template <class Parent, Addr BaseAddr, class Access = Inherit>
struct Block {
    using AccessType = std::conditional_t<std::is_same_v<Access, Inherit>, 
                                       typename Parent::AccessType, Access>;
    using AllowedTransportsType = typename Parent::AllowedTransportsType;
    static constexpr Addr base_address = Parent::base_address + BaseAddr;
};

/// @brief Bit region base - unified implementation for registers and fields
template <class Parent, Addr AddressOrOffset, std::size_t RegBitWidth,
          std::size_t BitOffset, std::size_t BitWidth, class Access, 
          std::uint64_t ResetValue, bool IsRegister>
struct BitRegion {
    using ParentAccessType = typename Parent::AccessType;
    using AccessType = std::conditional_t<std::is_same_v<Access, Inherit>, 
                                       ParentAccessType, Access>;
    using AllowedTransportsType = typename Parent::AllowedTransportsType;
    
    // For fields, provide ParentRegType
    using ParentRegType = std::conditional_t<IsRegister, void, Parent>;
    
    static constexpr bool is_register = IsRegister;
    static constexpr std::size_t bit_width = BitWidth;
    static constexpr std::size_t shift = BitOffset;
    static constexpr std::size_t reg_bits = RegBitWidth;
    
    using RegValueType = UintFit<reg_bits>;
    using ValueType = UintFit<bit_width>;
    
    static constexpr Addr address = []() {
        if constexpr (IsRegister) {
            return Parent::base_address + AddressOrOffset;
        } else {
            return Parent::address;
        }
    }();
    
    static consteval RegValueType mask() {
        if constexpr (bit_width >= sizeof(RegValueType) * bits8) {
            return ~RegValueType{0};
        } else {
            return static_cast<RegValueType>((RegValueType{1} << bit_width) - 1) << shift;
        }
    }
    
    static consteval RegValueType reset_value() {
        if constexpr (IsRegister) {
            return static_cast<RegValueType>(ResetValue);
        } else {
            return Parent::reset_value();
        }
    }
    
    // Dynamic value creation
    template <std::integral T>
    [[nodiscard("value() result must be used with write(), modify(), or is()")]]
    static constexpr auto value(T val) {
        // Compile-time range check
        if (std::is_constant_evaluated()) {
            if constexpr (bit_width < sizeof(T) * bits8) {
                auto const max_value = (1ULL << bit_width) - 1;
                if (static_cast<std::uint64_t>(val) > max_value) {
                    struct ValueOutOfRange {};
                    auto error = ValueOutOfRange{};
                    (void)error;
                }
            }
        }
        // Store original value without masking for runtime checks
        return DynamicValue<BitRegion, T>{val};
    }
};

/// @brief Register = BitRegion with IsRegister=true
template <class Parent, Addr Offset, std::size_t Bits, 
          class Access = RW, std::uint64_t Reset = 0>
using Register = BitRegion<Parent, Offset, Bits, 0, Bits, Access, Reset, true>;

/// @brief Forward declaration for Value
template <class FieldT, auto EnumValue>
struct Value;

/// @brief Field = BitRegion with IsRegister=false
template <class Reg, std::size_t BitOffset, std::size_t BitWidth, class Access = Inherit>
struct Field : BitRegion<Reg, 0, Reg::reg_bits, BitOffset, BitWidth, Access, 0, false> {
    using ParentRegType = Reg;  // Required by RegOps
    // parent_reg alias kept for backward compatibility
};

/// @brief 1-bit field specialization (auto Set/Reset)
template <class Reg, std::size_t BitOffset, class Access>
struct Field<Reg, BitOffset, 1, Access> : BitRegion<Reg, 0, Reg::reg_bits, BitOffset, 1, Access, 0, false> {
    using ParentRegType = Reg;  // Required by RegOps
    // parent_reg alias kept for backward compatibility
    using Set = Value<Field, 1>;
    using Reset = Value<Field, 0>;
};

/// @brief Value representation for Fields and Registers
template <class RegionT, auto EnumValue>
struct Value {
    using RegionType = RegionT;
    static constexpr auto value = EnumValue;
    
    // For Fields, provide shifted value
    static constexpr auto shifted_value = []() {
        if constexpr (requires { RegionT::shift; }) {
            return static_cast<typename RegionT::RegValueType>(value) << RegionT::shift;
        } else {
            return static_cast<typename RegionT::RegValueType>(value);
        }
    }();
    
    // Get the raw value (unshifted)
    static constexpr auto get() noexcept {
        return value;
    }
    
    // Get the value type
    using ValueType = decltype(value);
    
    // For backward compatibility
    using FieldType = RegionT;
};

template <class RegionT, typename T>
struct [[nodiscard("DynamicValue must be used with write(), modify(), or is()")]] 
DynamicValue {
    using RegionType = RegionT;
    using RegionValueType = typename RegionT::ValueType;
    T assigned_value;
};

// Forward declaration for friend
template <class Derived, typename CheckPolicy>
class ByteAdapter;

/// @brief RegOps base class - CRTP for register operations
template <class Derived, typename CheckPolicy = std::true_type>
class RegOps {
private:
    friend Derived;
    template <class D, typename C>
    friend class ByteAdapter;
    RegOps() = default;

protected:
    ~RegOps() = default;
    auto& self() { return static_cast<Derived&>(*this); }
    [[nodiscard]] const auto& self() const { return static_cast<const Derived&>(*this); }
    
    // Transport constraint check (at device level)
    template <typename Reg>
    static constexpr void check_transport_allowed() {
        using AllowedType = typename Reg::AllowedTransportsType;
        using TransportTagType = typename Derived::TransportTag;
        
        constexpr bool is_allowed = []<typename... Ts>(std::tuple<Ts...>) {
            return (std::is_same_v<TransportTagType, Ts> || ...);
        }(AllowedType{});
        
        static_assert(is_allowed, "This transport is not allowed for this device");
    }

public:
    // Derived classes must implement:
    // template <typename Reg>
    // auto reg_read(Reg reg) const noexcept -> typename Reg::RegValueType;
    //
    // template <typename Reg>
    // void reg_write(Reg reg, typename Reg::RegValueType value) const noexcept;

    // write - write to register/field
    template <typename... Values>
    void write(Values&&... values) noexcept {
        if constexpr (sizeof...(Values) == 1) {
            write_single(std::forward<Values>(values)...);
        } else {
            write_multiple(std::forward<Values>(values)...);
        }
    }
    
    // modify - Read-Modify-Write
    template <typename... Values>
    void modify(Values&&... values) const noexcept {
        static_assert(sizeof...(Values) > 0);
        modify_impl(std::forward<Values>(values)...);
    }
    
    // read - read register/field
    template <typename RegOrField>
    auto read(RegOrField /*reg_or_field*/) const noexcept {
        check_transport_allowed<RegOrField>();
        static_assert(RegOrField::AccessType::can_read, "Cannot read from write-only register");
        
        if constexpr (RegOrField::is_register) {
            return self().reg_read(RegOrField{});
        } else {
            auto const reg_val = self().reg_read(typename RegOrField::ParentRegType{});
            return static_cast<typename RegOrField::ValueType>(
                (reg_val & RegOrField::mask()) >> RegOrField::shift
            );
        }
    }
    
    // is - value comparison
    template <typename Value>
    bool is(Value&& value) const noexcept {
        using V = std::decay_t<Value>;
        
        if constexpr (requires { V::value; }) {
            // Enumerated value
            return read(typename V::FieldType{}) == V::value;
        } else {
            // Dynamic value with runtime check
            using RegionType = typename V::RegionType;
            if constexpr (CheckPolicy::value) {
                if constexpr (RegionType::bit_width < sizeof(decltype(value.assigned_value)) * bits8) {
                    auto const max_value = (1ULL << RegionType::bit_width) - 1;
                    assert(static_cast<std::uint64_t>(value.assigned_value) <= max_value && "Comparison value out of range");
                }
            }
            return read(RegionType{}) == value.assigned_value;
        }
    }
    
    // flip - toggle 1-bit field
    template <typename Field>
    void flip(Field /*field*/) const noexcept {
        static_assert(Field::bit_width == 1, "flip() only for 1-bit fields");
        static_assert(Field::AccessType::can_read && Field::AccessType::can_write, 
                     "flip() requires read-write access");
        check_transport_allowed<Field>();
        
        using ParentRegType = typename Field::ParentRegType;
        auto current = self().reg_read(ParentRegType{});
        current ^= Field::mask();
        self().reg_write(ParentRegType{}, current);
    }

private:
    // Helper functions
    template <typename T, typename Value>
    static auto apply_field_value(T reg_val, const Value& v) {
        using V = std::decay_t<Value>;
        using RegionType = typename V::RegionType;
        
        if constexpr (requires { V::value; }) {
            // Enumerated value (has static 'value' member)
            return (reg_val & ~RegionType::mask()) | (static_cast<T>(V::value) << RegionType::shift);
        } else {
            // Dynamic value
            // Runtime range check if policy is enabled
            if constexpr (CheckPolicy::value) {
                if constexpr (RegionType::bit_width < sizeof(T) * bits8) {
                    auto const max_value = (1ULL << RegionType::bit_width) - 1;
                    assert(static_cast<std::uint64_t>(v.assigned_value) <= max_value && "Value out of range");
                }
            }
            return (reg_val & ~RegionType::mask()) | ((static_cast<T>(v.assigned_value) << RegionType::shift) & RegionType::mask());
        }
    }
    
    template <typename T, typename Value>
    static auto apply_field_modify(T current, const Value& v) {
        return apply_field_value(current, v);
    }
    
    // Implementation details
    template <typename Value>
    void write_single(Value&& value) noexcept {
        using V = std::decay_t<Value>;
        using RegionType = typename V::RegionType;
        check_transport_allowed<RegionType>();
        static_assert(RegionType::AccessType::can_write, "Cannot write to read-only register");
        
        if constexpr (RegionType::is_register) {
            // Handle Value type for registers
            if constexpr (requires { V::value; }) {
                // Value type with enumerated value
                if constexpr (CheckPolicy::value) {
                    if constexpr (RegionType::bit_width < sizeof(V::value) * bits8) {
                        auto const max_value = (1ULL << RegionType::bit_width) - 1;
                        assert(static_cast<std::uint64_t>(V::value) <= max_value && "Register value out of range");
                    }
                }
                self().reg_write(RegionType{}, static_cast<typename RegionType::RegValueType>(V::value));
            } else {
                // DynamicValue type
                if constexpr (CheckPolicy::value) {
                    if constexpr (RegionType::bit_width < sizeof(decltype(value.assigned_value)) * bits8) {
                        auto const max_value = (1ULL << RegionType::bit_width) - 1;
                        assert(static_cast<std::uint64_t>(value.assigned_value) <= max_value && "Register value out of range");
                    }
                }
                self().reg_write(RegionType{}, value.assigned_value);
            }
        } else {
            // Field: start from reset value
            using ParentRegType = typename RegionType::ParentRegType;
            auto reg_val = ParentRegType::reset_value();
            if constexpr (requires { V::value; }) {
                // Enumerated value
                reg_val = (reg_val & ~RegionType::mask()) | 
                         ((V::value << RegionType::shift) & RegionType::mask());
            } else {
                // Dynamic value with runtime check
                if constexpr (CheckPolicy::value) {
                    if constexpr (RegionType::bit_width < sizeof(decltype(value.assigned_value)) * bits8) {
                        auto const max_value = (1ULL << RegionType::bit_width) - 1;
                        assert(static_cast<std::uint64_t>(value.assigned_value) <= max_value && "Field value out of range");
                    }
                }
                reg_val = (reg_val & ~RegionType::mask()) | 
                         ((value.assigned_value << RegionType::shift) & RegionType::mask());
            }
            self().reg_write(ParentRegType{}, reg_val);
        }
    }
    
    // Multiple field write (optimized)
    template <typename... Values>
    void write_multiple(Values&&... values) noexcept {
        // Get the parent register from the first value's region
        using FirstValueType = std::decay_t<std::tuple_element_t<0, std::tuple<Values...>>>;
        using FirstRegionType = typename FirstValueType::RegionType;
        
        // For fields, we need parent_reg_t
        using FirstRegType = std::conditional_t<
            FirstRegionType::is_register,
            FirstRegionType,
            typename FirstRegionType::ParentRegType
        >;
        
        check_transport_allowed<FirstRegType>();
        
        auto reg_val = FirstRegType::reset_value();
        ((reg_val = apply_field_value(reg_val, values)), ...);
        self().reg_write(FirstRegType{}, reg_val);
    }
    
    // modify implementation (single RMW)
    template <typename... Values>
    void modify_impl(Values&&... values) const noexcept {
        // Get the parent register from the first value
        using FirstValueType = std::decay_t<std::tuple_element_t<0, std::tuple<Values...>>>;
        using FirstRegionType = typename FirstValueType::RegionType;
        
        // Get parent register type
        using ParentRegType = std::conditional_t<
            FirstRegionType::is_register,
            FirstRegionType,
            typename FirstRegionType::ParentRegType
        >;
        
        check_transport_allowed<ParentRegType>();
        
        auto current = self().reg_read(ParentRegType{});
        ((current = apply_field_modify(current, values)), ...);
        self().reg_write(ParentRegType{}, current);
    }
};

// DirectTransport has been moved to mmio/transport/direct.hh

/// @brief ByteAdapter - base class for byte-oriented transports
template <class Derived, typename CheckPolicy = std::true_type>
class ByteAdapter : private RegOps<Derived, CheckPolicy> {
    friend class RegOps<Derived, CheckPolicy>;
protected:
    // Endian conversion (little-endian)
    template <typename T>
    static void pack_le(T value, std::uint8_t* buffer) noexcept {
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            buffer[i] = (value >> (i * 8)) & 0xFF;
        }
    }

    template <typename T>
    static T unpack_le(const std::uint8_t* buffer) noexcept {
        T value = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            value |= static_cast<T>(buffer[i]) << (i * 8);
        }
        return value;
    }

    using RegOps<Derived>::self;

public:
    using RegOps<Derived, CheckPolicy>::write;
    using RegOps<Derived, CheckPolicy>::read;
    using RegOps<Derived, CheckPolicy>::modify;
    using RegOps<Derived, CheckPolicy>::is;
    using RegOps<Derived, CheckPolicy>::flip;
    // RegOps required operations
    template <typename Reg>
    auto reg_read(Reg /*reg*/) const noexcept -> typename Reg::RegValueType {
        using T = typename Reg::RegValueType;
        static_assert(sizeof(T) <= 8, "Register size must be <= 64 bits");
        
        std::array<std::uint8_t, sizeof(T)> buffer;
        self().raw_read(static_cast<std::uint8_t>(Reg::address), buffer.data(), sizeof(T));
        return unpack_le<T>(buffer.data());
    }

    template <typename Reg>
    void reg_write(Reg /*reg*/, typename Reg::RegValueType value) const noexcept {
        using T = typename Reg::RegValueType;
        static_assert(sizeof(T) <= 8, "Register size must be <= 64 bits");
        
        std::array<std::uint8_t, sizeof(T)> buffer;
        pack_le(value, buffer.data());
        self().raw_write(static_cast<std::uint8_t>(Reg::address), buffer.data(), sizeof(T));
    }
};

// I2cTransport has been moved to mmio/transport/i2c.hh

} // namespace mm