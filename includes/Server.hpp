#pragma once

#include <vector>
#include <map>
#include <set>
#include <poll.h>
#include "Client.hpp"
#include "ServerConfig.hpp"
#include "RequestHandler.hpp"
#include "CgiProcess.hpp"

class Server
{
public:
	Server(const std::vector<ServerConfig> &configs);
	~Server();

	void run();
	static void stop(int signal);

private:
	static const int CLIENT_TIMEOUT = 30;
	static const int CGI_TIMEOUT = 10;

	std::vector<ServerConfig> _configs;
	std::vector<RequestHandler> _handlers;
	std::map<int, std::vector<size_t> > _listeners;
	std::map<int, int> _listenPorts;
	std::vector<struct pollfd> _pollfds;
	std::set<int> _closedThisRound;
	std::map<int, Client> _clients;
	std::map<int, CgiProcess> _cgis;
	std::map<int, int> _cgiPipes;
	std::vector<pid_t> _children;

	void setupSockets();
	int openListenSocket(const Listen &listen);
	void closeAll();

	void addPollFd(int fd, short events);
	void removePollFd(int fd);
	void setPollEvents(int fd, short events);

	void acceptNewClient(int listenFd);
	void handleClientEvent(int fd, short revents);
	bool readFromClient(Client &client);
	void processRequests(Client &client);
	void queueResponse(Client &client);
	void updateClientEvents(Client &client);
	void checkTimeouts();
	void closeClient(int fd);

	bool startCgi(Client &client, const CgiRequest &cgi);
	void handleCgiEvent(int pipeFd, short revents);
	void finishCgi(int clientFd, int errorCode);
	void reapChildren();

	size_t pickServer(const Client &client) const;
	size_t maxBodySize(int listenFd) const;

	Server(const Server &other);
	Server &operator=(const Server &other);
};
