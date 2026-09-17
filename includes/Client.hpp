#pragma once

#include <string>
#include <sys/types.h>
#include <sys/socket.h>

class Client
{
public:
	Client(int fd);

	int getFd() const;

	void appendToReadBuffer(const char *data, size_t len);
	const std::string &getReadBuffer() const;
	void clearReadBuffer();

	void appendToWriteBuffer(const std::string &data);
	bool hasDataToWrite() const;
	ssize_t flushWriteBuffer();

private:
	int _fd;
	std::string _readBuffer;
	std::string _writeBuffer;
};
