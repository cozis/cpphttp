#include <utility>
#include <cassert>
#include <iostream>
#include "pool.hpp"
#include "queue.hpp"
#include "parse.hpp"
#include "socket.hpp"
#include "buffer.hpp"

/*
 * Structure that represents the connection with a client
 */
struct Client {

    Socket sock;
    Buffer in;
    Buffer out;

    // Number of requests from this client that were
    // handled.
    int num_served;

    // True iff the client's reference is in the
    // server's candidate queue
    bool queued;

    // Tells the server that the connection with this
    // client should be terminated when the output
    // buffer is fully flushed.
    bool close_when_flushed;

    Client()
    {
        queued = false;
        num_served = 0;
        close_when_flushed = false;
    }

    Client(Client&) = delete;
    Client(Client&&) = delete;
    Client& operator=(Client&) = delete;
    Client& operator=(Client&&) = delete;
};

template <int MAX_CLIENTS>
class Server {

public:

    Server()
    {
        state = NOTARGET;
    }

    Server(Server&  other) = delete;
    Server(Server&& other) = delete;
    Server& operator=(Server& other) = delete;
    Server& operator=(Server&& other) = delete;

    /*
     * Start listening for incoming connections on
     * the specified port and on the "addr" interface.
     * 
     * The "addr" argument must be an ipv4 address in
     * dotted decimal notation. If it's NULL, the server
     * will listen on all available interfaces.
     */
    bool listen(int port=8080, const char *addr=nullptr);

    /*
     * Get an HTTP request to handle. If a request was
     * already queued this call won't block, else it
     * will.
     */
    void wait(Request& req);

    /*
     * This function can only be called between two 
     * "wait" calls and it sets the HTTP status code 
     * of the reply to the last request returned by 
     * wait.
     * 
     * You can't call this function more than once
     * per request and it must be done before calling
     * "status", "header", "write" or "send".
     */
    void status(int code);

    /*
     * Similarly to "status", this function sets a
     * response header for the last request returned
     * by "wait".
     * 
     * It may be called more than once. But before
     * "write" and "send".
     */
    void header(const char *name, const char *value);

    /*
     * Similar to "header" but appends bytes to the
     * response's body. It must be called after "send".
     */
    void write(const char *str, int len=-1);

    /*
     * Mark a request as handled. You can no longer
     * modify the response after you call this function.
     */
    void send();

private:

    // Since responses are built using a kind of
    // immediate-mode API ("status", "header", "write"
    // and "send"), the server needs to hold a state 
    // to discriminate between valid and invalid calls
    // for the response creation.
    enum State {

        NOTARGET, // No request is being handled. This is both the 
                  // starting value and that set by "send".
        
        STATUS,   // A request was returned though "wait" but no 
                  // "status" call was done
        
        HEADERS,  // "status" has been called. Now either "header" 
                  // or "write" are allowed.
        
        CONTENT,  // A call to "write" has been done, so only other 
                  // calls to it are allowed.
        
        // After any of these states you can "send" and
        // go to the "NOTARGET" state.
    };

    State state;

    // Listening socket.
    Socket socket_;

    // Pool of client structures
    Pool<Client, MAX_CLIENTS> pool;
    
    // The eventloop must be able to hold one entry per
    // client and one more for the listening socket.
    //
    // It may be necessary to hold some timers for each
    // client.
    EventLoop<MAX_CLIENTS+1> evloop;

    // This queue holds references to clients that are
    // "response candidates". A candidate is a client
    // for which a request head was received, but the
    // body may or may not.
    //
    // When a client is sending to the server a request, 
    // the moment the server receives the "\r\n\r\n" token 
    // it considers the client a "candidate" for being 
    // responded to.
    //
    // An HTTP request has this general structure:
    //
    //     GET /home HTTP/1.1 \r\n
    //     header1: value1 \r\n
    //     header2: value2 \r\n
    //     Content-Length: XXX \r\n
    //     \r\n
    //     ... Content ...
    //
    // So the \r\n\r\n determines the end of the request's 
    // head and start of the body. There is no way of knowing 
    // if the request body was also received without parsing 
    // the entire request and getting the value of the 
    // "Content-Length" header. To avoid parsing the request 
    // twice or having to cache the result, we just mark the 
    // client as "candidate" and push it to this queue. The 
    // "wait" function will pop elements of this queue looking 
    // for one that's actually ready and return that to the
    // user.
    Queue<Client*, MAX_CLIENTS> queue;

