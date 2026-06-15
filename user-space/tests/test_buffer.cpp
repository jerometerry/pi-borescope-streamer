#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "buffer.hpp"
#include "buffer_ptr.hpp"
#include "buffer_recycler.hpp"
#include "intrusive_ptr.hpp"

namespace {
struct DestructionTracker final : public BufferRecycler {
    int destroyedCount = 0;

    void recycle(Buffer* b) override {
        destroyedCount++;
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        delete b;
    }
};
}  // namespace

TEST(IntrusivePtrTest, StandaloneLifecycleNoLeak) {
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    BufferPtr ptr(new Buffer(128));
    EXPECT_TRUE(static_cast<bool>(ptr));
}

TEST(IntrusivePtrTest, CopySemanticsDeferDestruction) {
    DestructionTracker tracker;
    {
        BufferPtr master(new Buffer(128, &tracker));

        {
            // NOLINTBEGIN(performance-unnecessary-copy-initialization)
            [[maybe_unused]] BufferPtr copy1 = master;
            [[maybe_unused]] BufferPtr copy2 = master;
            // NOLINTEND(performance-unnecessary-copy-initialization)

            BufferPtr copy3;
            copy3 = master;

            EXPECT_EQ(tracker.destroyedCount, 0)
                << "Buffer destroyed prematurely while copies exist";
        }
        EXPECT_EQ(tracker.destroyedCount, 0) << "Buffer destroyed prematurely while master exists";
    }

    EXPECT_EQ(tracker.destroyedCount, 1) << "Buffer callback not fired exactly once";
}

TEST(IntrusivePtrTest, MoveAssignmentTransfersOwnership) {
    DestructionTracker tracker;

    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    BufferPtr ptr1(new Buffer(128, &tracker));

    Buffer* rawPointer = ptr1.get();

    BufferPtr ptr2;
    ptr2 = std::move(ptr1);

    bool isNull = static_cast<bool>(ptr1);  // NOLINT(bugprone-use-after-move)

    EXPECT_FALSE(isNull) << "Moved-from pointer should be empty";
    EXPECT_TRUE(static_cast<bool>(ptr2)) << "Target pointer should hold the resource";
    EXPECT_EQ(ptr2.get(), rawPointer)
        << "Underlying pointer address changed during move assignment";
    EXPECT_EQ(tracker.destroyedCount, 0) << "Destruction triggered during move";

    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    ptr2 = BufferPtr(new Buffer(128));

    EXPECT_EQ(tracker.destroyedCount, 1)
        << "Original buffer not destroyed upon pointer reassignment";
}

TEST(IntrusivePtrTest, SelfAssignmentIsSafe) {
    DestructionTracker tracker;

    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    BufferPtr ptr(new Buffer(128, &tracker));

    BufferPtr& ptrRef = ptr;
    ptr = ptrRef;

    EXPECT_TRUE(static_cast<bool>(ptr));
    EXPECT_EQ(tracker.destroyedCount, 0) << "Self-assignment triggered premature destruction";
}

TEST(IntrusivePtrTest, ResetDropsReference) {
    DestructionTracker tracker;

    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    BufferPtr ptr(new Buffer(128, &tracker));

    ptr = nullptr;

    EXPECT_FALSE(static_cast<bool>(ptr));
    EXPECT_EQ(tracker.destroyedCount, 1) << "Resetting pointer did not trigger destruction";
}

TEST(IntrusivePtrTest, SwapExchangesOwnership) {
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    BufferPtr p1(new Buffer(128));
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    BufferPtr p2(new Buffer(256));

    Buffer* raw1 = p1.get();
    Buffer* raw2 = p2.get();

    std::swap(p1, p2);

    EXPECT_EQ(p1.get(), raw2) << "Pointer 1 did not receive Pointer 2's data";
    EXPECT_EQ(p2.get(), raw1) << "Pointer 2 did not receive Pointer 1's data";
}

TEST(BufferMemoryTest, RawRetainReleaseLogic) {
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto* b = new Buffer(128);

    b->retain();
    b->retain();
    b->retain();

    EXPECT_FALSE(b->release()) << "Release returned true but ref count should be 2";
    EXPECT_FALSE(b->release()) << "Release returned true but ref count should be 1";
    EXPECT_TRUE(b->release()) << "Release returned false but ref count should have hit 0";

    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    delete b;
}

TEST(BufferMemoryTest, ReserveExpandsTotalCapacity) {
    Buffer b(128);

    size_t initialCapacity = b.totalCapacity();
    size_t newRequiredContentSpace = initialCapacity + 1024;

    b.reserve(newRequiredContentSpace);

    EXPECT_GE(b.totalCapacity(), newRequiredContentSpace)
        << "Buffer capacity did not expand to meet or exceed reserved request";

    EXPECT_EQ(b.paddingSize(), 128) << "Buffer expansion corrupted the reserved padding prefix";
}

TEST(BufferMemoryTest, ClearMaintainsReservedCapacity) {
    Buffer b(128);
    b.reserve(1024);

    size_t expandedCapacity = b.totalCapacity();

    std::vector<uint8_t> payload(500, 0xFF);
    b.insertContent(payload);

    b.clear();

    EXPECT_TRUE(b.empty()) << "Buffer is not empty after clear";
    EXPECT_EQ(b.totalCapacity(), expandedCapacity)
        << "Buffer released underlying memory during clear, defeating zero-allocation purpose";
}
