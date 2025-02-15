#include <catch_amalgamated.hpp>
#include <Flags.h>

enum class TestFlag
{
    Zeroth,
    First,
    Second,
};

using TestFlags = sl::Flags<TestFlag>;

constexpr TestFlags Identity = {};
constexpr TestFlags SetAll = -1;
constexpr TestFlags Set0 = TestFlag::Zeroth;
constexpr TestFlags Set1 = TestFlag::First;
constexpr TestFlags Set2 = TestFlag::Second;

TEST_CASE("sl::Flags<T>", "[syslib]") 
{
    CHECK_FALSE(TestFlags().Any());
    CHECK(Identity == TestFlags(0));

    CHECK_FALSE(Identity == SetAll);
    CHECK_FALSE(Set0 == Identity);
    CHECK_FALSE(Set1 == Identity);
    CHECK_FALSE(Set0 == Set1);
    CHECK_FALSE(Set0.Raw() == Identity.Raw());
    CHECK_FALSE(Set1.Raw() == Identity.Raw());
    CHECK_FALSE(Set0.Raw() == Set1.Raw());

    CHECK(TestFlags(TestFlag::Zeroth).SetThen(TestFlag::First).Raw() == 0b11);
    CHECK(TestFlags(TestFlag::Zeroth).SetThen(TestFlag::Second).Raw() == 0b101);
    CHECK(TestFlags(TestFlag::First).SetThen(TestFlag::Second).Raw() == 0b110);

    CHECK((SetAll & 1) == 1);
    CHECK((TestFlags(SetAll).ClearThen(TestFlag::Zeroth).Raw() & 1) == 0);
}
