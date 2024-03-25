
#include <cassert>
#include <utility>
#include <new>
#include <limits>
#include <algorithm>
#include "slice.hpp"
#include "socket.hpp"

#define MAX_VALUE(X) std::numeric_limits<decltype(X)>::max()

struct Buffer {

    char *data; // Buffer content (or NULL when size=0)
    int   size; // Number of bytes stored in the buffer
    int   used; // Allocated bytes
    bool  fail; // True if at least one read or write operation failed at one point. No read or write operations can be performed after this is set.

    int crlfcrlf; // Cache for the result of seek(\r\n\r\n)

    bool ensure_unused_space(int min)
    {
        assert(!fail);

        if (used + min > size) {
            // Allocation of a new buffer is necessary
            int   new_size = size > 0 ? 2 * size : 256;
            char *new_data = new (std::nothrow) char[new_size];
            if (new_data == nullptr) {
                fail = true;
                return false;
            }
            memcpy(new_data, data, used);
            delete[] data;
            data = new_data;
            size = new_size;
        }

        return true;
    }

public:
    Buffer()
    {
        data = nullptr;
        size = 0;
        used = 0;
        fail = false;
        crlfcrlf = -1;
    }

    Buffer(Buffer&) = delete;
    Buffer& operator=(Buffer& other) = delete;
    
    Buffer(Buffer&& other)
    {
        data = other.data;
        size = other.size;
        used = other.used;
        fail = other.fail;
        other.data = nullptr;
        other.size = 0;
        other.used = 0;
        other.fail = false;
    }

    Buffer& operator=(Buffer&& other)
    {
        if (this != &other) {
            delete[] data;
            data = other.data;
            size = other.size;
            used = other.used;
            fail = other.fail;
            other.data = nullptr;
            other.size = 0;
            other.used = 0;
            other.fail = false;
        }
        return *this;
    }

    ~Buffer()
    {
        delete[] data;
    }

    int length() const
    {
        return used;
    }

    bool failed() const
    {
        return fail;
    }

    void overwrite(int off, const char *src, int len)
    {
        if (fail) return;

        if (off < 0 || off + len > used) {
            // Slice (off, len) isn't fully contained by
            // the buffer.
            fail = true;
            return;
        }

        memmove(data + off, src, len);        
    }

    void write(const char *src, int len=-1)
    {
        // Only perform the write if no allocations failed previously
        if (fail) return;

        if (len < 0) len = strlen(src);

        // Check for overflows
        if (used > MAX_VALUE(used) - len) {
            fail = true;
            return;
        }

        if (!ensure_unused_space(len))
            return;
        
        memcpy(data + used, src, len);
        used += len;
    }

    int read(char *dst, int max)
    {
        if (fail) return 0;

        crlfcrlf = -1; // Invalidate cache

        int copy = std::min(used, max);
        memcpy(dst, data, copy);
        memmove(data, data + copy, used - copy);
        used -= copy;
        return copy;
    }

    // Moves byte from the socket to the buffer and
    // returns true iff the peer closed the connection
    bool write(Socket& sock)
    {
        if (fail) return false;

        bool closed = false;
        while (1) {

            // Make sure the buffer has at least a certain amount of free memory
            // to avoid small copies
            constexpr int min_read = 256;
            if (!ensure_unused_space(min_read))
                break;
            int res = sock.read(data + used, size - used);
            if (res == Socket::WOULD_BLOCK)
                break;
            if (res == Socket::OTHER_ERROR) {
                fail = true;
                return false;
            }
            if (res == 0) {
                closed = true;
                break;
            }
            
            assert(res > 0);

            // Check for overflow
            if (used > MAX_VALUE(used) - res) {
                fail = true;
                return false;
            }

            used += res;
        }

        return closed;
    }

    // Moves byte from the buffer to the socket
    int read(Socket& sock)
    {
        if (fail) return 0;

        int copied = 0;
        while (copied < used) {

            // Make sure the buffer has at least a certain amount
            // of free memory to avoid small copies
            constexpr int min_read = 256;
            if (!ensure_unused_space(min_read))
                break;

            int res = sock.write(data + copied, used - copied);
            if (res == Socket::WOULD_BLOCK)
                break;
            if (res == Socket::OTHER_ERROR || res == 0) {
                fail = true;
                return 0;
            }

            assert(res > 0);
            copied += res;
        }

        crlfcrlf = -1; // Invalidate cache

        memmove(data + copied, data, used - copied);
        used -= copied;
        return copied;
    }

    // Find the index of the first occurrence of "needle"
    // in the buffer's contents. Return -1 if it wasn't
    // found.
    int seek(const char *needle)
    {
        int len = strlen(needle);

        bool is_crlfcrlf = false;
        if (len == 4 && !strcmp(needle, "\r\n\r\n")) {
            if (crlfcrlf > -1) return crlfcrlf;
            is_crlfcrlf = true;
        }

        // If the token is contained by the buffer, its index
        // must be lower than:
        int lim = used - len + 1;

        // If the needle is larger than the buffer, then the
        // limit is negative
        int i = 0;

        while (i < lim && memcmp(data+i, needle, len))
            i++;

        if (i > lim)
            return -1;
        else {
            if (is_crlfcrlf) crlfcrlf = i;
            return i;
        }
    }
    
    // Removed "num" bytes from the head of the buffer
    void consume(int num)
    {
        assert(num <= used);
        memmove(data, data + num, used - num);
        used -= num;

        crlfcrlf = -1; // Invalidate cache
    }

    bool contains(const char *needle)
    {
        return seek(needle) >= 0;
    }

    // Make a slice of the buffer's contents up to
    // a given token. If include_token is true, then
    // the slice will include the token at the end.
    Slice slice_until(const char *token, bool include_token=false)
    {
        int end = seek(token);
        if (end < 0)
            return Slice("", 0);
        else {
            if (include_token) end += strlen(token);
            return Slice(data, end);
        }
    }

    Slice slice(int off, int end)
    {
        if (end < off || off < 0 || end >= used)
            return Slice("", 0);
        
        return Slice(data + off, end - off);
    }
};
