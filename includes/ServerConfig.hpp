#pragma once

#include <string>
#include <vector>
#include <map>

struct Location
{
	std::string path;
	std::string root;
	std::string index;
	std::vector<std::string> methods;
	bool autoindex;
	std::string uploadStore;
	int redirectCode;
	std::string redirectUrl;

	Location();

	bool allows(const std::string &method) const;
	std::string allowHeader() const;
};

struct Listen
{
	std::string host;
	int port;

	std::string key() const;
};

struct ServerConfig
{
	std::vector<Listen> listens;
	std::vector<std::string> serverNames;
	std::string root;
	std::string index;
	size_t maxBodySize;
	std::map<int, std::string> errorPages;
	std::vector<Location> locations;
	Location fallback;

	ServerConfig();

	void inheritDefaults();
	const Location &findLocation(const std::string &path) const;
	bool hasName(const std::string &host) const;
};
