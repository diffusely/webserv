#include "ServerConfig.hpp"
#include <sstream>
#include <cctype>

std::string Listen::key() const
{
	std::ostringstream out;

	out << host << ":" << port;
	return out.str();
}

Location::Location()
	: autoindex(false), redirectCode(0)
{
}

bool Location::allows(const std::string &method) const
{
	for (size_t i = 0; i < methods.size(); i++) {
		if (methods[i] == method)
			return true;
	}
	return false;
}

std::string Location::allowHeader() const
{
	std::string result;

	for (size_t i = 0; i < methods.size(); i++) {
		if (i > 0)
			result += ", ";
		result += methods[i];
	}
	return result;
}

ServerConfig::ServerConfig()
	: index("index.html"), maxBodySize(1024 * 1024)
{
}

static void fillFromServer(Location &loc, const ServerConfig &server)
{
	if (loc.root.empty())
		loc.root = server.root;
	if (loc.index.empty())
		loc.index = server.index;
	if (loc.methods.empty()) {
		loc.methods.push_back("GET");
		loc.methods.push_back("POST");
		loc.methods.push_back("DELETE");
	}
}

// locations that didn't set root/index/methods take them from the server block;
// fallback is used when no location matches the request path
void ServerConfig::inheritDefaults()
{
	for (size_t i = 0; i < locations.size(); i++)
		fillFromServer(locations[i], *this);

	fallback.path = "/";
	fillFromServer(fallback, *this);
}

// longest prefix wins: for "/images/cat.png", "/images" beats "/"
// "/img" must not match "/images/..." — the prefix has to end on a '/' boundary
const Location &ServerConfig::findLocation(const std::string &path) const
{
	const Location *best = &fallback;
	size_t bestLength = 0;

	for (size_t i = 0; i < locations.size(); i++) {
		const std::string &prefix = locations[i].path;

		if (path.compare(0, prefix.size(), prefix) != 0)
			continue;

		bool boundary = prefix[prefix.size() - 1] == '/'
			|| path.size() == prefix.size()
			|| path[prefix.size()] == '/';

		if (boundary && prefix.size() > bestLength) {
			best = &locations[i];
			bestLength = prefix.size();
		}
	}
	return *best;
}

// "Host: Example.com:8080" -> compares "example.com" with every server_name
bool ServerConfig::hasName(const std::string &host) const
{
	std::string name = host.substr(0, host.find(':'));

	for (size_t i = 0; i < name.size(); i++)
		name[i] = std::tolower(name[i]);
	for (size_t i = 0; i < serverNames.size(); i++) {
		if (serverNames[i] == name)
			return true;
	}
	return false;
}
