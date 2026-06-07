#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Mjpeg {
    struct Buffer;
    using ReturnCallback = void(*)(void*, Buffer*);
}

namespace Mjpeg {
    struct Buffer {
public:
		explicit Buffer(const size_t paddingSize);

        static std::unique_ptr<Mjpeg::Buffer> unique();

        static std::unique_ptr<Mjpeg::Buffer> unique(size_t padding);

        void retain();
        
        void release();

        void clear();

        bool empty() const;

        void reserve(size_t size);

        void trim(size_t startOffset, size_t endOffset);

        void setPoolContext(void* context);

        void setReturnCallback(ReturnCallback callback);

        void insertContent(std::span<const uint8_t> content);

        uint8_t front() const;

        size_t contentSize() const;

		size_t paddingSize() const;

		size_t prefixSize() const;

        size_t totalSize() const;

        size_t totalCapacity() const;

		std::span<const uint8_t> getContentSlice() const;

		std::span<const uint8_t> getPaddingSlice() const;

		std::span<const uint8_t> getPrefixSlice() const;

		std::span<const uint8_t> all() const;

        std::span<uint8_t> getMutableContentSlice();

		std::span<uint8_t> getMutablePaddingSlice();

        std::span<uint8_t> getMutablePrefixSlice();

    private:
		void ensurePaddingReserved();

		void ensurePrefixReserved();

        std::vector<uint8_t>& data();

		std::atomic<int> refCount_{0};
        std::vector<uint8_t> data_;
        ReturnCallback returnCallback_{nullptr};
        void* poolContext_{nullptr};
		const size_t paddingSize_;
    };
}