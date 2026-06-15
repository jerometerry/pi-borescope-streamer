#pragma once
#include "buffer.hpp"
#include "intrusive_ptr.hpp"

/**
 * @brief The standard, zero-allocation smart pointer used to pass video frames through the
 * pipeline.
 *
 * @details BufferPtr acts as the primary currency of the streaming architecture. It wraps a
 * raw Buffer with an intrusive reference count, ensuring that memory lifecycle management remains
 * completely detached from the heap.
 *
 * As this pointer is copied and moved between the USB capture thread, the single-frame queue,
 * and the network broadcast loop, it automatically tracks its active references. When the final
 * BufferPtr referencing a specific frame goes out of scope, it immediately triggers the
 * BufferRecycler, returning the underlying memory to the pool for the next hardware interrupt.
 */
using BufferPtr = IntrusivePtr<Buffer>;
