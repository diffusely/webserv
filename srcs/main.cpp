#include "Server.hpp"
#include "Config.hpp"
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv)
{
	if (argc > 2) {
		std::cerr << "Usage: " << argv[0] << " [config_file]" << std::endl;
		return 1;
	}

	try
	{
		Config config(argc == 2 ? argv[1] : "config/default.conf");
		Server server(config);

		server.run();
	}
	catch (const std::exception &e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}

	return 0;
}
