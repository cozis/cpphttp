
void test_(bool expr, const char *text, const char *file, int line);
#define test(expr) test_(expr, #expr, __FILE__, __LINE__)