#pragma once
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <vector>

/** @brief A pipeline for managing shared frames
 */
class SharedFramePipeline {
public:
    /** @brief Construct a new shared frame pipeline instance
     */
    SharedFramePipeline();

    /** @brief Update the current frame with a new frame
     * @param newFrame The new frame to update with
     */
    void updateFrame(std::shared_ptr<std::vector<uint8_t>> newFrame);

    /** @brief Checkout a buffer from the pool
     * @return A shared pointer to the checked out buffer
     */
    std::shared_ptr<std::vector<uint8_t>> checkoutBuffer();

    /** @brief Return a buffer to the pool
     * @param buffer The buffer to return
     */
    void returnBuffer(std::shared_ptr<std::vector<uint8_t>> buffer);

    /** @brief Request a snapshot of the current frame
     */
    void requestSnapshot();

    /** @brief Get the current frame
     * @param outFrameId The frame ID of the current frame
     * @return A shared pointer to the current frame
     */
    std::shared_ptr<const std::vector<uint8_t>> getCurrentFrame(uint32_t& outFrameId) const;

    /** @brief Get the snapshot frame
     * @return A shared pointer to the snapshot frame
     */
    std::shared_ptr<const std::vector<uint8_t>> getSnapshot() const;
    
private:
    /** @brief A mutex for protecting the free pool
     */
    mutable std::mutex poolMutex_;

    /** @brief A mutex for protecting the active frame
     */
    mutable std::mutex activeMutex_;

    /** @brief The free pool of buffers
     */
    std::vector<std::shared_ptr<std::vector<uint8_t>>> freePool_;

    /** @brief The latest frame
     */
    std::shared_ptr<const std::vector<uint8_t>> latestFrame_;

    /** @brief The snapshot frame
     */
    std::shared_ptr<std::vector<uint8_t>> snapshotFrame_;

    /** @brief The frame ID
     */
    uint32_t frameId_{0};

    /** @brief Whether a snapshot capture is requested
     */
    bool captureSnapshotRequested_{false};

    /** @brief Whether the initial snapshot has been captured
     */
    mutable bool initialSnapshotCaptured_{false};
};