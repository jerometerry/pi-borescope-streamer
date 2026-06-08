#pragma once

#include <utility>

template <typename T>
class IntrusivePtr {
public:
    using element_type = T;

    constexpr IntrusivePtr() noexcept : ptr_(nullptr) {}
    constexpr IntrusivePtr(std::nullptr_t) noexcept : ptr_(nullptr) {}

    explicit IntrusivePtr(T* p) : ptr_(p) {
        if (ptr_) {
            intrusive_ptr_add_ref(ptr_);
        }
    }

    IntrusivePtr(const IntrusivePtr& other) : ptr_(other.ptr_) {
        if (ptr_) {
            intrusive_ptr_add_ref(ptr_);
        }
    }

    IntrusivePtr& operator=(const IntrusivePtr& other) {
        IntrusivePtr(other).swap(*this);
        return *this;
    }

    IntrusivePtr(IntrusivePtr&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

	IntrusivePtr& operator=(IntrusivePtr&& other) noexcept {
		if (this != &other) {
			if (ptr_) {
				intrusive_ptr_release(ptr_);
			}
			ptr_ = other.ptr_;
			other.ptr_ = nullptr;
		}
		return *this;
	}

    ~IntrusivePtr() {
        if (ptr_) {
            intrusive_ptr_release(ptr_);
        }
    }

    T* get() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    void reset() {
        IntrusivePtr().swap(*this);
    }

    void reset(T* p) {
        IntrusivePtr(p).swap(*this);
    }

    void swap(IntrusivePtr& other) noexcept {
        std::swap(ptr_, other.ptr_);
    }

private:
    T* ptr_ = nullptr;
};

template <typename T>
void swap(IntrusivePtr<T>& lhs, IntrusivePtr<T>& rhs) noexcept {
    lhs.swap(rhs);
}

template <typename T, typename U>
bool operator==(const IntrusivePtr<T>& lhs, const IntrusivePtr<U>& rhs) noexcept { return lhs.get() == rhs.get(); }

template <typename T>
bool operator==(const IntrusivePtr<T>& lhs, std::nullptr_t) noexcept { return lhs.get() == nullptr; }
