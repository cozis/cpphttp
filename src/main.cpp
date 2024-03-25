#include "server.hpp"

int main()
{
    SocketSubsystem ss;

    Server<16384> server;

    int port = 8080;
    if (!server.listen(port)) {
        std::clog << "Couldn't start tcp server\n";
        return -1;
    }

    for (;;) {
        Request req;
        server.wait(req);
        server.status(200);
        server.header("Content-Type", "text/plain");
        server.write("Hello, world!");
        server.send();
    }

    return 0;
}
