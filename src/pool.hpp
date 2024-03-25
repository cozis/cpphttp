#include <cstdint>

template <typename T, int N>
class Pool {

    // Structure representing the bytes that can
    // hold a "T". This lets us speak about T's
    // memory without calling the constructor.
    struct Slot {
        alignas(T) char pad[sizeof(T)];
    };

    static constexpr int ceil(int U, int V)
    {
        return (U + V - 1) / V;
    }

    static const int NUM_BITSETS = ceil(N, 64);

    int num_allocated;

    // Memory available for allocation
    Slot slots[N];

    // Packed array of booleans. Each bit describes
    // if its associated slot was allocated or not
    uint64_t used[NUM_BITSETS];

    // Returns the index from the right of the
    // first set bit or -1 otherwise.
    static int find_first_set_bit(uint64_t bits)
    {
        // First check that at least one bit is set
        if (bits == 0) return -1;
        
        uint64_t bits_no_rightmost = bits & (bits - 1);
        uint64_t bits_only_rightmost = bits - bits_no_rightmost;

        int index = 0;
        uint64_t temp;

        // The index of the rightmost bit is the log2
        temp = bits_only_rightmost >> 32;
        if (temp) {
            // Bit is in the upper 32 bits
            index += 32;
            bits_only_rightmost = temp;
        }

        temp = bits_only_rightmost >> 16;
        if (temp) {
            index += 16;
            bits_only_rightmost = temp;
        }

        temp = bits_only_rightmost >> 8;
        if (temp) {
            index += 8;
            bits_only_rightmost = temp;
        }

        static const uint8_t table[] = {
            0, 0, 1, 0, 2, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0,
            4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        };

        index += table[bits_only_rightmost];
        
        return index;
    }

    void set_bit(int index, bool val)
    {
        assert(index >= 0 && index < N);
        
        uint64_t mask = 1ULL << (index & 63);
        if (val)
            used[index >> 6] |= mask;
        else
            used[index >> 6] &= ~mask;
    }

    bool get_bit(int index) const
    {
        uint64_t set = used[index >> 6];
        uint64_t mask = 1ULL << (index & 63);
        return (set & mask) == mask;
    }

    int get_index(T* addr) const
    {
        return (Slot*) addr - slots;
    }

    T* find_not_allocated()
    {
        if (num_allocated == N)
            return nullptr;
        
        // Find the index of a bitset with a zero bit
        int i = 0;
        while (used[i] == ~0ULL)
            i++;
        
        // Now find the zero bit
        int j = find_first_set_bit(~used[i]);
        assert(j > -1);

        T* addr = (T*) &slots[i * 64 + j];
        assert(!allocated(addr));

        return addr;
    }

    void mark_allocated(T* addr)
    {
        num_allocated++;
        set_bit(get_index(addr), 1);
    }

    void mark_not_allocated(T* addr)
    {
        num_allocated--;
        set_bit(get_index(addr), 0);
    }

public:
    Pool()
    {
        num_allocated = 0;
        for (int i = 0; i < NUM_BITSETS; i++)
            used[i] = 0;
    }

    ~Pool()
    {
        // Free allocated objects
        for (int i = 0; i < N; i++) {
            T* addr = (T*) &slots[i];
            if (allocated(addr))
                deallocate(addr);
        }
    }

    Pool(Pool&  other) = delete;
    Pool(Pool&& other) = delete;
    Pool& operator=(Pool& other) = delete;
    Pool& operator=(Pool&& other) = delete;

    T* allocate()
    {
        T* addr = find_not_allocated();
        if (addr == nullptr) return nullptr;
        
        new (addr) T();

        mark_allocated(addr);
        return addr;
    }

    bool owned(T* addr) const
    {
        return (intptr_t) addr >= (intptr_t) slots
            && (intptr_t) addr <  (intptr_t) (slots + N);
    }

    bool allocated(T* addr)
    {
        return owned(addr) && get_bit(get_index(addr));
    }

    int currently_allocated_count() const
    {
        return num_allocated;
    }

    bool have_free_space() const
    {
        return num_allocated < N;
    }

    void deallocate(T* addr)
    {
        if (!owned(addr) || !allocated(addr))
            return;
        
        // Destroy the object
        addr->~T();

        mark_not_allocated(addr);
    }
};