#pragma once

#include <climits>
#include <cstdlib>

#include <iostream>
#include <memory>

#include <sstream>
#include <stdio.h>
#include <string.h>

#include <string> //for std::string support

class String : public std::string
{
public:
	typedef size_t Size;
	enum {NoPosition = std::string::npos};

	String(){}
	String(const char * p) : std::string(p){}
	String(const std::string & s) : std::string(s){}
	
	
	String & operator = (const char * ano)
	{
		std::string::operator=(ano);
		return *this;
	}
	
	bool hasPrefix(const char * ps) const
	{
		return memcmp(data(), ps, strlen(ps))==0;
	}
	void substrInPlace(size_t pos)
	{
		std::string s = substr(pos);
		this->swap(s);
	}
	int caseInsensitiveCompare(const char * p) const
	{
		return strcasecmp(c_str(), p);
	}
	int caseInsensitiveCompare(const char * p, size_t len) const
	{
		return strncasecmp(c_str(), p, len);
	}
	
	
	String operator + (const char * anos) const
	{
		return *(std::string*)this + anos;
	}
	String operator + (const std::string & s) const
	{
		return *(std::string*)this + s;
	}
	String toLower() const
	{
		std::string a;
		std::transform(begin(), end(), std::back_inserter<std::string>(a), ::towlower);
		return a;
	}
	void insert(size_t pos, char ch)
	{
		std::string::insert(pos, 1, ch);
	}
	void insert(size_t pos, size_t cnt, char ch)
	{
		std::string::insert(pos, cnt, ch);
	}
	void insert(size_t pos, char * p)
	{
		std::string::insert(pos, p);
	}
	
	operator const char * () const
	{
		return c_str();
	}
	
	
	static String Fmt(const char * fmt, ...)
	{
		va_list vg;
		va_start(vg, fmt);
		char * vo = 0;
		vasprintf(&vo, fmt, vg);
		String rr(vo);
		va_end(vg);
		free(vo);
		return rr;
	}
};



