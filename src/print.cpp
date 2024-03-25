
#include "print.hpp"

void vprint(std::ostream& dst, const char* fmt, PrintArg* pargs, int num_args)
{
    char sep = '@';

    int curr_arg = 0;
    int len = strlen(fmt);
    int cur = 0;
    while (cur < len) {
        
        // Move the cursor until the next separator
        // or end of format string
        int plain_text_off = cur;
        while (cur < len && fmt[cur] != sep)
            cur++;
        int plain_text_len = cur - plain_text_off;
        
        // Write the plain text string
        dst.write(fmt + plain_text_off, plain_text_len);

        // If the plain text string ended with a
        // separator, print an argument
        if (cur < len) {
            assert(fmt[cur] == sep);

            if (curr_arg == num_args) {
                // The '%' doesn't refer to any arguments
                dst << sep;
            } else {
                dst << pargs[curr_arg++];
            }

            // Consume the '%'
            cur++;
        }        
    }
}