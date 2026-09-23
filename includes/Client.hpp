#pragma once

#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include "HttpRequest.hpp"

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

	void parseRequest();
	bool requestIsComplete() const;
	const HttpRequest &getRequest() const;
	void resetRequest();

private:
	int _fd;
	std::string _readBuffer;
	std::string _writeBuffer;
	HttpRequest _request;
};
