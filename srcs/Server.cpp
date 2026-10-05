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
#include <sys/wait.h>
#include <netinet/in.h>
#include <csignal>

// set from the signal handler, so it has to be this simple type
static volatile sig_atomic_t g_running = 1;

void Server::stop(int signal)
{
	(void)signal;
	g_running = 0;
}

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
	for (std::map<int, CgiProcess>::iterator it = _cgis.begin(); it != _cgis.end(); ++it)
		it->second.kill();
	for (size_t i = 0; i < _pollfds.size(); i++)
		close(_pollfds[i].fd);
	_pollfds.clear();
	_clients.clear();
	_cgis.clear();
	_cgiPipes.clear();
	_listeners.clear();
	reapChildren();
}

// ---------------------------------------------------------------- listening sockets

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
				_listenPorts[fd] = listen.port;
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

	addPollFd(fd, POLLIN);
	return fd;
}

// ---------------------------------------------------------------- pollfd list

void Server::addPollFd(int fd, short events)
{
	struct pollfd entry;

	entry.fd = fd;
	entry.events = events;
	entry.revents = 0;
	_pollfds.push_back(entry);
}

// closes nothing - only stops watching fd
void Server::removePollFd(int fd)
{
	for (size_t i = 0; i < _pollfds.size(); i++) {
		if (_pollfds[i].fd == fd) {
			_pollfds.erase(_pollfds.begin() + i);
			break;
		}
	}
	_closedThisRound.insert(fd);
}

void Server::setPollEvents(int fd, short events)
{
	for (size_t i = 0; i < _pollfds.size(); i++) {
		if (_pollfds[i].fd == fd) {
			_pollfds[i].events = events;
			return;
		}
	}
}

// ---------------------------------------------------------------- main loop

void Server::run()
{
	while (g_running) {
		// wake up at least once a second so timeouts get checked even when nobody talks
		int ready = poll(&_pollfds[0], _pollfds.size(), 1000);
		if (ready < 0) {
			// Ctrl+C interrupts poll() - that's a normal shutdown, not an error
			if (!g_running)
				break;
			throw std::runtime_error("poll() failed");
		}

		// handling one fd can close others (a finished CGI closes its pipes),
		// so we walk over a copy and skip fds that were closed during this round
		std::vector<struct pollfd> events = _pollfds;
		_closedThisRound.clear();

		for (size_t i = 0; i < events.size(); i++) {
			int fd = events[i].fd;
			short revents = events[i].revents;

			if (revents == 0 || _closedThisRound.count(fd))
				continue;

			if (_listeners.count(fd)) {
				if (revents & POLLIN)
					acceptNewClient(fd);
			} else if (_cgiPipes.count(fd)) {
				handleCgiEvent(fd, revents);
			} else if (_clients.count(fd)) {
				handleClientEvent(fd, revents);
			}
		}

		checkTimeouts();
		reapChildren();
	}
	std::cout << "\nShutting down" << std::endl;
}

// ---------------------------------------------------------------- clients

// "127.0.0.1" without inet_ntoa (not on the allowed list)
static std::string formatAddress(const struct sockaddr_in &address)
{
	unsigned long ip = ntohl(address.sin_addr.s_addr);
	std::ostringstream out;

	out << ((ip >> 24) & 255) << "." << ((ip >> 16) & 255) << "."
		<< ((ip >> 8) & 255) << "." << (ip & 255);
	return out.str();
}

void Server::acceptNewClient(int listenFd)
{
	struct sockaddr_in address;
	socklen_t length = sizeof(address);

	int fd = accept(listenFd, (struct sockaddr *)&address, &length);
	if (fd < 0)
		return;

	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
		close(fd);
		return;
	}

	addPollFd(fd, POLLIN);
	_clients.insert(std::make_pair(fd, Client(fd, listenFd, formatAddress(address))));
}

