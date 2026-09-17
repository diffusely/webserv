#include "HttpRequest.hpp"
#include <sstream>

HttpRequest::HttpRequest() 
	: _state(PARSING_REQUEST_LINE)
{
}

void HttpRequest::parse(std::string &buffer)
{
	size_t pos;

	while (_state != DONE && (pos = buffer.find("\r\n")) != std::string::npos) {
		std::string line = buffer.substr(0, pos);
		buffer.erase(0, pos + 2);

		if (_state == PARSING_REQUEST_LINE) {
			parseRequestLine(line);
			_state = PARSING_HEADERS;
		} else if (_state == PARSING_HEADERS) {
			if (line.empty())
				_state = DONE;
			else
				parseHeaderLine(line);
		}
	}
}

void HttpRequest::parseRequestLine(const std::string &line)
{
	std::istringstream iss(line);

	iss >> _method >> _path >> _version;
}

void HttpRequest::parseHeaderLine(const std::string &line)
{
	size_t colon = line.find(':');

	if (colon == std::string::npos)
		return;

	std::string key = line.substr(0, colon);
	size_t valueStart = colon + 1;

	while (valueStart < line.size() && line[valueStart] == ' ')
		valueStart++;

	std::string value = line.substr(valueStart);

	_headers[key] = value;
}

bool HttpRequest::isComplete() const
{
	return _state == DONE;
}

const std::string &HttpRequest::getMethod() const
{
	return _method;
}

const std::string &HttpRequest::getPath() const
{
	return _path;
}

const std::string &HttpRequest::getVersion() const
{
	return _version;
}

const std::map<std::string, std::string> &HttpRequest::getHeaders() const
{
	return _headers;
}
