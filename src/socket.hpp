#ifndef SOCKET_HPP
#define SOCKET_HPP

#include <ostream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define POLL WSAPoll
#define EWOULDBLOCK_2 WSAEWOULDBLOCK
#define EAGAIN_2      WSAEWOULDBLOCK
#define EINVAL_2      WSAEINVAL
#define CLOSESOCKET   closesocket
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define POLL poll
#define CLOSESOCKET close
#define EWOULDBLOCK_2 EWOULDBLOCK
#define EAGAIN_2      EAGAIN
#define EINVAL_2      EINVAL
#endif

struct SocketSubsystem {
    SocketSubsystem()
    {
        #ifdef _WIN32
        WSADATA data;
        int res = WSAStartup(MAKEWORD(2,2), &data);
        if (res)
            std::cout << "WSAStartup failed\n";
        #endif
    }
    ~SocketSubsystem()
    {
        #ifdef _WIN32
        WSACleanup();
        #endif
    }
};

struct Socket {
    
private:
    
    static int get_last_error()
    {
        #ifdef _WIN32
        return WSAGetLastError();
        #else
        return errno;
        #endif
    }

    static bool set_blocking(SOCKET fd, bool value)
    {
        #ifdef _WIN32
        unsigned long not_value = !value;
        return ioctlsocket(fd, FIONBIO, &not_value) != SOCKET_ERROR;
        #else
        int flags = fcntl(fd, F_GETFL);
        if (flags == -1)
            return false;
        if (value) 
            flags &= ~O_NONBLOCK;
        else
            flags |= O_NONBLOCK;
        return fcntl(fd, F_SETFL, flags) != -1;
        #endif
    }

public:

    SOCKET fd_;

    enum {
        WOULD_BLOCK = -1,
        OTHER_ERROR = -2,
    };

    Socket(SOCKET fd=INVALID_SOCKET)
    {
        fd_ = fd;
    }

    Socket(Socket&) = delete;
    Socket& operator=(Socket&) = delete;

    Socket(Socket&& other)
    {
        if (this != &other) {
            fd_ = other.fd_;
            other.fd_ = INVALID_SOCKET;
        }
    }

    Socket& operator=(Socket&& other)
    {
        if (this != &other) {
            if (fd_ != INVALID_SOCKET) CLOSESOCKET(fd_);
            fd_ = other.fd_;
            other.fd_ = INVALID_SOCKET;
        }
        return *this;
    }

    ~Socket()
    {
        if (fd_ != INVALID_SOCKET)
            CLOSESOCKET(fd_);
    }

    bool active() const
    {
        return fd_ != INVALID_SOCKET;
    }

    bool accept(Socket& dst)
    {
        if (!active()) return false;

        int accepted = ::accept(fd_, nullptr, nullptr);
        if (accepted < 0)
            return false;

        if (!set_blocking(accepted, false)) {
            CLOSESOCKET(accepted);
            return false;
        }
        
        dst = accepted;
        return true;
    }

    int read(char *dst, int max)
    {
        if (!active()) return -1;

        int res = recv(fd_, dst, max, 0);
        if (res < 0) {
            int code = get_last_error();
            if (code == EWOULDBLOCK_2 || code == EAGAIN_2)
                return WOULD_BLOCK;
            else
                return OTHER_ERROR;
        }
        assert(res >= 0);
        return res;
    }

    int write(char *src, int num)
    {
        if (!active()) return -1;
        int res = send(fd_, src, num, 0);
        if (res < 0) {
            int code = get_last_error();
            if (code == EWOULDBLOCK_2 || code == EAGAIN_2)
                return WOULD_BLOCK;
            else
                return OTHER_ERROR;
        }
        assert(res >= 0);
        return res;
    }
    
    bool start_server(int port, const char *addr)
    {
        if (active()) return false;

        SOCKET fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd == INVALID_SOCKET) {
            std::clog << "Couldn't create socket (did you initialize the socket system?)\n";
            return false;
        }

        if (!set_blocking(fd, false)) {
            CLOSESOCKET(fd);
            std::clog << "Couldn't set socket as non-blocking\n";
            return false;
        }

        struct in_addr addr_buf;
        if (addr == nullptr)
            addr_buf.s_addr = INADDR_ANY;
        else {
            int res = inet_pton(AF_INET, addr, &addr_buf);
            if (res == 0 || res == -1) {
                if (res == 0) {
                    // Invalid address string
                    std::cout << "Invalid address string\n";
                } else {
                    // Unknown error
                    std::cout << "Unknown error\n";
                }
                CLOSESOCKET(fd);
                return false;
            }
            assert(res == 1);
        }

