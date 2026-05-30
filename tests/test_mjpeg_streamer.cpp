#include <gtest/gtest.h>
#include "server_time.hpp"
#include "wall_clock.hpp"
#include "index_html.hpp"

#include <chrono>
#include <string>
#include <thread>

TEST(MjpegStreamerTest, GeneratedHeaderIsAccessible) {
    auto htmlContent = Resources::index_html;
    ASSERT_FALSE(htmlContent.empty()) << "Could not read or file is empty";
    EXPECT_NE(htmlContent.find("<title>Borescope Desk</title>"), std::string::npos) 
        << "Error: <title>Borescope Desk</title> tag not found in the HTML.";
}

TEST(ServerTimeTest, MeasuresElapsedMilliseconds) {
    WallClock clock;
    auto startTime = clock.now();
    ServerTime timer(clock, startTime);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    long long elapsed = timer.get();

    EXPECT_GE(elapsed, 50);
    EXPECT_LT(elapsed, 100);
}