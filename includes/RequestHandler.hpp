#pragma once

#include <string>
#include "ServerConfig.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"

class RequestHandler
{
public:
	RequestHandler(const ServerConfig &config);

	HttpResponse handle(const HttpRequest &request) const;
	HttpResponse error(int code) const;

private:
	ServerConfig _config;

	HttpResponse serveGet(const Location &loc, const std::string &path) const;
	HttpResponse servePost(const Location &loc, const std::string &path, const HttpRequest &request) const;
	HttpResponse serveDelete(const Location &loc, const std::string &path) const;
	HttpResponse redirect(int code, const std::string &url) const;
	HttpResponse listDirectory(const std::string &fullPath, const std::string &path) const;
	HttpResponse saveMultipart(const std::string &dir, const HttpRequest &request) const;
};