        // Probably should only use this in debug
        int v = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*) &v, sizeof(int));

        struct sockaddr_in full_addr_buf;
        full_addr_buf.sin_family = AF_INET;
        full_addr_buf.sin_port   = htons(port);
        full_addr_buf.sin_addr   = addr_buf;
        if (bind(fd, (struct sockaddr*) &full_addr_buf, sizeof(full_addr_buf))) {
            int code = get_last_error();
            std::cout << "Couldn't bind to the specified address (code " << code << ")\n";
            CLOSESOCKET(fd);
            return false;
        }

        int backlog = 32;
        if (listen(fd, backlog)) {
            std::cout << "Couldn't start listening on " << addr << ":" << port << "\n";
            CLOSESOCKET(fd);
            return false;
        }

        fd_ = fd;
        return true;
    }

};

struct Event {
    enum Type {
        FAILURE = 0,
        RECV    = 1 << 0,
        SEND    = 1 << 1,
    };
    Type   type;
    void  *data;

    Event()
    {
        type = FAILURE;
        data = nullptr;
    }

    Event(Type t, void* p=nullptr)
    {
        type = t;
        data = p;
    }

    friend std::ostream& operator<<(std::ostream& os, Event::Type const& type);
    friend std::ostream& operator<<(std::ostream& os, Event const& event);
};

template <int N>
class EventLoop {
    void         *ptrs[N];
    struct pollfd bufs[N];
    int count;
    int cursor;

    int find_socket_index(const Socket& sock)
    {
        for (int i = 0; i < count; i++)
            if (bufs[i].fd == sock.fd_)
                return i;
        return -1;
    }

    static int convert_event_flags(int in)
    {
        int out = 0;
        if (in & Event::RECV) out |= POLLIN; // Could OR POLLPRI but it's not supported by windows
        if (in & Event::SEND) out |= POLLOUT; 
        return out;
    }

public:

    EventLoop()
    {
        count = 0;
        cursor = 0;
    }

    ~EventLoop()
    {
    }
    
    bool add(const Socket& sock, int events, void *ptr=nullptr)
    {
        if (count == N)
            return false;

        bufs[count].fd = sock.fd_;
        bufs[count].events = convert_event_flags(events);
        bufs[count].revents = 0;
        ptrs[count] = ptr;

        count++;
        return true;
    }

    void add_events(const Socket& sock, int events)
    {
        int i = find_socket_index(sock);
        if (i < 0) return; // Not found

        bufs[i].events |= convert_event_flags(events);
    }

    void remove_events(const Socket& sock, int events)
    {
        int i = find_socket_index(sock);
        if (i < 0) return; // Not found

        bufs[i].events &= ~convert_event_flags(events);
    }
    
    bool remove(const Socket& sock)
    {
        int i = find_socket_index(sock);
        if (i < 0) return false; // Not found

        bufs[i] = bufs[count-1];
        ptrs[i] = ptrs[count-1];
        count--;

        if (cursor > i) cursor--;

        // TODO: Remove all buffered events that refer
        //       to this socket.

        return true;
    }

    // Move the cursor forward until a struct
    // with some reported events is found. If
    // no such structs exists, then "cursor"
    // reaches "count".
    void skip()
    {
        while (cursor < count && bufs[cursor].revents == 0)
            cursor++;
    }

    Event wait()
    {
        skip();
        
        // If no more buffers have events, poll for more events
        while (cursor == count) {

            int n = POLL(bufs, count, -1);
            if (n < 0)
                return Event(Event::FAILURE);

            cursor = 0;
            skip();
        }
        assert(cursor < count);

        // At this point we know the cursor refers a struct
        // with at least one reported event.

        void* ptr = ptrs[cursor];
        auto& revents = bufs[cursor].revents;
        assert(revents != 0);

        // Report to the caller only one of those events 
        // at the time. If report RECV events. Once those
        // are reported, at the next iteration SEND events
        // will be reported.

        // We assume POLLPRI isn't reported (Windows doesn't
        // support it).
        assert((revents & POLLPRI) == 0);

        if (revents & POLLIN) {
            revents &= ~POLLIN;
            return Event(Event::RECV, ptr);
        }

        if (revents & POLLOUT) {
            revents &= ~POLLOUT;
            return Event(Event::SEND, ptr);
        }

        // Report other events as errors
        revents = 0;
        return Event(Event::FAILURE, ptr);
    }
};

#endif