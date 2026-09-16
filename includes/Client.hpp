#pragma once

#include <string>

class Client
{
public:
	Client(int fd);

	int getFd() const;

private:
	int _fd;
	std::string _readBuffer;
	std::string _writeBuffer;
};
