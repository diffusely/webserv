#include "HttpRequest.hpp"
#include <cctype>

static const size_t MAX_LINE = 8192;
static const size_t MAX_HEADERS = 32768;

HttpRequest::HttpRequest()
	: _state(PARSING_REQUEST_LINE), _contentLength(0), _chunkRemaining(0), _headerBytes(0), _errorCode(0)
{
}

void HttpRequest::parse(std::string &buffer, size_t maxBodySize)
{
	std::string line;

	while (_state != DONE) {
		if (_state == PARSING_BODY) {
			size_t missing = _contentLength - _body.size();
			size_t take = buffer.size() < missing ? buffer.size() : missing;

			_body.append(buffer, 0, take);
			buffer.erase(0, take);
			if (_body.size() == _contentLength)
				_state = DONE;
			return;
		}

		if (_state == PARSING_CHUNK_DATA) {
			if (buffer.size() < _chunkRemaining + 2)
				return;
			parseChunkData(buffer);
			continue;
		}

		if (!nextLine(buffer, line))
			return;

		if (_state == PARSING_REQUEST_LINE) {
			// RFC 9112: a server should ignore empty lines before the request line
			if (!line.empty())
				parseRequestLine(line);
		} else if (_state == PARSING_HEADERS) {
			if (line.empty())
				startBody(maxBodySize);
			else
				parseHeaderLine(line);
		} else if (_state == PARSING_CHUNK_SIZE) {
			parseChunkSize(line, maxBodySize);
		} else if (_state == PARSING_CHUNK_TRAILER) {
			if (line.empty())
				_state = DONE;
		}
	}
}

// takes one line ending in "\r\n" (or just "\n") off the front of buffer
bool HttpRequest::nextLine(std::string &buffer, std::string &line)
{
	size_t pos = buffer.find('\n');

	if (pos == std::string::npos) {
		if (buffer.size() > MAX_LINE)
			fail(_state == PARSING_REQUEST_LINE ? 414 : 431);
		return false;
	}
	if (pos > MAX_LINE) {
		fail(_state == PARSING_REQUEST_LINE ? 414 : 431);
		return false;
	}

	line = buffer.substr(0, pos);
	buffer.erase(0, pos + 1);
	if (!line.empty() && line[line.size() - 1] == '\r')
		line.erase(line.size() - 1);

	if (_state == PARSING_HEADERS) {
		_headerBytes += pos + 1;
		if (_headerBytes > MAX_HEADERS) {
			fail(431);
			return false;
		}
	}
	return true;
}

// "GET /index.html HTTP/1.1" - exactly three parts separated by single spaces
void HttpRequest::parseRequestLine(const std::string &line)
{
	size_t first = line.find(' ');
	size_t second = first == std::string::npos ? first : line.find(' ', first + 1);

	if (second == std::string::npos || line.find(' ', second + 1) != std::string::npos)
		return fail(400);

	_method = line.substr(0, first);
	_path = line.substr(first + 1, second - first - 1);
	_version = line.substr(second + 1);

	if (_method.empty() || _path.empty() || _path[0] != '/')
		return fail(400);
	for (size_t i = 0; i < _method.size(); i++) {
		if (!std::isupper(_method[i]))
			return fail(400);
	}
	if (_version.compare(0, 5, "HTTP/") != 0)
		return fail(400);
	if (_version != "HTTP/1.1" && _version != "HTTP/1.0")
		return fail(505);

	_state = PARSING_HEADERS;
}

void HttpRequest::parseHeaderLine(const std::string &line)
{
	size_t colon = line.find(':');

	if (colon == std::string::npos || colon == 0)
		return fail(400);

	std::string key = line.substr(0, colon);
	for (size_t i = 0; i < key.size(); i++) {
		if (key[i] == ' ' || key[i] == '\t')
			return fail(400);
		key[i] = std::tolower(key[i]);
	}

	size_t start = colon + 1;
	size_t end = line.size();
	while (start < end && (line[start] == ' ' || line[start] == '\t'))
		start++;
	while (end > start && (line[end - 1] == ' ' || line[end - 1] == '\t'))
		end--;

	std::string value = line.substr(start, end - start);

	// two different Content-Length values = we can't know where the body ends
	if (key == "content-length" && _headers.count(key) && _headers[key] != value)
		return fail(400);
	_headers[key] = value;
}

