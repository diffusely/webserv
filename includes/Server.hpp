#pragma once

#include <vector>
#include <map>
#include <poll.h>
#include "Client.hpp"
#include "Config.hpp"
#include "HttpResponse.hpp"

class Server
{
public:
	Server(const Config &config);
	~Server();

	void run();

private:
	int _port;
	int _server_fd;
	std::string _root;
	std::string _index;
	std::vector<struct pollfd> _pollfds;
	std::map<int, Client> _clients;

	void setupSocket();
	void acceptNewClient();
	void handleServerEvent(short revents);
	bool handleClientEvent(size_t i);
	bool readFromClient(std::map<int, Client>::iterator it);
	bool writeToClient(std::map<int, Client>::iterator it);
	void printRequest(const HttpRequest &req) const;
	void queueResponse(Client &client);
	HttpResponse serveFile(std::string path) const;
	void closeClient(size_t i, std::map<int, Client>::iterator it);

	Server(const Server &other);
	Server &operator=(const Server &other);
};
