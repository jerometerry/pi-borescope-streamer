#include <gtest/gtest.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <memory>

#include "web_server.hpp"

class WebServerTest : public ::testing::Test {
private:
    // Moved to private to satisfy cppcoreguidelines-non-private-member-variables-in-classes
    std::atomic<bool> running_{true};
    uint32_t latestFrameId_{0};
    std::vector<uint8_t> frameBuffer_;
    std::mutex frameMutex_;
    std::vector<uint8_t> snapshotBuffer_;
    std::mutex snapshotMutex_;

    static constexpr int TEST_PORT = 18080; 
    std::unique_ptr<WebServer> server_;

protected:
    void SetUp() override {
        server_ = std::make_unique<WebServer>(
            TEST_PORT, running_, latestFrameId_, 
            frameBuffer_, frameMutex_, 
            snapshotBuffer_, snapshotMutex_
        );

        ASSERT_TRUE(server_->initialize()) << "Failed to bind to test port. Is it already in use?";
        server_->start();

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void TearDown() override {
        running_ = false;
        server_.reset(); 
    }

    // --- Proxy Helpers for Tests ---
    void injectMockSnapshot(const std::vector<uint8_t>& mockData) {
        std::scoped_lock<std::mutex> lock(snapshotMutex_);
        snapshotBuffer_ = mockData;
    }

    // --- Helper: Native POSIX TCP Client ---
    std::string fetchFromLocalhost(const std::string& requestPayload) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return "";

        // Initialized with {} to satisfy cppcoreguidelines-pro-type-member-init
        struct timeval timeout{}; 
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        struct sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(TEST_PORT);
        inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

        // Modern C++ cast to satisfy cppcheck
        if (connect(sock, reinterpret_cast<struct sockaddr*>(&serv_addr), sizeof(serv_addr)) < 0) {
            close(sock);
            return "";
        }

        send(sock, requestPayload.c_str(), requestPayload.length(), 0);

        std::string response;
        char buffer[4096] = {0};
        
        int bytesRead = read(sock, buffer, sizeof(buffer));
        if (bytesRead > 0) {
            response.assign(buffer, bytesRead);
        }
        
        close(sock);
        return response;
    }
};

// 1. Tests that unknown routes are safely rejected
TEST_F(WebServerTest, Returns404ForUnknownRoutes) {
    std::string response = fetchFromLocalhost("GET /invalid-route HTTP/1.1\r\n\r\n");
    
    EXPECT_FALSE(response.empty());
    EXPECT_NE(response.find("HTTP/1.1 404 Not Found"), std::string::npos);
}

// 2. Tests the specific Favicon caching route
TEST_F(WebServerTest, ReturnsFaviconNotFoundWithCacheHeaders) {
    std::string response = fetchFromLocalhost("GET /favicon.ico HTTP/1.1\r\n\r\n");
    
    EXPECT_FALSE(response.empty());
    EXPECT_NE(response.find("404 Not Found"), std::string::npos);
    EXPECT_NE(response.find("max-age=31536000"), std::string::npos); 
}

// 3. Tests the Snapshot pipeline end-to-end
TEST_F(WebServerTest, ServesSnapshotJpegData) {
    std::vector<uint8_t> mockJpeg = {0xFF, 0xD8, 0x01, 0x02, 0x03, 0xFF, 0xD9};
    
    // Safely inject via the protected proxy method
    injectMockSnapshot(mockJpeg);

    std::string response = fetchFromLocalhost("GET /snapshot HTTP/1.1\r\n\r\n");

    EXPECT_NE(response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(response.find("Content-Type: image/jpeg"), std::string::npos);
    
    std::string payloadString(mockJpeg.begin(), mockJpeg.end());
    EXPECT_NE(response.find(payloadString), std::string::npos) << "Binary JPEG payload was corrupted or missing.";
}

// 4. Tests the Dashboard route
TEST_F(WebServerTest, ServesWebDashboard) {
    std::string response = fetchFromLocalhost("GET /web HTTP/1.1\r\n\r\n");

    EXPECT_NE(response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(response.find("Content-Type: text/html"), std::string::npos);
}