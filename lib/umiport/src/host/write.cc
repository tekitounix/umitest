// SPDX-License-Identifier: MIT
#include <umirtm/detail/write.hh>
#include <unistd.h>

namespace umi::rt::detail {

void write_bytes(std::span<const std::byte> data) {
    ::write(1, data.data(), data.size());
}

} // namespace umi::rt::detail
