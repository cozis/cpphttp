#include <iostream>
#include "test_utils.hpp"
#include "../src/netutils.hpp"

int main()
{
    IPv4 ip;
    test(ip.parse("") == false);
    test(ip.parse("@") == false);
    test(ip.parse("1") == false);
    test(ip.parse("500") == false);
    test(ip.parse("45.") == false);
    test(ip.parse("45.54.56.98") == true);
    std::cout << "Passed\n";
    return 0;
}