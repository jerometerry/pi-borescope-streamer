#pragma once

struct Buffer;

class BufferRecycler {
public:
    virtual void recycle(Buffer* buffer) = 0;

protected:
    // Protected non-virtual destructor ensures no one can accidentally 
    // `delete` the BufferPool through this interface pointer.
    ~BufferRecycler() = default; 
};