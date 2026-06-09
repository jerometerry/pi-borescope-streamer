#pragma once
#include <utility>
#include <concepts>

/**
 * @brief IntrusivePtr exists to eliminate malloc calls associated with std::shared_ptr for reference counting. 
 * 
 * @tparam T - the type to wrap with an IntrusivePtr, to give it referencing counting / auto deletion. 
 *
 * @details  Buffer is wrapped in an IntrusivePtr<Buffer>, which is typedefed to BufferPtr = IntrusivePtr<Buffer>.
 * IntrusivePtr implements the intrusive pointer pattern, which embeds the reference count inside the IntrusivePtr 
 * class. BufferPool returns instances of BufferPtr (aka IntrusivePtr<Buffer>), which are allocated on the stack,
 * avoiding heap allocations (via malloc) which occur when using std::shared_ptr<Buffer>.
 *
 * std::shared_ptr<Buffer> is perfectly fine to use, if you don't mind a small amount of memory being allocated on the 
 * heap for the internal control block that shared_ptr uses. Arguably for this project the intrusive pointer pattern 
 * is overkill. I wanted to see if I could get zero allocations on the hot path, and this was the last hurdle to
 * overcome.
 */
template <typename T>
class IntrusivePtr {
public:
    using element_type = T;

    constexpr IntrusivePtr() noexcept : ptr_(nullptr) {}

    // cppcheck-suppress noExplicitConstructor
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
        if (this != &other) {
            T* old_ptr = ptr_;
            ptr_ = other.ptr_;
            if (ptr_) {
                intrusive_ptr_add_ref(ptr_);
            }
            if (old_ptr) {
                intrusive_ptr_release(old_ptr);
            }
        }
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
requires requires(T* t, U* u) { t == u; } // Verifies types are legally comparable
bool operator==(const IntrusivePtr<T>& lhs, const IntrusivePtr<U>& rhs) noexcept { 
    return lhs.get() == rhs.get(); 
}

template <typename T, typename U>
requires requires(T* t, U* u) { t != u; }
bool operator!=(const IntrusivePtr<T>& lhs, const IntrusivePtr<U>& rhs) noexcept { 
    return lhs.get() != rhs.get(); 
}

template <typename T, typename U>
requires requires(T* t, U* u) { t < u; }
bool operator<(const IntrusivePtr<T>& lhs, const IntrusivePtr<U>& rhs) noexcept { 
    return lhs.get() < rhs.get(); 
}

template <typename T>
bool operator==(const IntrusivePtr<T>& lhs, std::nullptr_t) noexcept { return lhs.get() == nullptr; }

template <typename T>
bool operator!=(const IntrusivePtr<T>& lhs, std::nullptr_t) noexcept { return lhs.get() != nullptr; }

template <typename T>
bool operator==(std::nullptr_t, const IntrusivePtr<T>& rhs) noexcept { return nullptr == rhs.get(); }

template <typename T>
bool operator!=(std::nullptr_t, const IntrusivePtr<T>& rhs) noexcept { return nullptr != rhs.get(); }

namespace std {
    template <typename T>
    struct hash<IntrusivePtr<T>> {
        size_t operator()(const IntrusivePtr<T>& p) const noexcept {
            return std::hash<T*>{}(p.get());
        }
    };
}
