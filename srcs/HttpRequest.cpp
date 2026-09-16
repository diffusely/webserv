#include "HttpRequest.hpp"

HttpRequest::HttpRequest()
{
}

const std::string &HttpRequest::getMethod() const
{
	return _method;
}

const std::string &HttpRequest::getPath() const
{
	return _path;
}
