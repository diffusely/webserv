#pragma once

#include <string>
#include <ctime>
#include <sys/types.h>
#include <sys/socket.h>
#include "HttpRequest.hpp"

class Client
{
public:
	Client(int fd, int listenFd, const std::string &remoteAddr);

	int getFd() const;
	int getListenFd() const;
	const std::string &getRemoteAddr() const;

	void setWaitingForCgi(bool waiting);
	bool isWaitingForCgi() const;

	void touch();
	time_t getLastActivity() const;
	bool hasPartialRequest() const;

	void appendToReadBuffer(const char *data, size_t len);

	void appendToWriteBuffer(const std::string &data);
	bool hasDataToWrite() const;
	ssize_t flushWriteBuffer();

	void parseRequest(size_t maxBodySize);
	bool requestIsComplete() const;
	const HttpRequest &getRequest() const;
	void resetRequest();

	void markForClose();
	bool shouldClose() const;

private:
	int _fd;
	int _listenFd;
	std::string _remoteAddr;
	bool _waitingForCgi;
	time_t _lastActivity;
	std::string _readBuffer;
	std::string _writeBuffer;
	HttpRequest _request;
	bool _closeAfterWrite;
};
