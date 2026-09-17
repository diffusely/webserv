#include "Server.hpp"
#include "HttpRequest.hpp"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>

Server::Server(int port)
	: _port(port), _server_fd(-1)
{
	setupSocket();
}

Server::~Server()
{
	for (std::map<int, Client>::iterator it = _clients.begin(); it != _clients.end(); ++it)
		close(it->first);

	if (_server_fd >= 0)
		close(_server_fd);
}

void Server::setupSocket()
{
	_server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (_server_fd < 0)
		throw std::runtime_error("socket() failed");

	int opt = 1;
	if (setsockopt(_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		close(_server_fd);
		throw std::runtime_error("setsockopt() failed");
	}

	if (fcntl(_server_fd, F_SETFL, O_NONBLOCK) < 0) {
		close(_server_fd);
		throw std::runtime_error("fcntl() failed");
	}

	sockaddr_in address;
	std::memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(_port);

	if (bind(_server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		close(_server_fd);
		throw std::runtime_error("bind() failed");
	}

	if (listen(_server_fd, 10) < 0) {
		close(_server_fd);
		throw std::runtime_error("listen() failed");
	}

	struct pollfd serverEntry;
	serverEntry.fd = _server_fd;
	serverEntry.events = POLLIN;
	serverEntry.revents = 0;
	_pollfds.push_back(serverEntry);
}

void Server::acceptNewClient()
{
	int fd = accept(_server_fd, NULL, NULL);
	if (fd < 0)
		return;

	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
		close(fd);
		return;
	}

	struct pollfd newEntry;
	newEntry.fd = fd;
	newEntry.events = POLLIN;
	newEntry.revents = 0;
	_pollfds.push_back(newEntry);

	_clients.insert(std::make_pair(fd, Client(fd)));

	std::cout << "Client connected! fd=" << fd << std::endl;
}

void Server::run()
{
	std::cout << "Listening on port " << _port << "..." << std::endl;

	while (true) {
		int ready = poll(&_pollfds[0], _pollfds.size(), -1);
		if (ready < 0)
			throw std::runtime_error("poll() failed");

		for (size_t i = 0; i < _pollfds.size(); ) {
			if (_pollfds[i].revents == 0) {
				i++;
				continue;
			}

			if (_pollfds[i].fd == _server_fd) {
				handleServerEvent(_pollfds[i].revents);
				i++;
				continue;
			}

			if (handleClientEvent(i))
				i++;
		}
	}
}

void Server::handleServerEvent(short revents)
{
	if (revents & POLLIN)
		acceptNewClient();
}

bool Server::handleClientEvent(size_t i)
{
	short revents = _pollfds[i].revents;
	int fd = _pollfds[i].fd;
	std::map<int, Client>::iterator it = _clients.find(fd);
	bool shouldClose = false;

	if (revents & POLLIN)
		shouldClose = !readFromClient(it);

	if (!shouldClose && (revents & POLLOUT) && it->second.hasDataToWrite())
		shouldClose = !writeToClient(it);

	if (shouldClose) {
		closeClient(i, it);
		return false;
	}

	if (it->second.hasDataToWrite())
		_pollfds[i].events |= POLLOUT;
	else
		_pollfds[i].events &= ~POLLOUT;
	return true;
}

bool Server::readFromClient(std::map<int, Client>::iterator it)
{
	char buffer[1024];
	ssize_t bytesRead = recv(it->first, buffer, sizeof(buffer) - 1, 0);

	if (bytesRead <= 0)
		return false;

	it->second.appendToReadBuffer(buffer, bytesRead);
	it->second.parseRequest();

	if (it->second.requestIsComplete())
		printRequest(it->second.getRequest());

	return true;
}

bool Server::writeToClient(std::map<int, Client>::iterator it)
{
	ssize_t sent = it->second.flushWriteBuffer();

	return sent >= 0;
}

void Server::printRequest(const HttpRequest &req) const
{
	std::cout << "Method: " << req.getMethod()
		<< " Path: " << req.getPath()
		<< " Version: " << req.getVersion() << std::endl;

	const std::map<std::string, std::string> &headers = req.getHeaders();
	for (std::map<std::string, std::string>::const_iterator hit = headers.begin(); hit != headers.end(); ++hit)
		std::cout << "  " << hit->first << ": " << hit->second << std::endl;
}

void Server::closeClient(size_t i, std::map<int, Client>::iterator it)
{
	std::cout << "Client disconnected! fd=" << it->first << std::endl;
	close(it->first);
	_clients.erase(it);
	_pollfds.erase(_pollfds.begin() + i);
}
