#include <cassert>
#include <limits>
#include <type_traits>
#include "parse.hpp"

#define MAX_VALUE(X) std::numeric_limits<decltype(X)>::max()

struct Scanner {

	const char *str;
	intptr_t len;
	intptr_t off;

	Scanner(const char *s, int l)
	{
		str = s;
		len = l;
		off = 0;
	}

	bool end() const
	{
		return off == len;
	}

	char curr() const
	{
		assert(off < len);
		return str[off];
	}

	void back()
	{
		assert(off > 0);
		off--;
	}

	char peek()
	{
		assert(off+1 < len);
		return str[off+1];
	}

	void consume()
	{
		assert(off < len);
		off++;
	}

	bool consume(char c)
	{
		if (end() || curr() != c)
			return false;
		consume();
		return true;
	}

	bool consume(const char *s)
	{
		intptr_t l = strlen(s);
		if (off + l > len)
			return false;
		if (strncmp(str + off, s, l))
			return false;
		off += l;
		return true;
	}

	typedef bool (*CharTestFunc)(char c);

	/*
	 * Consumes a substring that starts with a character
	 * c such that testfn_head(c) is true and following
	 * chars are such that testfn_body(c) is true.
	 *
	 * Returns true iff at least a character was consumed.
	 */
	bool consume(CharTestFunc testfn_head, CharTestFunc testfn_body)
	{
		if (end() || !testfn_head(curr()))
			return false;

		do
			consume();
		while (!end() && testfn_body(curr()));
		return true;
	}

	/*
	 * It's like the previous function but uses the same
	 * function for head of the substring and body.
	 */
	bool consume(CharTestFunc testfn)
	{
		return consume(testfn, testfn);
	}
};

bool is_upper_alpha(char c)
{
	return c >= 'A' && c <= 'Z';
}

