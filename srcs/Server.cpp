#include "Server.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <cctype>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>

Server::Server(const Config &config)
	: _port(config.getPort()), _server_fd(-1), _root(config.getRoot()), _index(config.getIndex())
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

	while (it->second.requestIsComplete()) {
		printRequest(it->second.getRequest());
		queueResponse(it->second);
		it->second.resetRequest();
		it->second.parseRequest();
	}

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

static bool readFile(const std::string &path, std::string &content)
{
	std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
	if (!file.is_open())
		return false;

	std::ostringstream buffer;
	buffer << file.rdbuf();
	content = buffer.str();
	return true;
}

static HttpResponse errorResponse(int code, const std::string &reason)
{
	HttpResponse response;
	std::ostringstream body;

	body << "<h1>" << code << " " << reason << "</h1>";
	response.setStatus(code, reason);
	response.setHeader("Content-Type", "text/html");
	response.setBody(body.str());
	return response;
}

static std::string getContentType(const std::string &path)
{
	size_t dot = path.rfind('.');
	size_t slash = path.rfind('/');

	if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
		return "application/octet-stream";

	std::string ext = path.substr(dot + 1);
	for (size_t i = 0; i < ext.size(); i++)
		ext[i] = std::tolower(ext[i]);

	if (ext == "html" || ext == "htm")
		return "text/html";
	if (ext == "css")
		return "text/css";
	if (ext == "js")
		return "application/javascript";
	if (ext == "txt")
		return "text/plain";
	if (ext == "json")
		return "application/json";
	if (ext == "png")
		return "image/png";
	if (ext == "jpg" || ext == "jpeg")
		return "image/jpeg";
	if (ext == "gif")
		return "image/gif";
	if (ext == "svg")
		return "image/svg+xml";
	if (ext == "ico")
		return "image/x-icon";
	if (ext == "pdf")
		return "application/pdf";
	return "application/octet-stream";
}

static bool escapesRoot(const std::string &path)
{
	if (path.find("/../") != std::string::npos)
		return true;
	return path.size() >= 3 && path.compare(path.size() - 3, 3, "/..") == 0;
}

HttpResponse Server::serveFile(std::string path) const
{
	size_t query = path.find('?');
	if (query != std::string::npos)
		path.erase(query);

	if (path.empty() || path[0] != '/')
		return errorResponse(400, "Bad Request");
	if (escapesRoot(path))
		return errorResponse(403, "Forbidden");

	std::string fullPath = _root + path;
	struct stat info;

	if (stat(fullPath.c_str(), &info) < 0)
		return errorResponse(404, "Not Found");

	if (S_ISDIR(info.st_mode)) {
		if (path[path.size() - 1] != '/') {
			HttpResponse redirect = errorResponse(301, "Moved Permanently");
			redirect.setHeader("Location", path + "/");
			return redirect;
		}
		fullPath += _index;
		if (stat(fullPath.c_str(), &info) < 0)
			return errorResponse(403, "Forbidden");
	}

	if (!S_ISREG(info.st_mode) || access(fullPath.c_str(), R_OK) < 0)
		return errorResponse(403, "Forbidden");

	std::string body;
	if (!readFile(fullPath, body))
		return errorResponse(500, "Internal Server Error");

	HttpResponse response;
	response.setHeader("Content-Type", getContentType(fullPath));
	response.setBody(body);
	return response;
}

void Server::queueResponse(Client &client)
{
	const HttpRequest &request = client.getRequest();
	HttpResponse response;

	if (request.getMethod() != "GET")
		response = errorResponse(405, "Method Not Allowed");
	else
		response = serveFile(request.getPath());

	client.appendToWriteBuffer(response.toString());
}

void Server::closeClient(size_t i, std::map<int, Client>::iterator it)
{
	std::cout << "Client disconnected! fd=" << it->first << std::endl;
	close(it->first);
	_clients.erase(it);
	_pollfds.erase(_pollfds.begin() + i);
}
