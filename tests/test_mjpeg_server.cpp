#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <compare>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "mjpeg_server.hpp"
#include "shared_frame_pipeline.hpp"

namespace {
    constexpr int TEST_PORT = 18080; 

    // Helper to safely check HTTP headers regardless of how the framework capitalizes them
    std::string toLowerString(std::string str) {
        std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c){ return std::tolower(c); });
        return str;
    }
}

class MjpegServerTest : public ::testing::Test {
private:
    std::atomic<bool> running_{true};
    SharedFramePipeline pipeline_;
    std::unique_ptr<MjpegServer> server_;

protected:
    void SetUp() override {
        server_ = std::make_unique<MjpegServer>(
            TEST_PORT, 
            running_, 
            pipeline_
        );
        
        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void TearDown() override {
        running_ = false;
        server_.reset(); 
    }

    void injectMockSnapshot(const std::vector<uint8_t>& mockData) {
        pipeline_.requestSnapshot();
        auto buffer = pipeline_.checkoutBuffer();
        if (buffer) {
            buffer->assign(mockData.begin(), mockData.end());
            pipeline_.updateFrame(std::move(buffer));
        }
    }

    void injectMockVideoFrame(const std::vector<uint8_t>& mockData) {
        auto buffer = pipeline_.checkoutBuffer();
        if (buffer) {
            buffer->assign(mockData.begin(), mockData.end());
            pipeline_.updateFrame(std::move(buffer));
        }
    }

    // Upgraded to act as a fully compliant HTTP/1.1 client
    std::string fetchFromLocalhost(const std::string& route) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return "";

        struct timeval timeout{}; 
        timeout.tv_sec = 2;
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

        // We MUST send a 'Host' header for HTTP/1.1 compliance.
        // We MUST send 'Connection: close' so the server hangs up, unblocking our read() loop.
        std::string requestPayload = "GET " + route + " HTTP/1.1\r\n"
                                     "Host: 127.0.0.1\r\n"
                                     "Connection: close\r\n\r\n";
                                     
        send(sock, requestPayload.c_str(), requestPayload.length(), 0);

        std::string response;
        char buffer[4096];

        while (true) {
            int bytesRead = read(sock, buffer, sizeof(buffer));
            if (bytesRead > 0) {
                response.append(buffer, bytesRead);
            } else {
                break; // Server cleanly closed the socket (0) or timeout (-1)
            }
        }
        
        close(sock);
        return response;
    }
};

TEST_F(MjpegServerTest, Returns404ForUnknownRoutes) {
    std::string response = fetchFromLocalhost("/invalid-route");
    std::string lowerResponse = toLowerString(response);
    
    EXPECT_FALSE(response.empty());
    EXPECT_NE(lowerResponse.find("404 not found"), std::string::npos);
}

TEST_F(MjpegServerTest, ReturnsFaviconNotFoundWithCacheHeaders) {
    std::string response = fetchFromLocalhost("/favicon.ico");
    std::string lowerResponse = toLowerString(response);
    
    EXPECT_FALSE(response.empty());
    EXPECT_NE(lowerResponse.find("404 not found"), std::string::npos);
    EXPECT_NE(lowerResponse.find("max-age=31536000"), std::string::npos); 
}

TEST_F(MjpegServerTest, ServesSnapshotJpegData) {
    std::vector<uint8_t> mockJpeg = {0xFF, 0xD8, 0x01, 0x02, 0x03, 0xFF, 0xD9};
    injectMockSnapshot(mockJpeg);

    std::string response = fetchFromLocalhost("/snapshot");
    std::string lowerResponse = toLowerString(response);

    EXPECT_NE(lowerResponse.find("200 ok"), std::string::npos);
    EXPECT_NE(lowerResponse.find("content-type: image/jpeg"), std::string::npos);
    
    // We check the original 'response' (not lowercased) to preserve the binary integrity 
    std::string payloadString(mockJpeg.begin(), mockJpeg.end());
    EXPECT_NE(response.find(payloadString), std::string::npos) << "Binary JPEG payload was corrupted or missing.";
}

TEST_F(MjpegServerTest, ServesWebDashboard) {
    // Note: The UI is now hosted at the root endpoint '/'
    std::string response = fetchFromLocalhost("/");
    std::string lowerResponse = toLowerString(response);

    EXPECT_NE(lowerResponse.find("200 ok"), std::string::npos);
    EXPECT_NE(lowerResponse.find("content-type: text/html"), std::string::npos);
}

TEST_F(MjpegServerTest, ServesContinuousMjpegStream) {
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

    std::string request = "GET /stream HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    send(sock, request.c_str(), request.length(), 0);

    // IMMEDIATE INJECTION: We must give the loop something to broadcast immediately,
    // otherwise uWebSockets will "cork" the initial HTTP headers waiting for a payload!
    std::vector<uint8_t> mockVideoFrame = {0xFF, 0xD8, 0xAA, 0xBB, 0xFF, 0xD9};
    injectMockVideoFrame(mockVideoFrame);

    std::string chunkResponse;
    std::string payloadString(mockVideoFrame.begin(), mockVideoFrame.end());
    char buffer[4096];
    
    auto startTime = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() - startTime < std::chrono::seconds(2)) {
        int bytesRead = read(sock, buffer, sizeof(buffer));
        if (bytesRead > 0) {
            chunkResponse.append(buffer, bytesRead);
        }

        if (chunkResponse.find(payloadString) != std::string::npos) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5)); 
    }

    std::string lowerResponse = toLowerString(chunkResponse);
    EXPECT_NE(lowerResponse.find("--mjpegstream"), std::string::npos);
    EXPECT_NE(lowerResponse.find("content-type: image/jpeg"), std::string::npos);
    EXPECT_NE(chunkResponse.find(payloadString), std::string::npos) << "Stream chunk payload corrupted or incomplete.";

    close(sock);
}