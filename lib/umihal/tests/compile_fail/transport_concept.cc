// SPDX-License-Identifier: MIT
// compile-fail test: incomplete transport must not satisfy I2cTransport.

#include <umihal/concept/transport.hh>

struct IncompleteI2c {
    // Missing write() and read() — must not satisfy I2cTransport.
};

static_assert(umi::hal::I2cTransport<IncompleteI2c>,
    "This should fail: IncompleteI2c does not satisfy I2cTransport");
