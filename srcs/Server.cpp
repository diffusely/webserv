#include "Server.hpp"
#include "HttpRequest.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

Server::Server(const std::vector<ServerConfig> &configs)
	: _configs(configs)
{
	for (size_t i = 0; i < _configs.size(); i++)
		_handlers.push_back(RequestHandler(_configs[i]));

	try {
		setupSockets();
	} catch (...) {
		closeAll();
		throw;
	}
}

Server::~Server()
{
	closeAll();
}

void Server::closeAll()
{
	for (size_t i = 0; i < _pollfds.size(); i++)
		close(_pollfds[i].fd);
	_pollfds.clear();
	_clients.clear();
	_listeners.clear();
}

// several server blocks may share one host:port - they share one socket,
// and the Host header picks which of them answers
void Server::setupSockets()
{
	std::map<std::string, int> opened;

	for (size_t i = 0; i < _configs.size(); i++) {
		for (size_t j = 0; j < _configs[i].listens.size(); j++) {
			const Listen &listen = _configs[i].listens[j];
			std::map<std::string, int>::iterator it = opened.find(listen.key());

			int fd;
			if (it != opened.end()) {
				fd = it->second;
			} else {
				fd = openListenSocket(listen);
				opened[listen.key()] = fd;
				std::cout << "Listening on " << listen.key() << std::endl;
			}
			_listeners[fd].push_back(i);
		}
	}
}

int Server::openListenSocket(const Listen &listen)
{
	struct addrinfo hints;
	struct addrinfo *result;
	std::ostringstream port;

	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	port << listen.port;

	int status = getaddrinfo(listen.host.c_str(), port.str().c_str(), &hints, &result);
	if (status != 0)
		throw std::runtime_error("cannot resolve " + listen.key() + ": " + gai_strerror(status));

	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		freeaddrinfo(result);
		throw std::runtime_error("socket() failed");
	}

	int opt = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0
		|| fcntl(fd, F_SETFL, O_NONBLOCK) < 0
		|| bind(fd, result->ai_addr, result->ai_addrlen) < 0
		|| ::listen(fd, SOMAXCONN) < 0) {
		freeaddrinfo(result);
		close(fd);
		throw std::runtime_error("cannot listen on " + listen.key());
	}
	freeaddrinfo(result);

	struct pollfd entry;
	entry.fd = fd;
	entry.events = POLLIN;
	entry.revents = 0;
	_pollfds.push_back(entry);
	return fd;
}

void Server::acceptNewClient(int listenFd)
{
	int fd = accept(listenFd, NULL, NULL);
	if (fd < 0)
		return;

	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
		close(fd);
		return;
	}

	struct pollfd entry;
	entry.fd = fd;
	entry.events = POLLIN;
	entry.revents = 0;
	_pollfds.push_back(entry);

	_clients.insert(std::make_pair(fd, Client(fd, listenFd)));
}

void Server::run()
{
	while (true) {
		// wake up at least once a second so timeouts get checked even when nobody talks
		int ready = poll(&_pollfds[0], _pollfds.size(), 1000);
		if (ready < 0)
			throw std::runtime_error("poll() failed");

		for (size_t i = 0; i < _pollfds.size(); ) {
			if (_pollfds[i].revents == 0) {
				i++;
				continue;
			}

			if (_listeners.count(_pollfds[i].fd)) {
				if (_pollfds[i].revents & POLLIN)
					acceptNewClient(_pollfds[i].fd);
				i++;
				continue;
			}

			if (handleClientEvent(i))
				i++;
		}

		checkTimeouts();
	}
}

// returns false if the client was closed (and removed from _pollfds)
bool Server::handleClientEvent(size_t i)
{
	short revents = _pollfds[i].revents;
	Client &client = _clients.find(_pollfds[i].fd)->second;
	bool shouldClose = false;

	if (revents & POLLIN)
		shouldClose = !readFromClient(client);
	else if (revents & (POLLHUP | POLLERR | POLLNVAL))
		shouldClose = true;

	if (!shouldClose && (revents & POLLOUT) && client.hasDataToWrite()) {
		if (client.flushWriteBuffer() < 0)
			shouldClose = true;
		else
			client.touch();
	}

	if (client.shouldClose() && !client.hasDataToWrite())
		shouldClose = true;

	if (shouldClose) {
		closeClient(i);
		return false;
	}

	if (client.hasDataToWrite())
		_pollfds[i].events |= POLLOUT;
	else
		_pollfds[i].events &= ~POLLOUT;
	return true;
}