void Server::handleClientEvent(int fd, short revents)
{
	Client &client = _clients.find(fd)->second;
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

	if (!shouldClose && !client.hasDataToWrite()) {
		if (client.shouldClose() && !client.isWaitingForCgi())
			shouldClose = true;
	}

	if (shouldClose)
		closeClient(fd);
	else
		updateClientEvents(client);
}

void Server::updateClientEvents(Client &client)
{
	short events = POLLIN;

	if (client.hasDataToWrite())
		events |= POLLOUT;
	setPollEvents(client.getFd(), events);
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

	client.appendToReadBuffer(buffer, bytesRead);
	processRequests(client);
	return true;
}

// answers every complete request in the read buffer; stops while a CGI is running,
// because responses must go out in the same order the requests came in
void Server::processRequests(Client &client)
{
	size_t limit = maxBodySize(client.getListenFd());

	if (client.isWaitingForCgi())
		return;

	client.parseRequest(limit);
	while (client.requestIsComplete()) {
		queueResponse(client);
		if (client.shouldClose() || client.isWaitingForCgi())
			break;
		client.resetRequest();
		client.parseRequest(limit);
	}
}

void Server::queueResponse(Client &client)
{
	const HttpRequest &request = client.getRequest();
	size_t index = pickServer(client);
	HttpResponse response;
	CgiRequest cgi;

	if (request.getErrorCode()) {
		// the rest of a broken request is still in the socket, we can't find where the next one starts
		response = _handlers[index].error(request.getErrorCode());
		client.markForClose();
	} else if (request.getBody().size() > _configs[index].maxBodySize) {
		// the parser used the biggest limit of this port, this server's own limit is smaller
		response = _handlers[index].error(413);
	} else if (_handlers[index].findCgi(request, cgi)) {
		if (startCgi(client, cgi))
			return;
		response = _handlers[index].error(500);
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
	std::vector<int> expired;

	// scripts stuck in an infinite loop: kill them, the client gets 504
	for (std::map<int, CgiProcess>::iterator it = _cgis.begin(); it != _cgis.end(); ++it) {
		if (now - it->second.getStartTime() >= CGI_TIMEOUT)
			expired.push_back(it->first);
	}
	for (size_t i = 0; i < expired.size(); i++) {
		std::cout << "[" << expired[i] << "] cgi timeout -> 504" << std::endl;
		_cgis.find(expired[i])->second.kill();
		finishCgi(expired[i], 504);
	}
	expired.clear();

	for (std::map<int, Client>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
		if (!it->second.isWaitingForCgi() && now - it->second.getLastActivity() >= CLIENT_TIMEOUT)
			expired.push_back(it->first);
	}

	for (size_t i = 0; i < expired.size(); i++) {
		Client &client = _clients.find(expired[i])->second;

		if (client.hasPartialRequest() && !client.shouldClose()) {
			// stuck in the middle of a request: tell it why before hanging up
			HttpResponse response = _handlers[pickServer(client)].error(408);
			response.setHeader("Connection", "close");
			client.markForClose();
			client.appendToWriteBuffer(response.toString());
			client.touch();
			updateClientEvents(client);
			std::cout << "[" << client.getFd() << "] timeout -> 408" << std::endl;
		} else {
			std::cout << "[" << client.getFd() << "] timeout" << std::endl;
			closeClient(expired[i]);
		}
	}
}

void Server::closeClient(int fd)
{
	removePollFd(fd);
	close(fd);
	_clients.erase(fd);

	// the client left while its script was still running: nobody needs the output anymore
	std::map<int, CgiProcess>::iterator cgi = _cgis.find(fd);
	if (cgi != _cgis.end()) {
		cgi->second.kill();
		finishCgi(fd, 502);
	}
}

// ---------------------------------------------------------------- CGI

bool Server::startCgi(Client &client, const CgiRequest &cgi)
{
	std::vector<int> fdsToClose;
	for (size_t i = 0; i < _pollfds.size(); i++)
		fdsToClose.push_back(_pollfds[i].fd);

	CgiProcess process;
	int port = _listenPorts[client.getListenFd()];
	if (!process.start(cgi, client.getRequest(), port, client.getRemoteAddr(), fdsToClose))
		return false;

	if (process.getInputFd() >= 0) {
		addPollFd(process.getInputFd(), POLLOUT);
		_cgiPipes[process.getInputFd()] = client.getFd();
	}
	addPollFd(process.getOutputFd(), POLLIN);
	_cgiPipes[process.getOutputFd()] = client.getFd();

	_cgis[client.getFd()] = process;
	_children.push_back(process.getPid());
	client.setWaitingForCgi(true);
	return true;
}

// poll() said one of a script's pipes is ready
void Server::handleCgiEvent(int pipeFd, short revents)
{
	int clientFd = _cgiPipes[pipeFd];
	CgiProcess &cgi = _cgis.find(clientFd)->second;

	if (pipeFd == cgi.getInputFd()) {
		bool ok = !(revents & (POLLERR | POLLHUP | POLLNVAL)) && cgi.writeInput();

		// stdin got closed (all sent, or the script stopped reading): stop watching it
		if (!ok)
			cgi.closeInput();
		if (cgi.getInputFd() != pipeFd) {
			removePollFd(pipeFd);
			_cgiPipes.erase(pipeFd);
		}
		return;
	}

	// POLLHUP with no POLLIN = the script exited and everything was already read
	if (!(revents & (POLLIN | POLLHUP | POLLERR)))
		return;

	ssize_t bytesRead = cgi.readOutput();
	if (bytesRead > 0)
		return;
	finishCgi(clientFd, bytesRead < 0 ? 502 : 0);
}

// output complete (errorCode 0) or script failed (502) / hung (504): answer the client
void Server::finishCgi(int clientFd, int errorCode)
{
	CgiProcess &cgi = _cgis.find(clientFd)->second;

	if (cgi.getInputFd() >= 0) {
		removePollFd(cgi.getInputFd());
		_cgiPipes.erase(cgi.getInputFd());
		cgi.closeInput();
	}
	if (cgi.getOutputFd() >= 0) {
		removePollFd(cgi.getOutputFd());
		_cgiPipes.erase(cgi.getOutputFd());
		cgi.closeOutput();
	}

	std::map<int, Client>::iterator it = _clients.find(clientFd);
	if (it == _clients.end()) {
		_cgis.erase(clientFd);
		return;
	}

	Client &client = it->second;
	size_t index = pickServer(client);
	HttpResponse response;

	if (errorCode)
		response = _handlers[index].error(errorCode);
	else if (!cgi.buildResponse(response))
		response = _handlers[index].error(502);
	_cgis.erase(clientFd);

	const HttpRequest &request = client.getRequest();
	std::string connection = request.getHeader("connection");
	if (connection == "close" || (request.getVersion() == "HTTP/1.0" && connection != "keep-alive"))
		client.markForClose();
	if (client.shouldClose())
		response.setHeader("Connection", "close");

	std::cout << "[" << clientFd << "] " << request.getMethod() << " " << request.getPath()
		<< " -> " << response.getStatusCode() << " (cgi)" << std::endl;
	client.appendToWriteBuffer(response.toString());
	client.setWaitingForCgi(false);
	client.touch();

	// requests that arrived while the script was running are still in the read buffer
	if (!client.shouldClose()) {
		client.resetRequest();
		processRequests(client);
	}
	updateClientEvents(client);
}

// collect exited scripts so they don't stay as zombies; WNOHANG = never block
void Server::reapChildren()
{
	for (size_t i = 0; i < _children.size(); ) {
		if (waitpid(_children[i], NULL, WNOHANG) != 0)
			_children.erase(_children.begin() + i);
		else
			i++;
	}
}

// ---------------------------------------------------------------- helpers

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
