#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#include "v4l2.hpp"
#include "argument_parser.hpp"

using ::testing::ThrowsMessage;

class V42LTest : public ::testing::Test {
private:
	std::vector<std::string> args_{};
	V4L2::Config config_;

protected:
    void SetUp() override {

	}

	void addArguments(std::vector<std::string> args) {
		 args_.insert(args_.end(), args.begin(), args.end());
	}

	Arguments::ParseResult parseArguments() {
		std::vector<const char*> ptr_vector;
		ptr_vector.reserve(args_.size());
		std::transform(
			args_.begin(), 
			args_.end(), 
			std::back_inserter(ptr_vector),
				[](const std::string& str) { 
					return str.c_str(); 
				});
		ptr_vector.push_back(nullptr); 
		const char** c_array = ptr_vector.data();

		return V4L2::parseArguments(args_.size(), c_array, config_);
	}

	const V4L2::Config& getConfig() const {
		return config_;
	}
};

TEST_F(V42LTest, ParseDevicePath) {	
	std::vector<std::string> args = {
		"./v4l2-borescope-daemon",
		"--dev",
		"/dev/video3",
		"--bus",
		"1",
		"--address",
		"2",
		"--width",
		"640",
		"--height",
		"480",
		"--size",
		"131072"
	};
	addArguments(args);
	auto result = parseArguments();
	EXPECT_EQ(result, Arguments::ParseResult::Success);

	auto config = getConfig();
    EXPECT_EQ(config.devicePath, "/dev/video3");
}

TEST_F(V42LTest, ParseBus) {	
	std::vector<std::string> args = {
		"./v4l2-borescope-daemon",
		"--dev",
		"/dev/video3",
		"--bus",
		"13",
		"--address",
		"2",
		"--width",
		"640",
		"--height",
		"480",
		"--size",
		"131072"
	};
	addArguments(args);
	auto result = parseArguments();
	EXPECT_EQ(result, Arguments::ParseResult::Success);

	auto config = getConfig();
    EXPECT_EQ(config.bus, 13);
}

TEST_F(V42LTest, ParseBusLargerThan255) {	
	std::vector<std::string> args = {
		"./v4l2-borescope-daemon",
		"--dev",
		"/dev/video3",
		"--bus",
		"512",
		"--address",
		"2",
		"--width",
		"640",
		"--height",
		"480",
		"--size",
		"131072"
	};
	addArguments(args);

	EXPECT_THAT(
		[this]() { parseArguments(); }, 
		ThrowsMessage<std::out_of_range>("bus exceeds uint8_t max range: 512")
	);
}

TEST_F(V42LTest, ParseAddress) {	
	std::vector<std::string> args = {
		"./v4l2-borescope-daemon",
		"--dev",
		"/dev/video3",
		"--bus",
		"1",
		"--address",
		"12",
		"--width",
		"640",
		"--height",
		"480",
		"--size",
		"131072"
	};
	addArguments(args);
	auto result = parseArguments();
	EXPECT_EQ(result, Arguments::ParseResult::Success);

	auto config = getConfig();
    EXPECT_EQ(config.address, 12);
}

TEST_F(V42LTest, ParseAddressLargerThan255) {	
	std::vector<std::string> args = {
		"./v4l2-borescope-daemon",
		"--dev",
		"/dev/video3",
		"--bus",
		"1",
		"--address",
		"512",
		"--width",
		"640",
		"--height",
		"480",
		"--size",
		"131072"
	};
	addArguments(args);
	EXPECT_THAT(
		[this]() { parseArguments(); }, 
		ThrowsMessage<std::out_of_range>("address exceeds uint8_t max range: 512")
	);
}

TEST_F(V42LTest, ParseWidth) {	
	std::vector<std::string> args = {
		"./v4l2-borescope-daemon",
		"--dev",
		"/dev/video3",
		"--bus",
		"1",
		"--address",
		"2",
		"--width",
		"1024",
		"--height",
		"768",
		"--size",
		"131072"
	};
	addArguments(args);
	auto result = parseArguments();
	EXPECT_EQ(result, Arguments::ParseResult::Success);

	auto config = getConfig();
    EXPECT_EQ(config.width, 1024);
}

TEST_F(V42LTest, ParseHeight) {	
	std::vector<std::string> args = {
		"./v4l2-borescope-daemon",
		"--dev",
		"/dev/video3",
		"--bus",
		"1",
		"--address",
		"2",
		"--width",
		"1024",
		"--height",
		"768",
		"--size",
		"131072"
	};
	addArguments(args);
	auto result = parseArguments();
	EXPECT_EQ(result, Arguments::ParseResult::Success);

	auto config = getConfig();
    EXPECT_EQ(config.height, 768);
}

TEST_F(V42LTest, ParseSizeRange) {	
	std::vector<std::string> args = {
		"./v4l2-borescope-daemon",
		"--dev",
		"/dev/video3",
		"--bus",
		"1",
		"--address",
		"2",
		"--width",
		"1024",
		"--height",
		"768",
		"--size",
		"123456"
	};
	addArguments(args);
	auto result = parseArguments();
	EXPECT_EQ(result, Arguments::ParseResult::Success);

	auto config = getConfig();

	size_t size = 123456;
    EXPECT_EQ(config.sizeImage, size);
}

TEST_F(V42LTest, ParseHelp) {	
	std::vector<std::string> args = {
		"./v4l2-borescope-daemon",
		"--help"
	};
	addArguments(args);
	auto result = parseArguments();
	EXPECT_EQ(result, Arguments::ParseResult::HelpRequested);
}

TEST_F(V42LTest, ParseUnknownArgument) {	
    std::vector<std::string> args = {
        "./v4l2-borescope-daemon",
        "--unknown",
        "N/A"
    };
    addArguments(args);

    EXPECT_THAT(
		[this]() { parseArguments(); }, 
		ThrowsMessage<std::invalid_argument>("Unknown argument: --unknown")
	);
}