bool Server::readFromClient(Client &client)
{
	char buffer[8192];
	ssize_t bytesRead = recv(client.getFd(), buffer, sizeof(buffer), 0);

	if (bytesRead <= 0)
		return false;
	client.touch();

	if (client.shouldClose())
		return true;

	size_t limit = maxBodySize(client.getListenFd());

	client.appendToReadBuffer(buffer, bytesRead);
	client.parseRequest(limit);

	while (client.requestIsComplete()) {
		queueResponse(client);
		if (client.shouldClose())
			break;
		client.resetRequest();
		client.parseRequest(limit);
	}
	return true;
}

void Server::queueResponse(Client &client)
{
	const HttpRequest &request = client.getRequest();
	size_t index = pickServer(client);
	HttpResponse response;

	if (request.getErrorCode()) {
		// the rest of a broken request is still in the socket, we can't find where the next one starts
		response = _handlers[index].error(request.getErrorCode());
		client.markForClose();
	} else if (request.getBody().size() > _configs[index].maxBodySize) {
		// the parser used the biggest limit of this port, this server's own limit is smaller
		response = _handlers[index].error(413);
	} else {
		response = _handlers[index].handle(request);
	}

	std::string connection = request.getHeader("connection");
	if (connection == "close" || (request.getVersion() == "HTTP/1.0" && connection != "keep-alive"))
		client.markForClose();
	if (client.shouldClose())
		response.setHeader("Connection", "close");

	std::cout << "[" << client.getFd() << "] " << request.getMethod() << " " << request.getPath()
		<< " -> " << response.getStatusCode() << std::endl;
	client.appendToWriteBuffer(response.toString());
}

void Server::checkTimeouts()
{
	time_t now = std::time(NULL);

	for (size_t i = 0; i < _pollfds.size(); ) {
		std::map<int, Client>::iterator it = _clients.find(_pollfds[i].fd);

		if (it == _clients.end() || now - it->second.getLastActivity() < CLIENT_TIMEOUT) {
			i++;
			continue;
		}

		Client &client = it->second;
		if (client.hasPartialRequest() && !client.shouldClose()) {
			// stuck in the middle of a request: tell it why before hanging up
			HttpResponse response = _handlers[pickServer(client)].error(408);
			response.setHeader("Connection", "close");
			client.markForClose();
			client.appendToWriteBuffer(response.toString());
			client.touch();
			_pollfds[i].events |= POLLOUT;
			std::cout << "[" << client.getFd() << "] timeout -> 408" << std::endl;
			i++;
		} else {
			std::cout << "[" << client.getFd() << "] timeout" << std::endl;
			closeClient(i);
		}
	}
}

void Server::closeClient(size_t i)
{
	close(_pollfds[i].fd);
	_clients.erase(_pollfds[i].fd);
	_pollfds.erase(_pollfds.begin() + i);
}

size_t Server::pickServer(const Client &client) const
{
	const std::vector<size_t> &candidates = _listeners.find(client.getListenFd())->second;
	std::string host = client.getRequest().getHeader("host");

	for (size_t i = 0; i < candidates.size(); i++) {
		if (_configs[candidates[i]].hasName(host))
			return candidates[i];
	}
	return candidates[0];
}

size_t Server::maxBodySize(int listenFd) const
{
	const std::vector<size_t> &candidates = _listeners.find(listenFd)->second;
	size_t biggest = 0;

	for (size_t i = 0; i < candidates.size(); i++) {
		if (_configs[candidates[i]].maxBodySize > biggest)
			biggest = _configs[candidates[i]].maxBodySize;
	}
	return biggest;
}
