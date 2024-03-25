#include <cstdlib>
#include <iostream>

void test_(bool expr, const char *text, const char *file, int line)
{
    if (!expr) {
        std::cout << "Failure in " << file << ":" << line << " [" << text << "]\n";
        abort();
    }
}