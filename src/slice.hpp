#ifndef SLICE_HPP
#define SLICE_HPP

#include <cstdint>
#include <cstring>
#include <ostream>

struct Slice {

	const char *str;
	intptr_t    off;
	intptr_t    len;

	Slice()
	{
		whipe();
	}

    Slice(const char *str2, int len2)
    {
        str = str2;
        len = len2;
        off = 0;
    }

	void whipe()
	{
		str = nullptr;
		off = 0;
		len = 0;
	}

	char operator[](int index) const
	{
		assert(index >= 0 && index < len);
		return str[off + index];
	}

	bool operator==(const char *s) const
	{
		intptr_t l = strlen(s);
		if (l != len)
			return false;
		return !strncmp(str+off, s, len);
	}

	friend std::ostream& operator<<(std::ostream& os, Slice& sl)
	{
		os.write(sl.str + sl.off, sl.len);
		return os;
	}
};

#endif