#include "mjpeg_stream.hpp"
#include "shared_frame_pipeline.hpp"
#include "hardware_button_manager.hpp"
#include "usb_capture_engine.hpp"
#include "web_server.hpp"

MjpegStream::MjpegStream(const ServerTime& serverTime) : serverTime(serverTime) {}
MjpegStream::~MjpegStream() = default;

int MjpegStream::run(int port, const DeviceInfo& target) {
    running = true;

    // 1. Initialize Shared State Container
    SharedFramePipeline pipeline;

    // 2. Initialize Logic Controller
    HardwareButtonManager buttonManager(serverTime);

    // 3. Inject shared pipeline state into your Async WebServer
    // (Presuming WebServer is refactored to take pipeline instead of 6 raw references)
    WebServer server(port, running, pipeline);
    if (!server.initialize()) {
        return 1;
    }

    // 4. Initialize Hardware Capture Worker Engine via Dependency Injection
    UsbCaptureEngine captureEngine(pipeline, buttonManager, running);
    captureEngine.start(target);

    // 5. Start Web Server on the primary thread
    server.start();

    // 6. Clean down application services upon exit signal
    captureEngine.stop();
    return EXIT_SUCCESS;
}

void MjpegStream::stop() {
    running = false;
}
