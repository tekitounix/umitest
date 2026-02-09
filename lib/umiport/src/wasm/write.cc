// SPDX-License-Identifier: MIT
#include <umirtm/detail/write.hh>
#include <cstdio>

namespace umi::rt::detail {

void write_bytes(std::span<const std::byte> data) {
    std::fwrite(data.data(), 1, data.size(), stdout);
}

} // namespace umi::rt::detail
