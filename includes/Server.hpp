#pragma once

#include <vector>
#include <map>
#include <poll.h>
#include "Client.hpp"

class Server
{
public:
	Server(int port);
	~Server();

	void run();

private:
	int _port;
	int _server_fd;
	std::vector<struct pollfd> _pollfds;
	std::map<int, Client> _clients;

	void setupSocket();
	void acceptNewClient();

	Server(const Server &other);
	Server &operator=(const Server &other);
};
