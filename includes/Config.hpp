#pragma once

#include <string>

class Config
{
public:
	Config();

	int getPort() const;

private:
	int _port;
};