    // The following fields are state necessary when responding
    // to a request. They only hold meaning when the state isn't
    // NOTARGET.
    
    Client* target; // Current client that's being responded to
    
    int offset_content_length; // Offset (in bytes) of the "Content-Length" header's value
                               // in the output buffer of the target client. This is set
                               // during the first "write" call after a "wait".
    
    int offset_content; // Offset (in bytes) of the response body in the output buffer
                        // of the target client. It's set at the first "write" call after
                        // "wait".

    int keep_alive; // This is 1 if the user set the "Connection: Keep-Alive" header or
                    // 0 if it set "Connection: Close". Its initial value is -1, so reading
                    // -1 means the user didn't specify anything yet.
    
    int req_bytes; // Size (in bytes) of the request that's being served. This is necessary
                   // when the response is completed and the request bytes can be dropped.

    static const char* status_text(int code);

    // Choose if a given connection can be kept alive.
    // This is a function of:
    //     1. num_clients: The number of curretly connected clients
    //     2. max_clients: The client limit
    //     3. How many responses were previously served to this client
    static bool should_keep_alive(int num_clients, int max_clients, int num_served);

    void remove_client(Client* client);
    void accept_incoming_connections();
    void handle_single_event(Event event);
    void handle_client_data_and_queue_if_candidate(Client* client);
    void flush_buffered_bytes_to_client_and_close_if_done(Client* client);
};

template <int N>
bool Server<N>::listen(int port, const char *addr)
{
    if (socket_.active())
        return false; // Already listening
    
    Socket socket;
    if (!socket.start_server(port, addr))
        return false;

    // We want to know when calling "accept" on the socket
    // will not block. From the point of view of "poll"
    // (the underlying syscall of the event loop) an ACCEPT
    // operation is a read operation.
    if (!evloop.add(socket, Event::RECV, this)) {
        std::clog << "Couldn't add socket to event loop object\n";
        return false;
    }

    std::clog << "Listening on " << (addr ? addr : "0.0.0.0") << ":" << port << "\n";

    // Commit the socket structure
    socket_ = std::move(socket);

    return true;
}

/*
 * See the forward declaration.
 */
template <int N>
void Server<N>::wait(Request& req)
{
    // Make sure any pending response is sent and
    // the state is NOTARGET.
    send();

    assert(state == NOTARGET);

    /*
     * Basically what this loop is doing is handling
     * TCP level I/O until one or more clients become
     * response candidates. When that's true it pops
     * a client, parses the request into the user-provided
     * structure and checks that the request body was
     * fully received. If it wasn't it drops the client
     * and gets or waits for a new candidate. If the body
     * was received, it returns to the user.
     * 
     * Clients that were considered candidates but 
     * couldn't be served yet may receive more bytes
     * in the future. Whenever they receive bytes they
     * will be considered candidates again until they're
     * served.
     */
    do {

        while (queue.empty()) {
            Event event = evloop.wait();
            handle_single_event(event);
        }

        Client* candidate;
        queue.pop(candidate);
        candidate->queued = false;
        assert(pool.allocated(candidate));
    
        // It's known that the input buffer contains
        // a \r\n\r\n or the client wouldn't have been
        // inserted in the queue.
        Slice head = candidate->in.slice_until("\r\n\r\n", true);

        ParseError error;
        if (!req.parse(head, error)) {
            // Invalid request
            // TODO: Send a message to the client before removing it
            std::clog << "Parsing Error: " << error.text << "\n";
            remove_client(candidate);
            continue;
        }
        
        int head_len = head.len;
        int body_len = req.content_length();
        if (body_len < 0) {
            // Malformed Content-Length header
            std::clog << "Malformed Content-Length header\n";
            remove_client(candidate);
            continue;
        }

        int total_len = head_len + body_len;

        // We know the head of the request was received, but
        // if the body wasn't we can't respond yet.
        if (candidate->in.length() >= total_len) {
            // Request was fully received
            req.body = candidate->in.slice(head_len, total_len);
            target = candidate;
            state = STATUS;
            req_bytes = total_len;
            keep_alive = -1;
            break;
        }

        // Still waiting for the request's body.
        // Go back to waiting for a candidate.
    } while (1);
}

