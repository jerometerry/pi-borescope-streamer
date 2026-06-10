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
#include <span>
#include <string>
#include <thread>
#include <vector>
#include "constants.hpp"
#include "frame.hpp"
#include "frame_disruptor.hpp"
#include "mjpeg_server.hpp"

namespace {
    constexpr int TEST_PORT = 18080; 

    std::string toLowerString(std::string str) {
        std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c){ return std::tolower(c); });
        return str;
    }
}

class MjpegServerTest : public ::testing::Test {
private:
    std::atomic<bool> running_{true};
    FrameDisruptor disruptor_;
    MjpegServer server_;

public:
    MjpegServerTest() :
        disruptor_(), 
        server_(TEST_PORT, running_, disruptor_)
    {
        disruptor_.preAllocate(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);
    }   

protected:
    void SetUp() override {
        int64_t seq = disruptor_.claim();
        disruptor_.publish(seq);

        server_.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void TearDown() override {
        running_ = false;
    }

    void injectMockVideoFrame(const std::vector<uint8_t>& data) {
        int64_t seq = disruptor_.claim();
        Frame& slot = disruptor_.getBySequence(seq);

        slot.clear();
        slot.insertContent(data);
        
        disruptor_.publish(seq);
    }

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
                break; 
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

TEST_F(MjpegServerTest, ServesWebDashboard) {
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

    std::string request = "GET /stream HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: keep-alive\r\n\r\n";
    send(sock, request.c_str(), request.length(), 0);

    std::string chunkResponse;
    char buffer[4096];
    bool headersReceived = false;
    
    auto startTime = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() - startTime < std::chrono::seconds(2)) {
        int bytesRead = read(sock, buffer, sizeof(buffer));
        
        if (bytesRead > 0) {
            chunkResponse.append(buffer, bytesRead);
            if (toLowerString(chunkResponse).find("multipart/x-mixed-replace") != std::string::npos) {
                headersReceived = true;
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5)); 
    }

    ASSERT_TRUE(headersReceived) << "Server did not respond with stream headers.";

    std::vector<uint8_t> mockVideoFrame = {0xFF, 0xD8, 0xAA, 0xBB, 0xFF, 0xD9};
    injectMockVideoFrame(mockVideoFrame);

    std::string payloadString(mockVideoFrame.begin(), mockVideoFrame.end());
    bool payloadReceived = false;

    startTime = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() - startTime < std::chrono::seconds(2)) {
        int bytesRead = read(sock, buffer, sizeof(buffer));
        
        if (bytesRead > 0) {
            chunkResponse.append(buffer, bytesRead);
            if (chunkResponse.find(payloadString) != std::string::npos) {
                payloadReceived = true;
                break;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(5)); 
    }

    EXPECT_TRUE(payloadReceived) << "Stream chunk payload corrupted or incomplete.";

    close(sock);
}
