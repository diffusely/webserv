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
		PARSING_BODY,
		PARSING_CHUNK_SIZE,
		PARSING_CHUNK_DATA,
		PARSING_CHUNK_TRAILER,
		DONE
	};

	HttpRequest();

	void parse(std::string &buffer, size_t maxBodySize);
	bool isComplete() const;
	bool isStarted() const;
	void reset();

	const std::string &getMethod() const;
	const std::string &getPath() const;
	const std::string &getVersion() const;
	const std::map<std::string, std::string> &getHeaders() const;
	std::string getHeader(const std::string &name) const;
	const std::string &getBody() const;
	int getErrorCode() const;

private:
	State _state;
	std::string _method;
	std::string _path;
	std::string _version;
	std::map<std::string, std::string> _headers;
	std::string _body;
	size_t _contentLength;
	size_t _chunkRemaining;
	size_t _headerBytes;
	int _errorCode;

	bool nextLine(std::string &buffer, std::string &line);
	void parseRequestLine(const std::string &line);
	void parseHeaderLine(const std::string &line);
	void startBody(size_t maxBodySize);
	void parseChunkSize(const std::string &line, size_t maxBodySize);
	void parseChunkData(std::string &buffer);
	void fail(int code);
};
