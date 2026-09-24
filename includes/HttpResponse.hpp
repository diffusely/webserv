#pragma once

#include <string>
#include <map>

class HttpResponse
{
public:
	HttpResponse();

	void setStatus(int code, const std::string &reason);
	void setHeader(const std::string &key, const std::string &value);
	void setBody(const std::string &body);

	int getStatusCode() const;
	std::string toString() const;

private:
	int _statusCode;
	std::string _statusReason;
	std::map<std::string, std::string> _headers;
	std::string _body;
};
