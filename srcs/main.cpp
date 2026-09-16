#include "Server.hpp"
#include "Config.hpp"
#include <iostream>
#include <stdexcept>

int main()
{
	try
	{
		Config config;
		Server server(config.getPort());

		server.run();
	}
	catch (const std::exception &e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}

	return 0;
}
