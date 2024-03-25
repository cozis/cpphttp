#include <cstdint>

struct IPv4 {

	uint32_t data;

	IPv4(uint32_t d=0)
	{
		data = d;
	}

	IPv4(const char *str)
	{
		if (!parse(str))
			data = 0;
	}

	bool parse(const char *str, int len=-1);
};

struct IPv6 {

	uint16_t data[8];

	IPv6()
	{
		for (int i = 0; i < 8; i++)
			data[i] = 0;
	}

	IPv6(const char *str)
	{
		if (!parse(str)) {
			for (int i = 0; i < 8; i++)
				data[i] = 0;
		}
	}

	bool parse(const char *str, int len=-1);
};
