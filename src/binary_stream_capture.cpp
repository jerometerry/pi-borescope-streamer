#include "usb_camera.hpp"
#include "device_info.hpp"
#include "device_finder.hpp"
#include "usb_frame_decoder.hpp"
#include "server_constants.hpp"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <array>

constexpr size_t CACHE_LINE_SIZE = 64;

static std::atomic<bool> running{true};
static std::mutex frameMutex;
static uint32_t frameId = 0;

static constexpr const char* DUMP_FILE = "raw_camera_dump.bin";

template <size_t QueueSize = 16>
class StreamDisruptor {
    static_assert((QueueSize & (QueueSize - 1)) == 0, "QueueSize must be a power of 2.");

    struct Slot {
        std::vector<uint8_t> buffer;
        Slot() {
            buffer.reserve(ServerConstants::ONE_MEGABYTE); 
        }
    };

    std::array<Slot, QueueSize> ring;

    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> published_sequence{0};
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> consumed_sequence{0};
    
    alignas(CACHE_LINE_SIZE) uint64_t cached_consumed_sequence{0};
    alignas(CACHE_LINE_SIZE) uint64_t next_claim_sequence{0};

public:
    std::vector<uint8_t>* claim() {
        while (next_claim_sequence - cached_consumed_sequence >= QueueSize) {
            if (!running.load(std::memory_order_relaxed)) return nullptr;
            
            cached_consumed_sequence = consumed_sequence.load(std::memory_order_acquire);
            if (next_claim_sequence - cached_consumed_sequence >= QueueSize) {
                std::this_thread::yield(); 
            }
        }
        
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        return &ring[next_claim_sequence & (QueueSize - 1)].buffer;
    }

    void publish() {
        next_claim_sequence++;
        published_sequence.store(next_claim_sequence, std::memory_order_release);
    }

    // --- THE GRACEFUL SHUTDOWN BARRIER ---
    const std::vector<uint8_t>* waitForAvailable(uint64_t sequence_to_consume) {
        while (published_sequence.load(std::memory_order_acquire) <= sequence_to_consume) {
            if (!running.load(std::memory_order_relaxed)) {
                // The Producer has signaled a shutdown.
                // We double-check the published sequence one final time to prevent a race condition
                // where the Producer published a frame exactly as the shutdown flag was flipped.
                if (published_sequence.load(std::memory_order_acquire) <= sequence_to_consume) {
                    return nullptr; 
                }
            }
            std::this_thread::yield();
        }
        
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        return &ring[sequence_to_consume & (QueueSize - 1)].buffer;
    }

    void release(uint64_t sequence_to_consume) {
        consumed_sequence.store(sequence_to_consume + 1, std::memory_order_release);
    }
};

void signalHandler(int signal) {
    std::cout << "\nSignal " << signal << " received. Initiating graceful shutdown...\n";
    running.store(false, std::memory_order_release);
}

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGPIPE, SIG_IGN);

    try {
        std::vector<DeviceInfo> cameras = DeviceFinder::all();
        if (cameras.empty()) {
            std::cerr << "No Useeplus supercamera devices found on the USB bus.\n";
            return EXIT_FAILURE;
        }

        DeviceInfo cameraInfo = cameras[0];
        
        if (cameras.size() > 1) {
            std::cout << "Multiple Useeplus cameras detected:\n";
            for (size_t i = 0; i < cameras.size(); ++i) {
                std::cout << "  [" << i << "] Bus " << static_cast<int>(cameras[i].bus)
                          << " Address " << static_cast<int>(cameras[i].address)
                          << " - " << cameras[i].manufacturer << " " << cameras[i].product
                          << "\n";
            }
            
            size_t choice = 0;
            while (true) {
                std::cout << "\nSelect camera to stream [0-" << (cameras.size() - 1) << "]: ";
                if (std::cin >> choice && choice < cameras.size()) {
                    cameraInfo = cameras[choice];
                    break;
                }
                std::cout << "Invalid selection.\n";
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            }
        }

        std::cout << "\n[Info] Binding stream to camera on Bus " << static_cast<int>(cameraInfo.bus)
                  << " Address " << static_cast<int>(cameraInfo.address) << "...\n";

        UsbCamera camera(cameraInfo);
        StreamDisruptor<16> disruptor;

        std::thread consumerThread([&]() {
            std::ofstream dumpFile(DUMP_FILE, std::ios::binary);
            if (!dumpFile) {
                std::cerr << "[Error] Failed to open " << DUMP_FILE << " for writing.\n";
                running.store(false, std::memory_order_release);
                return;
            }

            auto broadcastHandler = [](const std::vector<uint8_t>& frame) { 
                if (frame.empty()) return;
                std::scoped_lock<std::mutex> lock(frameMutex);
                frameId++;
            };
            
            UsbFrameDecoder decoder(broadcastHandler, [](){});
            uint64_t sequence = 0;

            std::cout << "[Consumer Thread] Ready. Waiting for sequence barriers...\n";

            // Loop infinitely. The break condition is purely driven by waitForAvailable returning nullptr.
            while (true) {
                const auto* buffer = disruptor.waitForAvailable(sequence);
                if (!buffer) {
                    std::cout << "[Consumer Thread] Ring buffer safely drained to disk. Shutting down.\n";
                    break;
                }

                dumpFile.write(reinterpret_cast<const char*>(buffer->data()), buffer->size());
                decoder.processIncomingCameraData(std::span<const uint8_t>{*buffer});

                disruptor.release(sequence);
                sequence++;
            }
        });

        std::cout << "[Producer Thread] Pipeline operational. Polling hardware at maximum velocity...\n";
        
        while (running.load(std::memory_order_relaxed)) {
            std::vector<uint8_t>* buffer = disruptor.claim();
            if (!buffer) break;

            int error = camera.read(*buffer);
            if (error == 0 && !buffer->empty()) {
                disruptor.publish();
            } else if (error == LIBUSB_ERROR_NO_DEVICE) {
                std::cerr << "[Producer Thread] Device disconnected.\n";
                running.store(false, std::memory_order_release);
                break;
            }
        }

        // Cleanup: Main thread will block here until the Consumer finishes draining the buffer.
        running.store(false, std::memory_order_release);
        if (consumerThread.joinable()) {
            consumerThread.join();
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[Fatal] Unhandled exception: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}