// called on the empty line after the headers: decides whether a body follows
void HttpRequest::startBody(size_t maxBodySize)
{
	if (_version == "HTTP/1.1" && !_headers.count("host"))
		return fail(400);

	bool hasLength = _headers.count("content-length");
	bool hasEncoding = _headers.count("transfer-encoding");

	if (hasEncoding) {
		std::string encoding = getHeader("transfer-encoding");
		for (size_t i = 0; i < encoding.size(); i++)
			encoding[i] = std::tolower(encoding[i]);

		// both at once is how request smuggling works - refuse it
		if (hasLength)
			return fail(400);
		if (encoding != "chunked")
			return fail(501);
		_state = PARSING_CHUNK_SIZE;
		return;
	}

	if (!hasLength) {
		_state = DONE;
		return;
	}

	const std::string value = getHeader("content-length");
	if (value.empty())
		return fail(400);

	_contentLength = 0;
	for (size_t i = 0; i < value.size(); i++) {
		if (value[i] < '0' || value[i] > '9')
			return fail(400);
		if (_contentLength > maxBodySize)
			return fail(413);
		_contentLength = _contentLength * 10 + (value[i] - '0');
	}

	if (_contentLength > maxBodySize)
		return fail(413);

	_state = _contentLength == 0 ? DONE : PARSING_BODY;
}

// chunked body:
// 5\r\n
// hello\r\n
// 0\r\n
// \r\n
void HttpRequest::parseChunkSize(const std::string &line, size_t maxBodySize)
{
	std::string hex = line.substr(0, line.find(';'));
	size_t size = 0;

	while (!hex.empty() && (hex[hex.size() - 1] == ' ' || hex[hex.size() - 1] == '\t'))
		hex.erase(hex.size() - 1);
	if (hex.empty() || hex.size() > 8)
		return fail(400);

	for (size_t i = 0; i < hex.size(); i++) {
		char c = std::tolower(hex[i]);

		if (c >= '0' && c <= '9')
			size = size * 16 + (c - '0');
		else if (c >= 'a' && c <= 'f')
			size = size * 16 + (c - 'a' + 10);
		else
			return fail(400);
	}

	if (size == 0) {
		_state = PARSING_CHUNK_TRAILER;
		return;
	}
	if (_body.size() + size > maxBodySize)
		return fail(413);

	_chunkRemaining = size;
	_state = PARSING_CHUNK_DATA;
}

void HttpRequest::parseChunkData(std::string &buffer)
{
	if (buffer.compare(_chunkRemaining, 2, "\r\n") != 0)
		return fail(400);

	_body.append(buffer, 0, _chunkRemaining);
	buffer.erase(0, _chunkRemaining + 2);
	_chunkRemaining = 0;
	_state = PARSING_CHUNK_SIZE;
}

void HttpRequest::fail(int code)
{
	_errorCode = code;
	_state = DONE;
}

bool HttpRequest::isComplete() const
{
	return _state == DONE;
}

bool HttpRequest::isStarted() const
{
	return _state != PARSING_REQUEST_LINE;
}

void HttpRequest::reset()
{
	_state = PARSING_REQUEST_LINE;
	_method.clear();
	_path.clear();
	_version.clear();
	_headers.clear();
	_body.clear();
	_contentLength = 0;
	_chunkRemaining = 0;
	_headerBytes = 0;
	_errorCode = 0;
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

std::string HttpRequest::getHeader(const std::string &name) const
{
	std::map<std::string, std::string>::const_iterator it = _headers.find(name);

	if (it == _headers.end())
		return "";
	return it->second;
}

const std::string &HttpRequest::getBody() const
{
	return _body;
}

int HttpRequest::getErrorCode() const
{
	return _errorCode;
}
