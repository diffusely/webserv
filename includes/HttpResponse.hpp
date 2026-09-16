#pragma once

#include <string>

class HttpResponse
{
public:
	HttpResponse();

	void setStatus(int code, const std::string &reason);
	void setBody(const std::string &body);

	std::string toString() const;

private:
	int _statusCode;
	std::string _statusReason;
	std::string _body;
};