template <int N>
bool Server<N>::should_keep_alive(int num_clients, int max_clients, int num_served)
{
    // If the server is about 70% full, don't keep connections alive
    if (10 * num_clients > 7 * max_clients)
        return false;
    
    // Only keep alive if less than 5 responses were served
    if (num_served >= 5)
        return false;
    
    return true;
}

template <int N>
void Server<N>::remove_client(Client* client)
{
    assert(pool.allocated(client));
    evloop.remove(client->sock);
    if (client->queued)
        queue.remove(client);
    pool.deallocate(client);
    assert(!pool.allocated(client));
}

template <int N>
void Server<N>::accept_incoming_connections()
{
    // TODO: Since we're leaving some connections in the queue
    //       when the client limit is reached, we need to make
    //       sure to serve them when some client structs are freed.

    // Accept all incoming connections until the client pool is full
    for (Socket sock; pool.have_free_space() && socket_.accept(sock); ) {

        Client* client = pool.allocate();
        assert(client);

        // At first only register for receive events since
        // there's nothing to be sent.
        if (!evloop.add(sock, Event::RECV, client)) {
            pool.deallocate(client);
            continue;
        }

        // Commit socket
        client->sock = std::move(sock);

        // The newly accepted client may already have some
        // data to be read. Generate a RECV event manually.
        handle_single_event(Event(Event::RECV, client));
    }
}

template <int N>
void Server<N>::handle_client_data_and_queue_if_candidate(Client* client)
{
    // Client sent data. Copy it into the buffer
    bool closed = client->in.write(client->sock);
    if (closed || client->in.failed()) {
        remove_client(client);
        return;
    }

    // If the client isn't already ready to be served,
    // it may be now. Check wether the head of the request
    // was fully received.

    // The head of a request is terminated by a CRLF CRLF 
    // token.
    if (client->in.contains("\r\n\r\n")) {

        // Head was received! Push the client into the queue if
        // it wasn't already.

        if (!client->queued) {
            queue.push(client);
            client->queued = true;
        }

        // It's possible to avoid to check wether the client is
        // contained in the queue by caching the information.
        // This would speed up the process but also add redundancy
        // to the class and possible bugs.
    }
}

template <int N>
void Server<N>::flush_buffered_bytes_to_client_and_close_if_done(Client* client)
{
    // Client is ready to receive data
    client->out.read(client->sock);
    if (client->out.failed()) {
        remove_client(client);
        return;
    }

    if (client->out.length() == 0) {
        
        // Nothing more to send.
        
        if (client->close_when_flushed) {
            remove_client(client);
            return;
        }

        // Tell the eventloop we're not interested
        // in output events for this client
        evloop.remove_events(client->sock, Event::SEND);
    }
}

template <int N>
void Server<N>::handle_single_event(Event event)
{
    if (event.data == nullptr)
        return; // Event isn't relative to a socket
    
    if (event.data == this)
        accept_incoming_connections();
    else {
        Client* client = (Client*) event.data;
        assert(pool.allocated(client));
        switch (event.type) {
            case Event::FAILURE: remove_client(client); break;
            case Event::RECV: handle_client_data_and_queue_if_candidate(client); break;
            case Event::SEND: flush_buffered_bytes_to_client_and_close_if_done(client); break;
        }
    }
}

template <int N>
void Server<N>::status(int code)
{
    if (state == NOTARGET)
        return;
    
    assert(target);

    if (state != STATUS)
        return; // "status" called twice

    char buf[256];
    int len = snprintf(buf, sizeof(buf), "HTTP/1.1 %d %s\r\n", code, status_text(code));
    assert(len > 0);

    // No need to check for errors. We'll do it
    // when "reply" is called.
    target->out.write(buf, len);
    
    state = HEADERS;
}

template <int N>
void Server<N>::header(const char *name, const char *value)
{
    if (state == NOTARGET)
        return;
    
    if (state == STATUS)
        // Header added before a status, so first
        // add a 200 for correctness sake
        status(200);

    if (state == CONTENT)
        // Can't add a header after the start of
        // the response's body.
        return;

    assert(state == HEADERS);
    
    // Make sure that the caller isn't writing
    // a header that must be added automatically
    // by this class.
    // TODO: Make the check case-insensitive.
    if (!strcmp(name, "Content-Length"))
        return;

    if (!strcmp(name, "Connection")) {
        if (!strcmp(value, "Close"))
            keep_alive = 0;
        else
            keep_alive = 1;
        return;
    }
    
    target->out.write(name);
    target->out.write(": ");
    target->out.write(value);
    target->out.write("\r\n");
}

