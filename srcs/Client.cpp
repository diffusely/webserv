#include "Client.hpp"


Client::Client(int fd)
	: _fd(fd)
{
}

int Client::getFd() const
{
	return _fd;
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
