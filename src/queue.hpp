#include <utility>

template <typename T, int N>
class Queue {
    int head;
    int used;
    T items[N];

public:
    Queue()
    {
        head = 0;
        used = 0;
    }

    Queue(Queue&  other) = delete;
    Queue(Queue&& other) = delete;
    Queue& operator=(Queue& other) = delete;
    Queue& operator=(Queue&& other) = delete;

    bool empty() const
    {
        return used == 0;
    }

    int size() const
    {
        return used;
    }

    template <typename U>
    bool push(U&& item)
    {
        if (used == N)
            return false;

        int tail = (head + used) % N;

        items[tail] = std::forward<U>(item);
        used++;

        return true;
    }

    bool pop()
    {
        if (used == 0)
            return false;
        items[head] = T();
        head = (head + 1) % N;
        used--;
        return true;
    }

    bool pop(T& item)
    {
        if (used == 0)
            return false;
        item = std::move(items[head]);
        return pop();
    }

    // Removes the first occurrence of item from the queue. Returns true if an
    // item was found or false if it wasn't.
    bool remove(const T& item)
    {
        int i;
        for (i = 0; i < used; i++) {
            int j = (head + i) % N;
            if (items[j] == item)
                break;
        }
        if (i == used)
            return false;
        for (; i < used-1; i++) {
            int j = (head + i) % N;
            items[j] = std::move(items[(j+1) % N]);
        }
        items[(head + used - 1) % N] = T();
        return true;
    }
};
