#pragma once

#include <string>
#include <map>

class HttpRequest
{
public:
	HttpRequest();

	const std::string &getMethod() const;
	const std::string &getPath() const;

private:
	std::string _method;
	std::string _path;
	std::map<std::string, std::string> _headers;
	std::string _body;
};