template <int N>
void Server<N>::write(const char *str, int len)
{
    if (len < 0) len = strlen(str);

    if (state == NOTARGET)
        return;
    
    assert(target);
    
    if (state == STATUS)
        status(200);

    // If this is the first time we append to the
    // body of the response, append special headers
    if (state == HEADERS) {

        // This is the start of the response body, so
        // add any special header and the empty line
        // separator.

        if (keep_alive == -1) keep_alive = 1;

        // If the user wants to keep the connection alive
        // (or didn't specify it) then check first if it's
        // reasonable given the server's state.
        int num_clients = pool.currently_allocated_count();
        int max_clients = N;
        int num_served  = target->num_served;
        if (!should_keep_alive(num_clients, max_clients, num_served))
            keep_alive = false;

        switch (keep_alive) {
            case  0: target->out.write("Connection: Close\r\n"); break;
            case  1: target->out.write("Connection: Keep-Alive\r\n"); break;
        }

        // Append the Content-Length header with an empty value.
        // When the response content is known we'll fill the value in.
        target->out.write("Content-Length: ");
        offset_content_length = target->out.length();
        target->out.write("         \r\n"); // This is exactly 9 spaces before the \r\n

        // Write an empty line
        target->out.write("\r\n");

        offset_content = target->out.length();
        state = CONTENT;
    }

    target->out.write(str, len);
}

template <int N>
void Server<N>::send()
{
    if (state == NOTARGET)
        return;

    assert(target);

    // Make sure the previous response parts are written
    write("");

    if (target->out.failed()) {

        // Actually the response construction failed, so drop the client.
        remove_client(target);

    } else {

        // Update the Content-Length header's vale now
        // that we know the content's length.
        int content_length = target->out.length() - offset_content;
        
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%d", content_length);
        assert(len < 10);

        target->out.overwrite(offset_content_length, buf, len);

        // Tell the eventloop that we're interested in output 
        // events for this client.
        evloop.add_events(target->sock, Event::SEND);

        // If the connection isn't marked as reusable, mark it
        // to be closed when the output buffer is flushed and
        // stop listening for input data.
        // NOTE: "keep_alive" can't be -1 at this point because
        //       it was set to either 0 or 1 when writing the
        //       "Connection" header.
        if (keep_alive == 0) {
            target->close_when_flushed = true;
            evloop.remove_events(target->sock, Event::RECV);
        }

        // Now that the request was served, we can remove it
        // from the input buffer.
        target->in.consume(req_bytes);

        // If the connection is keep-alive, pipelining is allowed
        // so check if an other request is pending and if it is,
        // put the client back into the queue.
        if (keep_alive && target->in.contains("\r\n\r\n")) {
            // We know that the client isn't already in the queue
            // because we just popped and served it.
            queue.push(target);
            target->queued = true;
        }

        target->num_served++;
    }

    state = NOTARGET;
    target = nullptr;
    keep_alive = -1;
    req_bytes = -1;
}

template <int N>
const char* Server<N>::status_text(int code)
{
    switch(code) {

        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 102: return "Processing";

        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 203: return "Non-Authoritative Information";
        case 204: return "No Content";
        case 205: return "Reset Content";
        case 206: return "Partial Content";
        case 207: return "Multi-Status";
        case 208: return "Already Reported";

        case 300: return "Multiple Choices";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 305: return "Use Proxy";
        case 306: return "Switch Proxy";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";

        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 402: return "Payment Required";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 406: return "Not Acceptable";
        case 407: return "Proxy Authentication Required";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 411: return "Length Required";
        case 412: return "Precondition Failed";
        case 413: return "Request Entity Too Large";
        case 414: return "Request-URI Too Long";
        case 415: return "Unsupported Media Type";
        case 416: return "Requested Range Not Satisfiable";
        case 417: return "Expectation Failed";
        case 418: return "I'm a teapot";
        case 420: return "Enhance your calm";
        case 422: return "Unprocessable Entity";
        case 426: return "Upgrade Required";
        case 429: return "Too many requests";
        case 431: return "Request Header Fields Too Large";
        case 449: return "Retry With";
        case 451: return "Unavailable For Legal Reasons";

        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        case 509: return "Bandwidth Limit Exceeded";
    }
    return "???";
}
