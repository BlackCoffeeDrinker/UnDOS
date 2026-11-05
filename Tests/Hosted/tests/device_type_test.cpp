#include <catch2/catch_test_macros.hpp>
#include <Kernel.hpp>

using namespace kernel;

TEST_CASE("DeviceType static identifier", "[DeviceType]") {
    SECTION("Constants consistency") {
        REQUIRE(device_type::Bus == DeviceType::from_literal("bus"));
        REQUIRE(device_type::Disk == DeviceType::from_literal("disk"));
        REQUIRE(device_type::Optical == DeviceType::from_literal("optical"));
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
        for (const auto other: {device_type::Unknown, device_type::Bus, device_type::DiskController,
                                device_type::Disk, device_type::Keyboard, device_type::Mouse,
                                device_type::Gamepad, device_type::Video, device_type::Serial,
                                device_type::Parallel, device_type::Volume}) {
            REQUIRE(device_type::Optical != other);
        }
    }

    SECTION("Literal operator") {
        auto custom = "custom"_dev;
        REQUIRE(custom == DeviceType::from_literal("custom"));
        REQUIRE(custom != device_type::Bus);
    }
}
