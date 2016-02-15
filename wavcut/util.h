#ifndef UTIL_H
#define UTIL_H

#define _HAS_EXCEPTIONS 0

#include <stdio.h>
#include <stdint.h>
#include <string>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>

typedef int64_t int64;
typedef uint64_t uint64;
typedef uint32_t uint32;
typedef uint16_t uint16;
typedef uint8_t uint8;
typedef uint32_t uint32;
typedef int32_t int32;
typedef uint8_t byte;
#define null nullptr
#ifdef _MSC_VER
#define debugBreak() __debugbreak()
#else
#define debugBreak() raise(SIGTRAP)
#endif

class StringNumberHolder
{
	static const int buffersize = 256;
	std::string _str;
public:
	StringNumberHolder(int64 n)
	{
		std::ostringstream ss;
		ss << n;
		_str = ss.str();
	}
	StringNumberHolder(int n, int zerofill)
	{
		std::ostringstream ss;
		ss << std::setw(zerofill) << std::setfill('0') << n;
		_str = ss.str();
	}
	StringNumberHolder(double d)
	{
		std::ostringstream ss;
		ss << d;
		_str = ss.str();
	}
	operator const char*()
	{
		return _str.c_str();
	}
};


void assertEqualImpl(int a, int b, const char* msg, int lineno, const char* file);
#define assertEqual(a,b) assertEqualImpl((a), (b), null,__LINE__, __FILE__)
#define assertTrue(b) assertEqualImpl(((b) ? 1 : 0), 1, "err "#b, __LINE__, __FILE__)

#define return_err(err) do { return err; } while(false)
typedef const char* errormsg;
extern const errormsg OK;

std::vector<std::string> splitString(const std::string & s, char delim);
bool fileExists(const char* path);
bool getBoolFromUser(const char* prompt);
uint64 getFileSize(const char* path);
errormsg truncateFile(const char* path, int64 length);
std::vector<int64> parseLengthsFile(const char* filename, int sampleRate);
const uint64 fourgb = 268435456ULL * 16ULL;

class PerfTimer
{
	int64 _start;

public:
	PerfTimer();
	double stop();
};

class SimpleBuffer
{
	byte* _ptr = 0;
	size_t _count = 0;

public:
	SimpleBuffer(size_t count)
	{
		_ptr = (byte*)calloc(count, sizeof(byte));
		_count = count;
	}
	void Clear()
	{
		if (_ptr)
		{
			memset(_ptr, 0, sizeof(byte) * _count);
		}
	}
	byte* Get()
	{ 
		return _ptr;
	}
	size_t Size()
	{
		return sizeof(byte) * _count;
	}
	~SimpleBuffer()
	{
		free(_ptr);
	}
};


inline bool stringEndsWith(const char* s1, const char*s2)
{
	size_t len1 = strlen(s1), len2 = strlen(s2);
	if (len2>len1) return false;
	return strcmp(s1 + len1 - len2, s2) == 0;
}

#endif /* UTIL_H */
