#pragma once
#include <atomic>
#include <functional>
class SharedFramePipeline;
class HardwareButtonManager;
class WebServer;
struct DeviceInfo;

class MjpegStream {
public:
    MjpegStream(SharedFramePipeline& pipeline, 
                HardwareButtonManager& buttonManager, 
                WebServer& server,
                std::atomic<bool>& running);
    ~MjpegStream() = default;

    int run(const DeviceInfo& target);
    void stop();

private:
    std::reference_wrapper<SharedFramePipeline> pipeline_;
    std::reference_wrapper<HardwareButtonManager> buttonManager_;
    std::reference_wrapper<WebServer> server_;
    std::reference_wrapper<std::atomic<bool>> running_;

};
