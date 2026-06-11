/**
 * @file mjpeg_stream_capture.cpp
 * @brief A hardcore debugging tool that rips raw data straight off the USB cable to a file.
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <print>
#include <span>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>
#include "constants.hpp"
#include "disruptor.hpp"
#include "video_frame.hpp"
#include "video_frame_buffer.hpp"
#include "usb_device_finder.hpp"
#include "usb_device_info.hpp"
#include "usb_driver.hpp"

namespace {
    static std::atomic<bool> running{true};

    struct alignas(disruptor::cache_line_size) PipelineMetrics {
        std::atomic<uint64_t> totalFramesReceived{0};
        std::atomic<uint64_t> totalFramesDropped{0};
    };
    
    static PipelineMetrics metrics;
}

void signalHandler(int /*signum*/) {
    running.store(false, std::memory_order_relaxed);
}

bool selectCamera(UsbDeviceInfo& cameraInfo) {
    std::vector<UsbDeviceInfo> cameras = UsbDeviceFinder::superCameras();
    if (cameras.empty()) {
        std::cerr << "[Error] No Useeplus supercamera devices found on the USB bus.\n";
        return false;
    }

    cameraInfo = cameras[0];
    
    if (cameras.size() > 1) {
        std::cout << "Multiple Useeplus cameras detected:\n";
        for (size_t i = 0; i < cameras.size(); ++i) {
            std::cout << "  [" << i << "] Bus " << static_cast<int>(cameras[i].bus)
                      << " Address " << static_cast<int>(cameras[i].address)
                      << " - " << cameras[i].manufacturer << " " << cameras[i].product
                      << " (Serial: " << (cameras[i].serialNumber.empty() ? "N/A" : cameras[i].serialNumber) << ")\n";
        }
        
        size_t choice = 0;
        while (true) {
            std::cout << "\nSelect camera to stream [0-" << (cameras.size() - 1) << "]: ";
            if (std::cin >> choice && choice < cameras.size()) {
                cameraInfo = cameras[choice];
                break;
            }
            std::cout << "Invalid selection. Please try again.\n";
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
    }

    return true;
}

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGPIPE, SIG_IGN);

    try {
        UsbDeviceInfo camera;
        if (!selectCamera(camera)) {
            return EXIT_FAILURE;
        }

        std::cout << "\n[Info] Binding stream to camera on Bus " << static_cast<int>(camera.bus)
                  << " Address " << static_cast<int>(camera.address) << "...\n";

        VideoFrameBuffer ringBuffer;
        ringBuffer.preAllocate(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);

        std::jthread metricsMonitor([](const std::stop_token& st) {
            uint64_t last_received = 0;
            uint64_t last_dropped = 0;
            auto last_time = std::chrono::steady_clock::now();

            std::cout << "[Metrics] Monitoring engine activated.\n";

            while (!st.stop_requested() && running.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::seconds(1));

                uint64_t current_received = metrics.totalFramesReceived.load(std::memory_order_relaxed);
                uint64_t current_dropped = metrics.totalFramesDropped.load(std::memory_order_relaxed);
                auto current_time = std::chrono::steady_clock::now();

                std::chrono::duration<double> elapsed = current_time - last_time;
                double seconds = elapsed.count();

                if (seconds > 0.0) {
                    uint64_t delta_received = current_received - last_received;
                    uint64_t delta_dropped = current_dropped - last_dropped;
                    uint64_t total_attempted = delta_received + delta_dropped;

                    double fps = delta_received / seconds;
                    double drop_rate = (total_attempted > 0) 
                        ? (static_cast<double>(delta_dropped) / total_attempted) * 100.0 
                        : 0.0;

                    if (delta_dropped > 0) {
                        std::print("[METRICS] Ingestion: {:.2f} FPS | WARNING: Dropped {} frames ({:.1f}% drop rate) due to disk saturation\n", 
                                   fps, delta_dropped, drop_rate);
                    } else if (delta_received > 0) {
                        std::print("[METRICS] Ingestion: {:.2f} FPS | Health: 100% | Total Processed: {}\n", 
                                   fps, current_received);
                    }
                }

                last_received = current_received;
                last_dropped = current_dropped;
                last_time = current_time;
            }
            std::cout << "[Metrics] Monitoring engine stopped.\n";
        });

        std::jthread diskWriter([&ringBuffer]() {
            std::ofstream outFile("camera_stream.mjpeg", std::ios::out | std::ios::binary);
            int64_t next_read = 0;
            bool keep_running = true;

            // Notice: The loop relies entirely on the pipeline architecture, not flags.
            while (keep_running) {
                int64_t available = ringBuffer.waitFor(next_read);

                while (next_read <= available) {
                    VideoFrame& slot = ringBuffer.getBySequence(next_read);

                    // 1. Sentinel / Poison Pill Detection (Graceful Shutdown)
                    if (slot.active_size == 0) {
                        keep_running = false;
                        break;
                    }

                    // 2. Safely write to disk and check for OS errors (e.g., Disk Full)
                    if (!outFile.write(reinterpret_cast<const char*>(slot.getContentSlice().data()), slot.active_size)) {
                        std::cerr << "\n[Fatal] Failed to write to disk. Is the drive full?\n";
                        running.store(false, std::memory_order_relaxed); // Trigger global app shutdown
                        keep_running = false;
                        break;
                    }
                    
                    next_read++;
                }
                // Mark consumed safely outside the batch processing loop
                ringBuffer.markConsumed(next_read - 1);
            }
            std::cout << "[Disk Writer] Output file closed securely. Stream capture finalized.\n";
        });

        auto transfer = [&](UsbTransferStatus status, std::span<const uint8_t> payload) -> bool {
            if (status == UsbTransferStatus::Completed && !payload.empty()) {
                auto seq_opt = ringBuffer.tryClaim();
                
                if (seq_opt.has_value()) {
                    int64_t seq = *seq_opt;
                    VideoFrame& slot = ringBuffer.getBySequence(seq);
                    slot.insertContent(payload);
                    ringBuffer.publish(seq);

                    metrics.totalFramesReceived.fetch_add(1, std::memory_order_relaxed);
                } else {
                    metrics.totalFramesDropped.fetch_add(1, std::memory_order_relaxed);
                }
            }
            // Return false to kill the callback chain if the app is shutting down
            return running.load(std::memory_order_relaxed) && status != UsbTransferStatus::Disconnected; 
        };

        UsbDriver driver(transfer, &running);
        std::cout << "[Server Core] Starting asynchronous capture and network worker engines...\n";
        driver.start(camera);

        std::cout << "[Server Core] System fully operational. Awaiting network events. Press Ctrl+C to stop.\n";
        
        while (running.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "\n[Server Core] Shutdown signal received. Stopping worker lanes...\n";

        // 1. Stop the upstream hardware producer
        driver.stop();

        // 2. Inject the Poison Pill to wake up and kill the downstream consumer
        int64_t seq = ringBuffer.claim();
        ringBuffer.getBySequence(seq).active_size = 0; 
        ringBuffer.publish(seq);

        // jthreads automatically join and clean up here on scope exit
        return EXIT_SUCCESS;

    } catch (const std::exception& e) {
        std::cerr << "[Fatal] Unhandled exception: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}