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

namespace {
    constexpr int TEST_PORT = 18080; 
}

class WebServerTest : public ::testing::Test {
private:
    std::atomic<bool> running_{true};
    uint32_t latestFrameId_{0};
    std::vector<uint8_t> frameBuffer_;
    std::mutex frameMutex_;
    std::vector<uint8_t> snapshotBuffer_;
    std::mutex snapshotMutex_;

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

    void injectMockSnapshot(const std::vector<uint8_t>& mockData) {
        std::scoped_lock<std::mutex> lock(snapshotMutex_);
        snapshotBuffer_ = mockData;
    }

    void injectMockVideoFrame(const std::vector<uint8_t>& mockData) {
        std::scoped_lock<std::mutex> lock(frameMutex_);
        frameBuffer_ = mockData;
        latestFrameId_++;
    }

    std::string fetchFromLocalhost(const std::string& requestPayload) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return "";

        struct timeval timeout{}; 
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        struct sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(TEST_PORT);
        inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

        if (connect(sock, reinterpret_cast<struct sockaddr*>(&serv_addr), sizeof(serv_addr)) < 0) {
            close(sock);
            return "";
        }

        send(sock, requestPayload.c_str(), requestPayload.length(), 0);

        std::string response;
        char buffer[4096] = {0};

        while (true) {
            std::fill(std::begin(buffer), std::end(buffer), 0);
            int bytesRead = read(sock, buffer, sizeof(buffer));

            if (bytesRead > 0) {
                response.append(buffer, bytesRead);
            } else {
                break; 
            }
        }
        
        close(sock);
        return response;
    }
};

TEST_F(WebServerTest, Returns404ForUnknownRoutes) {
    std::string response = fetchFromLocalhost("GET /invalid-route HTTP/1.1\r\n\r\n");
    
    EXPECT_FALSE(response.empty());
    EXPECT_NE(response.find("HTTP/1.1 404 Not Found"), std::string::npos);
}

TEST_F(WebServerTest, ReturnsFaviconNotFoundWithCacheHeaders) {
    std::string response = fetchFromLocalhost("GET /favicon.ico HTTP/1.1\r\n\r\n");
    
    EXPECT_FALSE(response.empty());
    EXPECT_NE(response.find("404 Not Found"), std::string::npos);
    EXPECT_NE(response.find("max-age=31536000"), std::string::npos); 
}

TEST_F(WebServerTest, ServesSnapshotJpegData) {
    std::vector<uint8_t> mockJpeg = {0xFF, 0xD8, 0x01, 0x02, 0x03, 0xFF, 0xD9};
    injectMockSnapshot(mockJpeg);

    std::string response = fetchFromLocalhost("GET /snapshot HTTP/1.1\r\n\r\n");

    EXPECT_NE(response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(response.find("Content-Type: image/jpeg"), std::string::npos);
    
    std::string payloadString(mockJpeg.begin(), mockJpeg.end());
    EXPECT_NE(response.find(payloadString), std::string::npos) << "Binary JPEG payload was corrupted or missing.";
}

TEST_F(WebServerTest, ServesWebDashboard) {
    std::string response = fetchFromLocalhost("GET /web HTTP/1.1\r\n\r\n");

    EXPECT_NE(response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(response.find("Content-Type: text/html"), std::string::npos);
}

TEST_F(WebServerTest, ServesContinuousMjpegStream) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(sock, 0);

    struct timeval timeout{};
    timeout.tv_sec = 2; 
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(TEST_PORT);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    ASSERT_GE(connect(sock, reinterpret_cast<struct sockaddr*>(&serv_addr), sizeof(serv_addr)), 0);

    std::string request = "GET / HTTP/1.1\r\n\r\n";
    send(sock, request.c_str(), request.length(), 0);

    char buffer[4096] = {0};
    int bytesRead = read(sock, buffer, sizeof(buffer));
    std::string response(buffer, bytesRead > 0 ? bytesRead : 0);
    EXPECT_NE(response.find("multipart/x-mixed-replace"), std::string::npos);

    std::vector<uint8_t> mockVideoFrame = {0xFF, 0xD8, 0xAA, 0xBB, 0xFF, 0xD9};
    injectMockVideoFrame(mockVideoFrame);

    std::string chunkResponse;
    std::string payloadString(mockVideoFrame.begin(), mockVideoFrame.end());
    
    auto startTime = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() - startTime < std::chrono::seconds(2)) {
        std::fill(std::begin(buffer), std::end(buffer), 0);
        bytesRead = read(sock, buffer, sizeof(buffer));
        
        if (bytesRead > 0) {
            chunkResponse.append(buffer, bytesRead);
        }

        if (chunkResponse.find(payloadString) != std::string::npos) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5)); 
    }

    EXPECT_NE(chunkResponse.find("--mjpegstream"), std::string::npos);
    EXPECT_NE(chunkResponse.find("Content-Type: image/jpeg"), std::string::npos);
    EXPECT_NE(chunkResponse.find(payloadString), std::string::npos) << "Stream chunk payload corrupted or incomplete.";

    close(sock);
}