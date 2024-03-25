#include <cstring>
#include <cstdint>
#include <cassert>
#include <ostream>

struct PrintArg {

    enum Type {
        INT,
        FLOAT,
        STRING,
        OTHER,
    };

    Type  type;
    union {
        int64_t     as_int;
        double      as_float;
        const char* as_string;
        void*       other;
    } data;

    #define IS_INT(T)   typename std::enable_if< std::is_integral<T>::value>::type
    #define ISNT_INT(T) typename std::enable_if<!std::is_integral<T>::value>::type

    // Only used when type=OTHER
    void (*print_ptr)(void* data, std::ostream& dst);

    template <typename T>
    PrintArg(IS_INT(T) value)
    {
        type = INT;
        data.as_int = value;
    }

    PrintArg(double value)
    {
        type = FLOAT;
        data.as_float = value;
    }

    PrintArg(const char* value)
    {
        type = STRING;
        data.as_string = value;
    }

    template <typename T>
    PrintArg(ISNT_INT(T)&& value)
    {
        data.other = &value;
        print_ptr = [](void* data, std::ostream& dst) { dst << (*(T*) data); };
    }

    friend std::ostream& operator<<(std::ostream& dst, PrintArg& arg)
    {
        switch (arg.type) {
            case INT   : dst << arg.data.as_int;    break;
            case FLOAT : dst << arg.data.as_float;  break;
            case STRING: dst << arg.data.as_string; break;
            case OTHER : arg.print_ptr(arg.data.other, dst); break;
        }
        return dst;
    }
};

void vprint(std::ostream& dst, const char* fmt, PrintArg* pargs, int num_args);

template <typename ...Ts>
void print(std::ostream& dst, const char *fmt, Ts&& ...args)
{
    PrintArg pargs[] = {args...};
    vprint(dst, fmt, pargs, sizeof...(args));
}
