#include "Config.hpp"

Config::Config()
	: _port(8080)
{
}

int Config::getPort() const
{
	return _port;
}
