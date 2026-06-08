#include <gtest/gtest.h>
#include <cctype>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include "buffer.hpp"
#include "buffer_pool.hpp"
#include "buffer_ptr.hpp"
#include "intrusive_ptr.hpp"
#include "zero_allocation_response_builder.hpp"

class ZeroAllocationResponseBuilderTest : public ::testing::Test {
private:
    std::shared_ptr<BufferPool> bufferPool_;

protected:
    void SetUp() override {
        bufferPool_ = BufferPool::create();
    }

    void TearDown() override {
    }

    BufferPtr borrow() {
        return bufferPool_->borrow();
    }
};

TEST_F(ZeroAllocationResponseBuilderTest, Build) {

    auto ptr = borrow();
    auto* buffer = ptr.get();
    std::vector<uint8_t> payload = { 0xDE, 0xAD, 0xBE, 0xEF };
    ptr->insertContent(payload);

    auto response = ZeroAllocationResponseBuilder::build(buffer);

    EXPECT_EQ(response, 
        "--mjpegstream\r\nContent-Type: image/jpeg\r\nContent-Length: 4\r\n\r\n\xDE\xAD\xBE\xEF"
    );
}
