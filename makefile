ifeq ($(OS),Windows_NT)
	# Windows
	EXT = .exe
    LFLAGS = -lws2_32
else
    # Linux
	EXT =
	LFLAGS = 
endif

all: http$(EXT) test_queue$(EXT) test_parse_ipv4$(EXT) # fuzz_parse_ipv4$(EXT) fuzz_parse_ipv6$(EXT)

http$(EXT): src/main.cpp src/parse.cpp src/socket.cpp
	g++ $^ -o $@ -Wall -Wextra -ggdb $(LFLAGS)

test_queue$(EXT):
	g++ test/test_queue.cpp test/test_utils.cpp -o $@ -Wall -Wextra -ggdb

test_parse_ipv4$(EXT):
	g++ test/test_parse_ipv4.cpp test/test_utils.cpp src/parse.cpp -o $@ -Wall -Wextra -ggdb

fuzz_parse_ipv4$(EXT):
	clang++ test/fuzz_parse_ipv4.cpp -o $@ -fsanitize=fuzzer

fuzz_parse_ipv6$(EXT):
	clang++ test/fuzz_parse_ipv6.cpp -o $@ -fsanitize=fuzzer
