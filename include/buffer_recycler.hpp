#pragma once

class Buffer;

/**
 * @brief Interface to break the cyclic dependency between BufferPool and Buffer for recycling Buffers once they are 
 * no longer referenced.
 *
 * @details Buffer is wrapped in an IntrusivePtr<Buffer>, which is typedefed to BufferPtr = IntrusivePtr<Buffer>.
 * IntrusivePtr implements the intrusive pointer pattern, which embeds the reference count inside the IntrusivePtr 
 * class. BufferPool returns instances of BufferPtr (aka IntrusivePtr<Buffer>), which are allocated on the stack,
 * avoiding heap allocations (via malloc) which occur when using std::shared_ptr<Buffer>.
 *
 * std::shared_ptr<Buffer> is perfectly fine to use, if you don't mind a small amount of memory being allocated on the 
 * heap for the internal control block that shared_ptr uses. Arguably for this project the intrusive pointer pattern 
 * is overkill. I wanted to see if I could get zero allocations on the hot path, and this was the last hurdle to
 * overcome.
 */
class BufferRecycler {
public:
    /**
     * @brief Called automatically by IntrusivePtr when a Buffer's reference count hits zero.
     * @param buffer The naked pointer to the Buffer that is ready to be reused.
     */
    virtual void recycle(Buffer* buffer) = 0;

protected:
    ~BufferRecycler() = default; 
};
