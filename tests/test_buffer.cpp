#include <gtest/gtest.h>
#include <cstdint>
#include <utility>
#include <vector>

#include "buffer.hpp"
#include "buffer_ptr.hpp"
#include "buffer_recycler.hpp"
#include "intrusive_ptr.hpp"

namespace {

// Helper struct to track destruction when simulating a custom pool callback.
// Implements the BufferRecycler interface to intercept the intrusive pointer release.
struct DestructionTracker final : public BufferRecycler {
    int destroyedCount = 0;

    void recycle(Buffer* b) override {
        destroyedCount++;
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        delete b; 
    }
};

} // anonymous namespace

// -------------------------------------------------------------------
// INTRUSIVE POINTER LIFECYCLE TESTS
// -------------------------------------------------------------------

TEST(IntrusivePtrTest, StandaloneLifecycleNoLeak) {
    // Verifies that a Buffer created outside of a pool correctly defaults 
    // to std::default_delete when the intrusive pointer hits 0.
    // If this fails, AddressSanitizer/Valgrind will flag a memory leak.
    
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    BufferPtr ptr(new Buffer(128));
    EXPECT_TRUE(static_cast<bool>(ptr));
}

TEST(IntrusivePtrTest, CopySemanticsDeferDestruction) {
    DestructionTracker tracker;
    {
        // Pass the tracker to the constructor to wire up the recycler interface
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        BufferPtr master(new Buffer(128, &tracker));

        {
            // Copy construct - increments ref count
            // NOLINTBEGIN(performance-unnecessary-copy-initialization)
            [[maybe_unused]] BufferPtr copy1 = master;
            [[maybe_unused]] BufferPtr copy2 = master;
            // NOLINTEND(performance-unnecessary-copy-initialization)
            
            // Copy assign - increments ref count
            BufferPtr copy3;
            copy3 = master;

            EXPECT_EQ(tracker.destroyedCount, 0) << "Buffer destroyed prematurely while copies exist";
        } // copy1, copy2, copy3 go out of scope, ref count drops but doesn't hit 0
        
        EXPECT_EQ(tracker.destroyedCount, 0) << "Buffer destroyed prematurely while master exists";
    } // master goes out of scope, ref count hits 0
    
    EXPECT_EQ(tracker.destroyedCount, 1) << "Buffer callback not fired exactly once";
}

TEST(IntrusivePtrTest, MoveAssignmentTransfersOwnership) {
    DestructionTracker tracker;
    
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    BufferPtr ptr1(new Buffer(128, &tracker));

    Buffer* rawPointer = ptr1.get();

    BufferPtr ptr2;
    ptr2 = std::move(ptr1); // Move assignment

    EXPECT_FALSE(static_cast<bool>(ptr1)) << "Moved-from pointer should be empty"; // NOLINT
    EXPECT_TRUE(static_cast<bool>(ptr2)) << "Target pointer should hold the resource";
    EXPECT_EQ(ptr2.get(), rawPointer) << "Underlying pointer address changed during move assignment";
    EXPECT_EQ(tracker.destroyedCount, 0) << "Destruction triggered during move";

    // Reassigning ptr2 to a new buffer should trigger destruction of the old one
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    ptr2 = BufferPtr(new Buffer(128));

    EXPECT_EQ(tracker.destroyedCount, 1) << "Original buffer not destroyed upon pointer reassignment";
}

TEST(IntrusivePtrTest, SelfAssignmentIsSafe) {
    DestructionTracker tracker;
    
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    BufferPtr ptr(new Buffer(128, &tracker));

    // Use a reference to trick the compiler's static analysis 
    // and avoid compiler-specific #pragma directives.
    BufferPtr& ptrRef = ptr;
    ptr = ptrRef;

    EXPECT_TRUE(static_cast<bool>(ptr));
    EXPECT_EQ(tracker.destroyedCount, 0) << "Self-assignment triggered premature destruction";
}

TEST(IntrusivePtrTest, ResetDropsReference) {
    DestructionTracker tracker;
    
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    BufferPtr ptr(new Buffer(128, &tracker));

    // Explicitly dropping the reference
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

// -------------------------------------------------------------------
// RAW BUFFER MEMORY & REF COUNTING TESTS
// -------------------------------------------------------------------

TEST(BufferMemoryTest, RawRetainReleaseLogic) {
    // We instantiate manually without IntrusivePtr to directly test the atomic boundaries
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto* b = new Buffer(128);
    
    // Explicitly retain (simulating ptr copies)
    b->retain(); // Ref count = 1
    b->retain(); // Ref count = 2
    b->retain(); // Ref count = 3

    EXPECT_FALSE(b->release()) << "Release returned true but ref count should be 2";
    EXPECT_FALSE(b->release()) << "Release returned true but ref count should be 1";
    EXPECT_TRUE(b->release()) << "Release returned false but ref count should have hit 0";

    // Since we didn't use IntrusivePtr to wrap this, we must manually delete 
    // to fulfill what intrusive_ptr_release would normally do.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    delete b;
}

TEST(BufferMemoryTest, ReserveExpandsTotalCapacity) {
    Buffer b(128); // Initialized with 128 bytes of padding
    
    size_t initialCapacity = b.totalCapacity();
    size_t newRequiredContentSpace = initialCapacity + 1024;

    b.reserve(newRequiredContentSpace);

    EXPECT_GE(b.totalCapacity(), newRequiredContentSpace) 
        << "Buffer capacity did not expand to meet or exceed reserved request";
        
    EXPECT_EQ(b.paddingSize(), 128) 
        << "Buffer expansion corrupted the reserved padding prefix";
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