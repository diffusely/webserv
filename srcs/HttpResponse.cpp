#include "HttpResponse.hpp"
#include <sstream>

HttpResponse::HttpResponse() : _statusCode(200), _statusReason("OK")
{
}

void HttpResponse::setStatus(int code, const std::string &reason)
{
	_statusCode = code;
	_statusReason = reason;
}

void HttpResponse::setBody(const std::string &body)
{
	_body = body;
}

std::string HttpResponse::toString() const
{
	std::ostringstream response;

	response << "HTTP/1.1 " << _statusCode << " " << _statusReason << "\r\n";
	response << "Content-Length: " << _body.size() << "\r\n";
	response << "\r\n";
	response << _body;

	return response.str();
}
