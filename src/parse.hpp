#include "slice.hpp"
#include "netutils.hpp"

struct Host {

	enum Type { NAME, IPV4, IPV6 };

	Type  type;
	Slice text; // any type
	IPv4  ipv4; // when type=IPV4
	IPv6  ipv6; // when type=IPV6;

	Host()
	{
		type = IPV4;
	}
};

struct Authority {
	Slice userinfo;
	Host  host;
	int   port; // -1 means no port

	Authority()
	{
		port = -1;
	}
};

struct URL {
	Slice     full;
	Slice     scheme;
	Authority authority;
	Slice     path;
	Slice     query;
	Slice     fragment;
};

struct Header {
	Slice name;
	Slice value;
};

enum Method {
	GET, POST,
};

#include <cstdio>
#include <cstdarg>

struct ParseError {
	char text[256];
	void write(const char *fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		vsnprintf(text, sizeof(text), fmt, args);
		va_end(args);
	}
};

constexpr int MAX_REQUEST_HEADERS = 32;

struct Request {

	bool valid;

	Method method;
	URL  url;

	Header headers[MAX_REQUEST_HEADERS];
	int count, ignored_count;

	Slice body;

	Request()
	{
		valid = false;
		method = GET;
		count = 0;
		ignored_count = 0;
	}

	bool parse(const char *str, int len);
	bool parse(const char *str, int len, ParseError& error);
	bool parse(Slice slice);
	bool parse(Slice slice, ParseError& error);
	int content_length() const;
};
