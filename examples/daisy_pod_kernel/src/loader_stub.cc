// SPDX-License-Identifier: MIT
// Minimal AppLoader method stubs for daisy_pod_kernel
// Full loader.cc has crypto/MPU dependencies not needed yet.
// Only the methods called from syscall dispatch are implemented here.

#include <umios/kernel/loader.hh>
#include <cstring>

namespace umi::kernel {

LoadResult AppLoader::load(const uint8_t* /*image*/, size_t /*size*/) noexcept {
    // Not yet implemented for H7 kernel — use set_entry() for XIP
    return LoadResult::INVALID_SIZE;
}

void AppLoader::unload() noexcept {
    if (runtime_.state == AppState::NONE) {
        return;
    }
    if (runtime_.base != nullptr && app_ram_size_ > 0) {
        std::memset(runtime_.base, 0, app_ram_size_);
    }
    runtime_.clear();
    loaded_header_ = nullptr;
}

bool AppLoader::start() noexcept {
    if (runtime_.state != AppState::LOADED) {
        return false;
    }
    if (runtime_.entry == nullptr) {
        return false;
    }
    runtime_.state = AppState::RUNNING;
    runtime_.entry();
    return true;
}

void AppLoader::terminate(int exit_code) noexcept {
    runtime_.exit_code = exit_code;
    runtime_.state = AppState::TERMINATED;
    runtime_.processor = nullptr;
    runtime_.process_fn = nullptr;
}

void AppLoader::suspend() noexcept {
    if (runtime_.state == AppState::RUNNING) {
        runtime_.state = AppState::SUSPENDED;
    }
}

void AppLoader::resume() noexcept {
    if (runtime_.state == AppState::SUSPENDED) {
        runtime_.state = AppState::RUNNING;
    }
}

int AppLoader::register_processor(void* processor, ProcessFn process_fn) noexcept {
    if (runtime_.state != AppState::RUNNING) {
        return -1;
    }
    if (processor == nullptr || process_fn == nullptr) {
        return -1;
    }
    runtime_.processor = processor;
    runtime_.process_fn = process_fn;
    return 0;
}

// Private methods — stub
LoadResult AppLoader::validate_header(const AppHeader*, size_t) noexcept {
    return LoadResult::INVALID_MAGIC;
}

bool AppLoader::verify_crc(const AppHeader*, const uint8_t*) noexcept {
    return false;
}

bool AppLoader::verify_signature(const AppHeader*, const uint8_t*) noexcept {
    return false;
}

bool AppLoader::setup_memory(const AppHeader*) noexcept {
    return false;
}

void AppLoader::copy_sections(const AppHeader*, const uint8_t*) noexcept {
}

void AppLoader::configure_mpu() noexcept {
}

} // namespace umi::kernel
