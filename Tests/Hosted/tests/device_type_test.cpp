#include <catch2/catch_test_macros.hpp>
#include <Kernel.hpp>

using namespace kernel;

TEST_CASE("DeviceType static identifier", "[DeviceType]") {
    SECTION("Constants consistency") {
        REQUIRE(device_type::Bus == DeviceType::from_literal("bus"));
        REQUIRE(device_type::Disk == DeviceType::from_literal("disk"));
        REQUIRE(device_type::Keyboard == DeviceType::from_literal("keyboard"));
        REQUIRE(device_type::Mouse == DeviceType::from_literal("mouse"));
        REQUIRE(device_type::Video == DeviceType::from_literal("video"));
        REQUIRE(device_type::Serial == DeviceType::from_literal("serial"));
        REQUIRE(device_type::Parallel == DeviceType::from_literal("parallel"));
    }

    SECTION("Unique values") {
        REQUIRE(device_type::Bus != device_type::Disk);
        REQUIRE(device_type::Keyboard != device_type::Mouse);
        REQUIRE(device_type::Unknown.value == 0);
        REQUIRE(device_type::Bus.value != 0);
    }

    SECTION("Literal operator") {
        auto custom = "custom"_dev;
        REQUIRE(custom == DeviceType::from_literal("custom"));
        REQUIRE(custom != device_type::Bus);
    }
}
