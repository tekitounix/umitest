-- umitest release metadata
-- See tools/release.lua for schema documentation
{
    description = "Zero-macro lightweight test framework for C++23",
    remote = "umitest-public",
    main_header = "umitest/test.hh",
    test_init = 'umi::test::Suite s("pkg_test");'
}
