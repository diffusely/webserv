#include "Config.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cctype>

Config::Config(const std::string &path)
	: _pos(0)
{
	std::ifstream file(path.c_str());
	if (!file.is_open())
		throw std::runtime_error("cannot open config file: " + path);

	std::ostringstream content;
	content << file.rdbuf();
	tokenize(content.str());

	while (_pos < _tokens.size()) {
		ServerConfig server;

		expect("server");
		parseServerBlock(server);

		if (server.listens.empty())
			throw std::runtime_error("config: server block without 'listen'");
		if (server.root.empty())
			throw std::runtime_error("config: server block without 'root'");
		server.inheritDefaults();
		_servers.push_back(server);
	}

	if (_servers.empty())
		throw std::runtime_error("config: no server blocks in " + path);
}

void Config::tokenize(const std::string &content)
{
	std::string current;

	for (size_t i = 0; i < content.size(); i++) {
		char c = content[i];

		if (c == '#') {
			while (i < content.size() && content[i] != '\n')
				i++;
			c = '\n';
		}

		if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '{' || c == '}' || c == ';') {
			if (!current.empty()) {
				_tokens.push_back(current);
				current.clear();
			}
			if (c == '{' || c == '}' || c == ';')
				_tokens.push_back(std::string(1, c));
		} else {
			current += c;
		}
	}

	if (!current.empty())
		_tokens.push_back(current);
}

void Config::parseServerBlock(ServerConfig &server)
{
	expect("{");

	while (true) {
		const std::string &token = next();

		if (token == "}")
			return;
		if (token == "location") {
			parseLocationBlock(server);
		} else {
			std::string name = token;
			serverDirective(server, name, readArgs(name));
		}
	}
}

void Config::parseLocationBlock(ServerConfig &server)
{
	Location loc;

	loc.path = next();
	if (loc.path.empty() || loc.path[0] != '/')
		throw std::runtime_error("config: location path must start with '/': '" + loc.path + "'");
	for (size_t i = 0; i < server.locations.size(); i++) {
		if (server.locations[i].path == loc.path)
			throw std::runtime_error("config: duplicate location '" + loc.path + "'");
	}

	expect("{");
	while (true) {
		const std::string &token = next();

		if (token == "}")
			break;
		std::string name = token;
		locationDirective(loc, name, readArgs(name));
	}

	server.locations.push_back(loc);
}

std::vector<std::string> Config::readArgs(const std::string &name)
{
	std::vector<std::string> args;

	while (true) {
		const std::string &token = next();

		if (token == ";")
			break;
		if (token == "{" || token == "}")
			throw std::runtime_error("config: missing ';' after '" + name + "'");
		args.push_back(token);
	}
	if (args.empty())
		throw std::runtime_error("config: '" + name + "' needs a value");
	return args;
}

static void requireOne(const std::string &name, const std::vector<std::string> &args)
{
	if (args.size() != 1)
		throw std::runtime_error("config: '" + name + "' takes exactly one value");
}

void Config::serverDirective(ServerConfig &server, const std::string &name, const std::vector<std::string> &args)
{
	if (name == "server_name") {
		for (size_t i = 0; i < args.size(); i++) {
			std::string lower = args[i];
			for (size_t j = 0; j < lower.size(); j++)
				lower[j] = std::tolower(lower[j]);
			server.serverNames.push_back(lower);
		}
		return;
	}

	if (name == "error_page") {
		// error_page 500 502 503 /50x.html;
		if (args.size() < 2)
			throw std::runtime_error("config: 'error_page' needs at least one code and a page");
		for (size_t i = 0; i + 1 < args.size(); i++)
			server.errorPages[parseStatusCode(args[i], 400, 599)] = args.back();
		return;
	}

	requireOne(name, args);
	if (name == "listen") {
		Listen listen = parseListen(args[0]);
		for (size_t i = 0; i < server.listens.size(); i++) {
			if (server.listens[i].key() == listen.key())
				throw std::runtime_error("config: duplicate listen '" + args[0] + "'");
		}
		server.listens.push_back(listen);
	}
	else if (name == "root")
		server.root = args[0];
	else if (name == "index")
		server.index = args[0];
	else if (name == "client_max_body_size")
		server.maxBodySize = parseSize(args[0]);
	else
		throw std::runtime_error("config: unknown directive '" + name + "'");
}

