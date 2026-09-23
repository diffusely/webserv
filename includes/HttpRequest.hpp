#pragma once

#include <string>
#include <map>

class HttpRequest
{
public:
	enum State
	{
		PARSING_REQUEST_LINE,
		PARSING_HEADERS,
		DONE
	};

	HttpRequest();

	void parse(std::string &buffer);
	bool isComplete() const;
	void reset();

	const std::string &getMethod() const;
	const std::string &getPath() const;
	const std::string &getVersion() const;
	const std::map<std::string, std::string> &getHeaders() const;

private:
	State _state;
	std::string _method;
	std::string _path;
	std::string _version;
	std::map<std::string, std::string> _headers;
	std::string _body;

	void parseRequestLine(const std::string &line);
	void parseHeaderLine(const std::string &line);
};
