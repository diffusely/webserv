#pragma once

#include <vector>
#include <map>
#include <poll.h>
#include "Client.hpp"
#include "ServerConfig.hpp"
#include "RequestHandler.hpp"

class Server
{
public:
	Server(const std::vector<ServerConfig> &configs);
	~Server();

	void run();

private:
	static const int CLIENT_TIMEOUT = 30;

	std::vector<ServerConfig> _configs;
	std::vector<RequestHandler> _handlers;
	std::map<int, std::vector<size_t> > _listeners;
	std::vector<struct pollfd> _pollfds;
	std::map<int, Client> _clients;

	void setupSockets();
	int openListenSocket(const Listen &listen);
	void closeAll();

	void acceptNewClient(int listenFd);
	bool handleClientEvent(size_t i);
	bool readFromClient(Client &client);
	void queueResponse(Client &client);
	void checkTimeouts();
	void closeClient(size_t i);

	size_t pickServer(const Client &client) const;
	size_t maxBodySize(int listenFd) const;

	Server(const Server &other);
	Server &operator=(const Server &other);
};
