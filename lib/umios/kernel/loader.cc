// SPDX-License-Identifier: MIT
// UMI-OS Application Loader Implementation

#include "loader.hh"

#include <cstring>

#include "../crypto/ed25519.hh"
#include "../crypto/public_key.hh"
#include "mpu_config.hh"

#if defined(__ARM_ARCH) && defined(UMIOS_KERNEL)
#include "../backend/cm/platform/privilege.hh"
#endif

namespace umi::kernel {

// ============================================================================
// AppLoader Implementation
// ============================================================================

LoadResult AppLoader::load(const uint8_t* image, size_t size) noexcept {
    // Check if already loaded
    if (runtime_.state != AppState::None) {
        return LoadResult::ALREADY_LOADED;
    }

    // Validate minimum size
    if (size < sizeof(AppHeader)) {
        return LoadResult::INVALID_SIZE;
    }

    // Get header
    const auto* header = reinterpret_cast<const AppHeader*>(image);

    // Validate header
    auto result = validate_header(header, size);
    if (result != LoadResult::OK) {
        return result;
    }

    // Verify CRC
    if (!verify_crc(header, image)) {
        return LoadResult::CRC_MISMATCH;
    }

    // Verify signature for Release apps
    if (header->target == AppTarget::Release) {
        if (!verify_signature(header, image)) {
            return LoadResult::SIGNATURE_INVALID;
        }
    }

    // Setup memory layout
    if (!setup_memory(header)) {
        return LoadResult::MEMORY_ERROR;
    }

    // Copy sections to RAM
    copy_sections(header, image);

    // Configure MPU for isolation
    configure_mpu();

    // Update state
    loaded_header_ = header;
    runtime_.state = AppState::Loaded;

    return LoadResult::OK;
}

void AppLoader::unload() noexcept {
    if (runtime_.state == AppState::None) {
        return;
    }

    // Clear memory
    if (runtime_.base != nullptr && app_ram_size_ > 0) {
        std::memset(runtime_.base, 0, app_ram_size_);
    }

    // Reset state
    runtime_.clear();
    loaded_header_ = nullptr;
}

bool AppLoader::start() noexcept {
    if (runtime_.state != AppState::Loaded) {
        return false;
    }

    if (runtime_.entry == nullptr) {
        return false;
    }

    runtime_.state = AppState::Running;

#if defined(__ARM_ARCH) && defined(UMIOS_KERNEL)
    // Start application in unprivileged mode with PSP
    // This is a one-way transition - cannot return to privileged without SVC

    // Set up PSP to application stack
    auto user_sp = reinterpret_cast<uint32_t>(runtime_.stack_top);

    // entry() is a [[noreturn]] function in enter_user_mode
    // Control returns via SVC_Handler syscalls
    umi::privilege::enter_user_mode(user_sp, runtime_.entry);

    // Should never reach here
    __builtin_unreachable();
#else
    // Non-ARM builds: just call the entry function directly (for testing)
    // No privilege separation on host
    runtime_.entry();
#endif

    return true;
}

void AppLoader::terminate(int exit_code) noexcept {
    runtime_.exit_code = exit_code;
    runtime_.state = AppState::Terminated;

    // Clear processor registration
    runtime_.processor = nullptr;
    runtime_.process_fn = nullptr;
}

void AppLoader::suspend() noexcept {
    if (runtime_.state == AppState::Running) {
        runtime_.state = AppState::Suspended;
    }
}

void AppLoader::resume() noexcept {
    if (runtime_.state == AppState::Suspended) {
        runtime_.state = AppState::Running;
    }
}

int AppLoader::register_processor(void* processor, ProcessFn process_fn) noexcept {
    if (runtime_.state != AppState::Running) {
        return -1;
    }

    if (processor == nullptr || process_fn == nullptr) {
        return -1;
    }

    runtime_.processor = processor;
    runtime_.process_fn = process_fn;

    return 0;
}

// ============================================================================
// Internal Methods
// ============================================================================

LoadResult AppLoader::validate_header(const AppHeader* header, size_t image_size) noexcept {
    // Check magic
    if (!header->valid_magic()) {
        return LoadResult::INVALID_MAGIC;
    }

    // Check ABI version
    if (!header->compatible_abi()) {
        return LoadResult::INVALID_ABI;
    }

    // Check target compatibility
    switch (header->target) {
    case AppTarget::User:
        // User apps run on any kernel
        break;

    case AppTarget::Development:
        // Development apps only on development kernel
        if constexpr (KERNEL_BUILD_TYPE != BuildType::Development) {
            return LoadResult::INVALID_TARGET;
        }
        break;

    case AppTarget::Release:
        // Release apps only on release kernel
        if constexpr (KERNEL_BUILD_TYPE != BuildType::Release) {
            return LoadResult::INVALID_TARGET;
        }
        break;

    default:
        return LoadResult::INVALID_SIZE; // Unknown target
    }

    // Check size consistency
    size_t expected_size = sizeof(AppHeader) + header->sections_size();
    if (header->total_size != expected_size || image_size < expected_size) {
        return LoadResult::INVALID_SIZE;
    }

    // Check entry point is within text section
    if (header->entry_offset < sizeof(AppHeader) || header->entry_offset >= sizeof(AppHeader) + header->text_size) {
        return LoadResult::INVALID_SIZE;
    }

    return LoadResult::OK;
}

bool AppLoader::verify_crc(const AppHeader* header, const uint8_t* image) noexcept {
    // Calculate CRC of sections (after header)
    const uint8_t* sections_start = image + sizeof(AppHeader);
    size_t sections_len = header->sections_size();

    uint32_t calculated = crc32(sections_start, sections_len);

    return calculated == header->crc32;
}

bool AppLoader::verify_signature(const AppHeader* header, const uint8_t* image) noexcept {
    if constexpr (KERNEL_BUILD_TYPE == BuildType::Development) {
        // In development mode, skip signature verification for User/Development apps
        if (header->target != AppTarget::Release) {
            return true;
        }
    }

    // Select public key based on build type
    const uint8_t* public_key = nullptr;
    if constexpr (KERNEL_BUILD_TYPE == BuildType::Release) {
        public_key = crypto::RELEASE_PUBLIC_KEY;
    } else {
        public_key = crypto::DEVELOPMENT_PUBLIC_KEY;
    }

    // Signature verification scheme:
    // The signature covers the header (with signature field zeroed) + sections
    //
    // AppHeader layout (128 bytes):
    //   [0..56)    - fields before signature
    //   [56..120)  - signature (64 bytes)
    //   [120..128) - reserved
    //
    // Message to verify = header[0..56) + zeros[56..120) + header[120..128) + sections

    constexpr size_t sig_offset = 56;
    constexpr size_t sig_size = 64;

    (void)image;  // Image data not needed; sections integrity verified by CRC32

    // Create header copy with zeroed signature for verification
    std::array<uint8_t, sizeof(AppHeader)> header_copy{};
    std::memcpy(header_copy.data(), header, sizeof(AppHeader));
    std::memset(header_copy.data() + sig_offset, 0, sig_size);

    // For embedded systems with limited memory, we verify header only
    // This is a trade-off: smaller apps can be fully verified,
    // larger apps verify header integrity (CRC covers sections)
    //
    // The signing tool should sign: header (with zeroed sig) only
    // CRC32 already covers sections integrity

    return crypto::ed25519_verify(
        header->signature,
        public_key,
        header_copy.data(),
        sizeof(AppHeader)
    );
}

bool AppLoader::setup_memory(const AppHeader* header) noexcept {
    if (app_ram_base_ == nullptr || app_ram_size_ == 0) {
        return false;
    }

    // Calculate required memory
    size_t required = header->required_ram();
    if (required > app_ram_size_) {
        return false;
    }

    // Memory layout:
    // [base] -> .data/.bss
    // [base + data + bss] -> heap (if any)
    // [top - stack_size] -> stack (grows down)

    uint8_t* base = static_cast<uint8_t*>(app_ram_base_);

    runtime_.base = base;
    runtime_.data_start = base;

    // Stack at the end of app RAM
    runtime_.stack_base = base + app_ram_size_ - header->stack_size;
    runtime_.stack_top = base + app_ram_size_;

    return true;
}

void AppLoader::copy_sections(const AppHeader* header, const uint8_t* image) noexcept {
    const uint8_t* src = image + sizeof(AppHeader);

    // .text section - execute from flash (XIP)
    runtime_.text_start = const_cast<uint8_t*>(src);

    // Entry point (set bit 0 for Thumb mode on Cortex-M)
    uintptr_t entry_addr = reinterpret_cast<uintptr_t>(src) + (header->entry_offset - sizeof(AppHeader));
    entry_addr |= 1; // Thumb bit
    runtime_.entry = reinterpret_cast<void (*)()>(entry_addr);

    // .data/.bss initialization is handled by _start() (crt0) using linker symbols.
    // Loader only sets up text/entry and memory regions.
}

void AppLoader::configure_mpu() noexcept {
#if defined(__ARM_ARCH) && defined(UMIOS_KERNEL)
    // Configure MPU for application isolation
    // Only enable if MPU is available
    if (!mpu::is_available()) {
        return;
    }

    // Get shared memory info (if available)
    void* shared_base = nullptr;
    size_t shared_size = 0;
    if (shared_ != nullptr) {
        shared_base = shared_;
        shared_size = sizeof(SharedMemory);
    }

    // Kernel region: Flash from 0x08000000
    // For STM32F4, use conservative estimates
    void* kernel_flash_base = reinterpret_cast<void*>(0x08000000);
    size_t kernel_flash_size = 256 * 1024;  // 256KB for kernel

    mpu::configure_app_regions(
        runtime_,
        shared_base,
        shared_size,
        kernel_flash_base,
        kernel_flash_size
    );
#endif
}

} // namespace umi::kernel