bool is_alpha(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool is_digit(char c)
{
	return (c >= '0' && c <= '9');
}

/*
 * Parse the scheme token of the following URL in "src" if present
 */
void parse_scheme(Scanner &src, Slice &dst)
{
	intptr_t start = src.off;

	auto schema_testfn_head = [](auto c) { return is_upper_alpha(c); };
	auto schema_testfn_body = [](auto c) { return is_upper_alpha(c) || is_digit(c) || c == '+' || c == '-' || c == '.'; };

	if (!src.consume(schema_testfn_head, schema_testfn_body))
		dst.whipe();
	else {
		// May have a schema if ':' follows
		if (!src.consume(':')) {
			// Not a schema. Empty schema substring and
			// restore the scanner cursor to the start
			dst.whipe();
			src.off = start;
		} else {
			dst.str = src.str + src.off;
			dst.off = start;
			dst.len = src.off - start;
		}
	}
}

/*
 * From RFC 3986, Appendix A:
 *
 *     sub-delims = "!" / "$" / "&" / "'" / "(" / ")"
 *                / "*" / "+" / "," / ";" / "="
 */
bool is_subdelim(char c)
{
	return c == '!' || c == '$' || c == '&' || c == '\'' || c == '('
		|| c == ')' || c == '*' || c == '+' || c == ',' || c == ';'
		|| c == '=';
}

/*
 * From RFC 3986, Section 2.3:
 *     Characters that are allowed in a URI but do not have a reserved
 *     purpose are called unreserved.  These include uppercase and lowercase
 *     letters, decimal digits, hyphen, period, underscore, and tilde.
 *
 *         unreserved  = ALPHA / DIGIT / "-" / "." / "_" / "~"
 */
bool is_unreserved(char c)
{
	return is_alpha(c) || is_digit(c) || c == '-' || c == '.' || c == '_' || c == '~';
}

/*
 * From RFC 3986, Appendix A:
 *     pchar = unreserved / pct-encoded / sub-delims / ":" / "@"
 */
bool is_pchar(char c)
{
	return is_unreserved(c) || is_subdelim(c) || c == ':' || c == '@';
}

void parse_user_info(Scanner &src, Slice &dst)
{
	intptr_t start = src.off;
	if (src.consume([](char c) { return is_unreserved(c) || is_subdelim(c) || c == ':'; })) {
		dst.str = src.str;
		dst.off = start;
		dst.len = src.len - start;
		if (!src.consume("@")) {
			// The scanned string wasn't an userinfo string
			// after all. Clear the substring and put the
			// scanner's cursor back to the start.
			dst.whipe();
			src.off = start;
		}
	} else
		dst.whipe();
}

bool is_hex(char c)
{
	return (c >= '0' && c <= '9')
		|| (c >= 'a' && c <= 'f')
		|| (c >= 'A' && c <= 'F');
}

bool parse_u16_base16(Scanner &src, uint16_t &dst)
{
	if (src.end() || !is_hex(src.curr()))
		return false; // Missing hex number

	dst = 0;
	do {

		// Hex digit to int
		int d = src.curr() - '0';
		assert(d >= 0 && d < 16);

		// Only consume hex digits up to 2^16-1
		if (dst > (UINT16_MAX - d) / 16) {
			src.back();
			break;
		}
		dst = dst * 16 + d;

		src.consume();

	} while (!src.end() && is_hex(src.curr()));
	return true;
}

bool parse_ipv6(Scanner &src, IPv6 &dst)
{
	/*
	 * An IPv6 address is a sequence of 8 byte pairs expressed in
	 * hex and separated by ':' tokens.
	 *
	 * Each hex number represents a number from 0 to 2^16-1 which
	 * means it has a maximum of 4 hex digits.
	 *
	 * At any point between two hex numbers the "::" token may be
	 * used in place of ":". If that's the case the IPv6 will have
	 * less than 8 groups. The remaining hex numbers are assumed to
	 * be 0 and inserted where "::" is. It's also possible to have
	 * "::" at the beginning or end of the address.
	 */

	// Current number of numbers appended to
	// the IPv6 data. By the end this must be 8.
	int count = 0;

	while (count < 8 && !src.consume("::")) {

		// If this isn't the first number, consume the preceding ':'.
		if (count > 0 && !src.consume(":"))
			return false;

		if (!parse_u16_base16(src, dst.data[count]))
			return false;
		count++;
	}

	if (count < 8) {
		// The "::" was used.

		// Parse the remaining ones in a buffer, then
		// calculate the implicit zero numbers necessary
		// to get to 8. Then, append the zeros and tail
		// to the final array.
		uint16_t tail[8];
		int tail_count = 0;

		while (count + tail_count < 7) {

			// If this isn't the first number, consume the preceding ':'.
			if (tail_count > 0 && !src.consume(":"))
				return false;

			if (!parse_u16_base16(src, tail[tail_count]))
				return false;
			tail_count++;
		}

		assert(count + tail_count < 8);

		int implicit_pairs = 8 - (count + tail_count);

		for (int i = 0; i < implicit_pairs; i++)
			dst.data[count++] = 0;

		// Now append the tail
		for (int i = 0; i < tail_count; i++)
			dst.data[count++] = tail[i];
	}

	assert(count == 8);
	return true;
}

bool parse_ipv6(Scanner &src, Host &dst)
{
	intptr_t start = src.off;
	if (parse_ipv6(src, dst.ipv6)) {
		dst.type = Host::IPV6;
		dst.text.str = src.str;
		dst.text.off = start;
		dst.text.len = src.off - start;
		return true;
	}
	return false;
}

bool parse_u8_base10(Scanner &src, uint8_t &dst)
{
	if (src.end() || !is_digit(src.curr()))
		return false;

	dst = 0;
	do {

		int d = src.curr() - '0';
		assert(d >= 0 && d < 10);

		if (dst > (UINT8_MAX - d) / 10) {
			src.back();
			break;
		}

		dst = dst * 10 + d;

		src.consume();

	} while (!src.end() && is_digit(src.curr()));

	return true;
}

bool parse_u16_base10(Scanner &src, uint16_t &dst)
{
	if (src.end() || !is_digit(src.curr()))
		return false;

	dst = 0;
	do {

		int d = src.curr() - '0';
		assert(d >= 0 && d < 10);

		if (dst > (UINT16_MAX - d) / 10) {
			src.back();
			break;
		}

		dst = dst * 10 + d;

		src.consume();

	} while (!src.end() && is_digit(src.curr()));

	return true;
}

bool parse_ipv4(Scanner &src, IPv4 &dst)
{
	uint32_t word = 0;
	for (int i = 0; i < 4; i++) {

		if (i > 0 && !src.consume('.'))
			return false;

		uint8_t byte;
		if (!parse_u8_base10(src, byte))
			return false;
		word = (word << 8) | byte;
	}

	dst = word;
	return true;
}

bool parse_ipv4(Scanner &src, Host &dst)
{
	intptr_t start = src.off;
	if (parse_ipv4(src, dst.ipv4)) {
		dst.type = Host::IPV4;
		dst.text.str = src.str;
		dst.text.off = start;
		dst.text.len = src.off - start;
		return true;
	}
	return false;
}

bool parse_host(Scanner &src, Host &dst)
{
	if (src.end())
		return false;

	if (src.curr() == '[') {

		// IPv6 or IPvFuture
		if (!parse_ipv6(src, dst))
			return false;

		if (!src.consume(']'))
			return false;

	} else {

		bool ipv4;

		if (is_digit(src.curr()))
			// May be a registered name or IPv4
			ipv4 = parse_ipv4(src, dst);
		else
			ipv4 = false;

		if (!ipv4) {

			// It's a registered name.
			//
			// From RFC 3986, Appendix A:
			//
			//     reg-name = *( unreserved / pct-encoded / sub-delims )
			//
			// It's worth noting that the registered name may be empty.

			intptr_t start = src.off;
			src.consume([](char c) { return is_unreserved(c) || is_subdelim(c); });
			dst.type = Host::NAME;
			dst.text.str = src.str;
			dst.text.off = start;
			dst.text.len = src.off - start;
		}
	}

	return true;
}

bool parse_authority(Scanner &src, Authority &dst)
{
	parse_user_info(src, dst.userinfo);

	if (!parse_host(src, dst.host))
		return false;

	if (src.consume(':')) {

		// There may be a port
		if (src.end() || !is_digit(src.curr()))
			dst.port = -1; // No port
		else {
			uint16_t buffer;
			if (!parse_u16_base10(src, buffer))
				return false;
			dst.port = buffer;
		}
	} else
		dst.port = -1; // No port

	return true;
}

/*
 * From RFC 3986, Section 3.4:
 *
 *     query = *( pchar / "/" / "?" )
 * 
 * and Section 3.5:
 * 
 *     fragment = *( pchar / "/" / "?" )
 */
Slice parse_query_or_fragment(Scanner& src)
{
	Slice res;
	res.str = src.str;
	res.off = src.off;
	src.consume([](char c) { return is_pchar(c) || c == '/' || c == '?'; });
	res.len = src.off - res.off;
	return res;
}

Slice parse_path_abempty(Scanner& src)
{
	Slice path;
	path.str = src.str;
	path.off = src.off;
	while (src.consume('/'))
		src.consume(is_pchar);
	path.len = src.off - path.off;
	return path;
}

Slice parse_path(Scanner& src)
{
	Slice path;
	path.str = src.str;
	path.off = src.off;
	src.consume([](char c) { return is_pchar(c) || c == '/'; });
	path.len = src.off - path.off;
	return path;
}

/*
 * See RFC 3986
 */
bool parse_url(Scanner &src, URL &dst)
{
	Slice full;
	full.str = src.str;
	full.off = src.off;

	parse_scheme(src, dst.scheme);

	/*
	 * From RFC 3986, Section 3.2:
	 *   The authority component is preceded by a double slash ("//") and is
     *   terminated by the next slash ("/"), question mark ("?"), or number
     *   sign ("#") character, or by the end of the URI.
	 */
	if (src.consume("//")) {
		if (!parse_authority(src, dst.authority))
			return false;
		if (src.peek() == '/')
			dst.path = parse_path_abempty(src);
	} else
		dst.path = parse_path(src);

	if (src.consume('?')) dst.query    = parse_query_or_fragment(src);
	if (src.consume('#')) dst.fragment = parse_query_or_fragment(src);

	full.len = src.off - full.off;
	dst.full = full;
	return true;
}

bool parse_method(Scanner& src, Method& dst, ParseError& error)
{
	intptr_t start = src.off;
	if (!src.consume(is_upper_alpha)) {
		error.write("Missing method");
		return false;
	}

	Slice text;
	text.str = src.str;
	text.off = start;
	text.len = src.off - start;

	if (text == "GET")
		dst = GET;
	else if (text == "POST")
		dst = POST;
	else {
		error.write("Method not supported");
		return false;
	}

	return true;
}

bool parse_request(Scanner &src, Request &dst, ParseError& error)
{
	if (!parse_method(src, dst.method, error))
		return false;

	// Skip one space
	if (!src.consume(' ')) {
		error.write("Missing space after method");
		return false;
	}

	URL url;
	if (!parse_url(src, url)) {
		error.write("Invalid URL");
		return false;
	}
	dst.url = url;

	if (!src.consume(" HTTP/1\r\n") && 
		!src.consume(" HTTP/1.0\r\n") && 
		!src.consume(" HTTP/1.1\r\n")) {
		error.write("Invalid HTTP version token\n");
		return false;
	}
	
	// Parse headers
	if (!src.consume("\r\n")) {

		do {
			Slice name;
			Slice value;

			name.str = src.str;
			name.off = src.off;
			src.consume([](char c) { return c != ':'; });
			name.len = src.off - name.off;

			if (!src.consume(':')) {
				error.write("Missing ':' after header name");
				return false;
			}

			value.str = src.str;
			value.off = src.off;
			src.consume([](char c) { return c != '\r'; });
			value.len = src.off - value.off;

			// Add the parsed header to the request structure.
			// If the header limit was reached, report that
			// the header was dropped.
			if (dst.count < MAX_REQUEST_HEADERS)
				dst.headers[dst.count++] = (Header) {name, value};
			else
				dst.ignored_count++;
			
			if (!src.consume("\r\n")) {
				error.write("Missing CRLF after header body");
				return false;
			}

		} while (!src.consume("\r\n")); // Parse headers until you find and empty line.
	}

	// Now the cursor points to after the final CRLF. Lets make sure
	// that nothing comes after that.
	if (src.off != src.len) {
		error.write("Bad characters after empty line");
		return false;
	}
	
	// All done.
	return true;
}

bool Request::parse(const char *str, int len)
{
	ParseError error;
	Scanner scanner(str, len);
	valid = parse_request(scanner, *this, error);
	return valid;
}

bool Request::parse(const char *str, int len, ParseError& error)
{
	Scanner scanner(str, len);
	valid = parse_request(scanner, *this, error);
	return valid;
}

bool Request::parse(Slice src)
{
	return parse(src.str, src.len);
}

bool Request::parse(Slice src, ParseError& error)
{
	return parse(src.str, src.len, error);
}

bool IPv4::parse(const char *str, int len)
{
	if (len < 0) len = strlen(str);
	Scanner scanner(str, len);
	return parse_ipv4(scanner, *this);
}

bool IPv6::parse(const char *str, int len)
{
	if (len < 0) len = strlen(str);
	Scanner scanner(str, len);
	return parse_ipv6(scanner, *this);
}

bool is_space(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

int Request::content_length() const
{
	if (!valid)
		return 0;
	
	int i; // Header index
	for (i = 0; i < count; i++)
		if (headers[i].name == "Content-Length")
			break;
	if (i == count)
		return 0; // Content-Length not found, assume 0

	Slice value = headers[i].value;
	int j = 0; // Header value cursor

	// Consume optional spaces
	while (j < value.len && is_space(value[j]))
		j++;
	
	// After the spaces is expected a number
	if (j == value.len || !is_digit(value[j]))
		return 0; // No number, assume 0 length

	// Found a digit. Parse the entire number until
	// no more digits are found
	int length = 0;
	do {
		char c = value[j];
		assert(is_digit(c));
		int digit = c - '0';
		if (length > (MAX_VALUE(length) - digit) / 10) {
			// Overflow. The reported length is too big.
			return -1;
		}
		length = length * 10 + digit;
		j++;
	} while (j < value.len && is_digit(value[j]));

	return length;
}