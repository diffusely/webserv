#include "Server.hpp"
#include "Config.hpp"
#include <iostream>
#include <stdexcept>
#include <csignal>

int main(int argc, char **argv)
{
	if (argc > 2) {
		std::cerr << "Usage: " << argv[0] << " [config_file]" << std::endl;
		return 1;
	}

	// a client that disconnects while we send() would otherwise kill the whole server
	signal(SIGPIPE, SIG_IGN);

	try
	{
		Config config(argc == 2 ? argv[1] : "config/default.conf");
		Server server(config.getServers());

		server.run();
	}
	catch (const std::exception &e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}

	return 0;
}
