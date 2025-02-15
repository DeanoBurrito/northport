#include <catch_amalgamated.hpp>
#include <Maths.h>

TEST_CASE("sl::Maths", "syslib")
{
    CHECK(sl::AlignDown(0x1234, 0x1) == 0x1234);
    CHECK(sl::AlignDown(0x1234, 0x10) == 0x1230);
    CHECK(sl::AlignDown(0x1234, 0x100) == 0x1200);
    CHECK(sl::AlignDown(0x1234, 0x1000) == 0x1000);
    CHECK(sl::AlignDown(0x1234, 0x10000) == 0x0);
    CHECK(sl::AlignDown(0x1000, 0x1000) == 0x1000);

    CHECK(sl::AlignUp(0x1234, 0x1) == 0x1234);
    CHECK(sl::AlignUp(0x1234, 0x10) == 0x1240);
    CHECK(sl::AlignUp(0x1234, 0x100) == 0x1300);
    CHECK(sl::AlignUp(0x1234, 0x1000) == 0x2000);
    CHECK(sl::AlignUp(0x1000, 0x1000) == 0x1000);

    CHECK(sl::Min(0, 100) == 0);
    CHECK(sl::Max(0, 100) == 100);

    CHECK(sl::Clamp(42, 0, 100) == 42);
    CHECK(sl::Clamp(0, 42, 100) == 42);
    CHECK(sl::Clamp(100, 0, 42) == 42);

    constexpr uint8_t u8 = 0xAA;
    constexpr uint16_t u16 = 0xAABB;
    constexpr uint32_t u32 = 0xAABBCCDD;
    constexpr uint64_t u64 = 0xAABBCCDDEEFF0011;

    CHECK(sl::ByteSwap(u8) == 0xAA);
    CHECK(sl::ByteSwap(u16) == 0xBBAA);
    CHECK(sl::ByteSwap(u32) == 0xDDCCBBAA);
    CHECK(sl::ByteSwap(u64) == 0x1100FFEEDDCCBBAA);

    CHECK(sl::IsPowerOfTwo(1));
    CHECK(sl::IsPowerOfTwo(2));
    CHECK(sl::IsPowerOfTwo(4));
    CHECK(sl::IsPowerOfTwo(0x1000));
    CHECK(sl::IsPowerOfTwo(2 * GiB));
    CHECK_FALSE(sl::IsPowerOfTwo(0));
    CHECK_FALSE(sl::IsPowerOfTwo(3));
    CHECK_FALSE(sl::IsPowerOfTwo(0x1001));
    CHECK_FALSE(sl::IsPowerOfTwo((2 * GiB) - 1));

    CHECK(sl::AlignUpBinary(1) == 1);
    CHECK(sl::AlignUpBinary(3) == 4);
    CHECK(sl::AlignUpBinary(5) == 8);
    CHECK(sl::AlignUpBinary(101) == 128);
    CHECK(sl::AlignUpBinary(512) == 512);

    //TODO: MaxOf/MinOf, MapRange, Normalize, PopCount
}
