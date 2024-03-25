#include <iostream>
#include "test_utils.hpp"
#include "../src/netutils.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>

extern "C" 
int LLVMFuzzerTestOneInput(const char *data, 
                           size_t size) 
{
    IPv4 ip;
    bool ok = ip.parse(data, size);

    char buf[256];
    if (size < sizeof(buf)) {
        memcpy(buf, data, size);
        buf[size] = '\0';
        struct in_addr buf2;
        switch (inet_pton(AF_INET, buf, &buf2)) {
            
            case 1: 
            test(ok); 
            test(ip.data == buf2.s_addr);
            break;

            case 0:
            case -1:
            test(!ok);
            break;
        }
    }
    return 0;  // Values other than 0 and -1 are reserved for future use.
}