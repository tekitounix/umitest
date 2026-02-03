-- lib/umios/crypto/test/xmake.lua

local test_dir = os.scriptdir()
local crypto_dir = path.join(os.scriptdir(), "..")

-- Crypto signature tests (SHA-512, Ed25519)
target("test_umios_crypto")
    add_rules("host.test")
    set_default(true)
    add_deps("umi.all", "umitest")
    add_files(path.join(test_dir, "test_signature.cc"))
    add_files(path.join(crypto_dir, "sha512.cc"))
    add_files(path.join(crypto_dir, "ed25519.cc"))
    add_includedirs(path.join(crypto_dir, ".."))
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()
