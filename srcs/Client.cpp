#include "Client.hpp"

Client::Client(int fd, int listenFd)
	: _fd(fd), _listenFd(listenFd), _lastActivity(std::time(NULL)), _closeAfterWrite(false)
{
}

int Client::getFd() const
{
	return _fd;
}

int Client::getListenFd() const
{
	return _listenFd;
}

void Client::touch()
{
	_lastActivity = std::time(NULL);
}

time_t Client::getLastActivity() const
{
	return _lastActivity;
}

bool Client::hasPartialRequest() const
{
	return !_readBuffer.empty() || _request.isStarted();
}

void Client::appendToReadBuffer(const char *data, size_t len)
{
	_readBuffer.append(data, len);
}

const std::string &Client::getReadBuffer() const
{
	return _readBuffer;
}

void Client::clearReadBuffer()
{
	_readBuffer.clear();
}

void Client::parseRequest(size_t maxBodySize)
{
	_request.parse(_readBuffer, maxBodySize);
}

bool Client::requestIsComplete() const
{
	return _request.isComplete();
}

const HttpRequest &Client::getRequest() const
{
	return _request;
}

void Client::resetRequest()
{
	_request.reset();
}

void Client::markForClose()
{
	_closeAfterWrite = true;
	_readBuffer.clear();
}

bool Client::shouldClose() const
{
	return _closeAfterWrite;
}

void Client::appendToWriteBuffer(const std::string &data)
{
	_writeBuffer += data;
}

bool Client::hasDataToWrite() const
{
	return !_writeBuffer.empty();
}

ssize_t Client::flushWriteBuffer()
{
	ssize_t res = send(_fd, _writeBuffer.c_str(), _writeBuffer.size(), 0);

	if (res > 0)
		_writeBuffer.erase(0, res);

	return res;
}
