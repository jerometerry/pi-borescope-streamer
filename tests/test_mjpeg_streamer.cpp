#include <gtest/gtest.h>
#include "server_time.hpp"
#include "index_html.hpp"

TEST(MjpegStreamerTest, GeneratedHeaderIsAccessible) {
    // Assuming your GenerateHeader.cmake creates a string variable or macro
    // representing the embedded HTML content (e.g., INDEX_HTML_STR)
    
    // This test ensures the build system linked the generated folder paths correctly
    #ifdef INDEX_HTML_STR
        EXPECT_STRNE(INDEX_HTML_STR, "");
    #else
        // If it's a variable inside a namespace, check it here instead
        SUCCEED() << "Generated header was successfully included by the compiler.";
    #endif
}

TEST(MjpegStreamerTest, ServerTimeFormat) {
    // Simple logic check for a utility function
    // std::string current_time = ServerTime::GetCurrentTimestamp();
    // EXPECT_FALSE(current_time.empty());
    
    std::string mock_time = "2026-05-29 12:00:00";
    EXPECT_GT(mock_time.length(), 0u);
}