void Config::locationDirective(Location &loc, const std::string &name, const std::vector<std::string> &args)
{
	if (name == "methods") {
		for (size_t i = 0; i < args.size(); i++) {
			if (args[i] != "GET" && args[i] != "POST" && args[i] != "DELETE")
				throw std::runtime_error("config: unsupported method '" + args[i] + "'");
			if (!loc.allows(args[i]))
				loc.methods.push_back(args[i]);
		}
		return;
	}

	if (name == "return") {
		// return 301 http://example.com/;
		if (args.size() != 2)
			throw std::runtime_error("config: 'return' takes a code and a url");
		loc.redirectCode = parseStatusCode(args[0], 301, 308);
		if (loc.redirectCode > 303 && loc.redirectCode < 307)
			throw std::runtime_error("config: invalid redirect code '" + args[0] + "'");
		loc.redirectUrl = args[1];
		return;
	}

	requireOne(name, args);
	if (name == "root") {
		loc.root = args[0];
	} else if (name == "index") {
		loc.index = args[0];
	} else if (name == "upload_store") {
		loc.uploadStore = args[0];
	} else if (name == "autoindex") {
		if (args[0] != "on" && args[0] != "off")
			throw std::runtime_error("config: 'autoindex' must be on or off");
		loc.autoindex = args[0] == "on";
	} else {
		throw std::runtime_error("config: unknown directive '" + name + "' in location");
	}
}

// "8080" -> 0.0.0.0:8080, "127.0.0.1:8080" -> 127.0.0.1:8080
Listen Config::parseListen(const std::string &value) const
{
	Listen listen;
	size_t colon = value.rfind(':');

	if (colon == std::string::npos) {
		listen.host = "0.0.0.0";
		listen.port = parsePort(value);
	} else {
		listen.host = value.substr(0, colon);
		listen.port = parsePort(value.substr(colon + 1));
		if (listen.host.empty())
			throw std::runtime_error("config: invalid listen '" + value + "'");
	}
	return listen;
}

int Config::parsePort(const std::string &value) const
{
	long port = 0;

	for (size_t i = 0; i < value.size(); i++) {
		if (value[i] < '0' || value[i] > '9' || port > 65535)
			throw std::runtime_error("config: invalid port '" + value + "'");
		port = port * 10 + (value[i] - '0');
	}
	if (value.empty() || port < 1 || port > 65535)
		throw std::runtime_error("config: invalid port '" + value + "'");
	return port;
}

int Config::parseStatusCode(const std::string &value, int min, int max) const
{
	int code = 0;

	if (value.size() != 3)
		throw std::runtime_error("config: invalid status code '" + value + "'");
	for (size_t i = 0; i < value.size(); i++) {
		if (value[i] < '0' || value[i] > '9')
			throw std::runtime_error("config: invalid status code '" + value + "'");
		code = code * 10 + (value[i] - '0');
	}
	if (code < min || code > max)
		throw std::runtime_error("config: invalid status code '" + value + "'");
	return code;
}

// "1024", "10K", "5M" -> bytes
size_t Config::parseSize(const std::string &value) const
{
	size_t digits = 0;
	size_t size = 0;

	while (digits < value.size() && value[digits] >= '0' && value[digits] <= '9') {
		if (size > 1024 * 1024 * 1024)
			throw std::runtime_error("config: body size too big '" + value + "'");
		size = size * 10 + (value[digits] - '0');
		digits++;
	}
	if (digits == 0 || value.size() - digits > 1)
		throw std::runtime_error("config: invalid size '" + value + "'");

	if (digits < value.size()) {
		char unit = value[digits];

		if (unit == 'k' || unit == 'K')
			size *= 1024;
		else if (unit == 'm' || unit == 'M')
			size *= 1024 * 1024;
		else
			throw std::runtime_error("config: invalid size '" + value + "'");
	}
	if (size > 1024 * 1024 * 1024)
		throw std::runtime_error("config: body size too big '" + value + "'");
	return size;
}

const std::string &Config::next()
{
	if (_pos >= _tokens.size())
		throw std::runtime_error("config: unexpected end of file");
	return _tokens[_pos++];
}

void Config::expect(const std::string &token)
{
	const std::string &got = next();

	if (got != token)
		throw std::runtime_error("config: expected '" + token + "', got '" + got + "'");
}

const std::vector<ServerConfig> &Config::getServers() const
{
	return _servers;
}
