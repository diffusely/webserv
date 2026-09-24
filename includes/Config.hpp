#pragma once

#include <string>
#include <vector>
#include "ServerConfig.hpp"

class Config
{
public:
	Config(const std::string &path);

	const std::vector<ServerConfig> &getServers() const;

private:
	std::vector<ServerConfig> _servers;

	std::vector<std::string> _tokens;
	size_t _pos;

	void tokenize(const std::string &content);
	void parseServerBlock(ServerConfig &server);
	void parseLocationBlock(ServerConfig &server);
	void serverDirective(ServerConfig &server, const std::string &name, const std::vector<std::string> &args);
	void locationDirective(Location &loc, const std::string &name, const std::vector<std::string> &args);
	std::vector<std::string> readArgs(const std::string &name);

	Listen parseListen(const std::string &value) const;
	int parsePort(const std::string &value) const;
	int parseStatusCode(const std::string &value, int min, int max) const;
	size_t parseSize(const std::string &value) const;

	const std::string &next();
	void expect(const std::string &token);
};
