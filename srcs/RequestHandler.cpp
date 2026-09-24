#include "RequestHandler.hpp"
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

static std::string reasonPhrase(int code)
{
	switch (code) {
		case 200: return "OK";
		case 201: return "Created";
		case 204: return "No Content";
		case 301: return "Moved Permanently";
		case 302: return "Found";
		case 303: return "See Other";
		case 307: return "Temporary Redirect";
		case 308: return "Permanent Redirect";
		case 400: return "Bad Request";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 408: return "Request Timeout";
		case 409: return "Conflict";
		case 413: return "Payload Too Large";
		case 414: return "URI Too Long";
		case 431: return "Request Header Fields Too Large";
		case 501: return "Not Implemented";
		case 505: return "HTTP Version Not Supported";
		default: return "Internal Server Error";
	}
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

// strips "?query" and returns an error code if the path is unusable, 0 if it's fine
static int cleanPath(std::string &path)
{
	size_t query = path.find('?');
	if (query != std::string::npos)
		path.erase(query);

	if (path.empty() || path[0] != '/')
		return 400;
	if (escapesRoot(path))
		return 403;
	return 0;
}

static std::string parentDir(const std::string &fullPath)
{
	size_t slash = fullPath.rfind('/');

	if (slash == std::string::npos)
		return ".";
	if (slash == 0)
		return "/";
	return fullPath.substr(0, slash);
}

static std::string htmlEscape(const std::string &text)
{
	std::string result;

	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] == '<')
			result += "&lt;";
		else if (text[i] == '>')
			result += "&gt;";
		else if (text[i] == '&')
			result += "&amp;";
		else if (text[i] == '"')
			result += "&quot;";
		else
			result += text[i];
	}
	return result;
}

// writes data to fullPath, returns 201 on success or the error code to answer with
static int saveFile(const std::string &fullPath, const std::string &data)
{
	std::string dir = parentDir(fullPath);
	struct stat info;

	if (stat(dir.c_str(), &info) < 0 || !S_ISDIR(info.st_mode))
		return 404;
	if (stat(fullPath.c_str(), &info) == 0) {
		if (S_ISDIR(info.st_mode))
			return 409;
		if (access(fullPath.c_str(), W_OK) < 0)
			return 403;
	} else if (access(dir.c_str(), W_OK) < 0) {
		return 403;
	}

	std::ofstream file(fullPath.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
	if (!file.is_open())
		return 500;
	file.write(data.c_str(), data.size());
	if (!file)
		return 500;
	return 201;
}

RequestHandler::RequestHandler(const ServerConfig &config)
	: _config(config)
{
}

HttpResponse RequestHandler::handle(const HttpRequest &request) const
{
	std::string path = request.getPath();
	const std::string &method = request.getMethod();

	int code = cleanPath(path);
	if (code)
		return error(code);

	const Location &loc = _config.findLocation(path);

	if (loc.redirectCode)
		return redirect(loc.redirectCode, loc.redirectUrl);

	if (!loc.allows(method)) {
		HttpResponse response = error(405);
		response.setHeader("Allow", loc.allowHeader());
		return response;
	}

	if (method == "GET")
		return serveGet(loc, path);
	if (method == "POST")
		return servePost(loc, path, request);
	return serveDelete(loc, path);
}

// custom page from error_page if configured and readable, built-in one otherwise
HttpResponse RequestHandler::error(int code) const
{
	HttpResponse response;
	std::string body;

	response.setStatus(code, reasonPhrase(code));
	response.setHeader("Content-Type", "text/html");

	std::map<int, std::string>::const_iterator it = _config.errorPages.find(code);
	if (it != _config.errorPages.end() && readFile(_config.root + it->second, body)) {
		response.setBody(body);
		return response;
	}

	std::ostringstream page;
	page << "<html><head><title>" << code << " " << reasonPhrase(code) << "</title></head>"
		<< "<body><h1>" << code << " " << reasonPhrase(code) << "</h1><hr>webserv</body></html>";
	response.setBody(page.str());
	return response;
}

HttpResponse RequestHandler::redirect(int code, const std::string &url) const
{
	HttpResponse response;

	response.setStatus(code, reasonPhrase(code));
	response.setHeader("Location", url);
	response.setHeader("Content-Type", "text/html");
	response.setBody("<a href=\"" + htmlEscape(url) + "\">" + htmlEscape(url) + "</a>");
	return response;
}

HttpResponse RequestHandler::serveGet(const Location &loc, const std::string &path) const
{
	std::string fullPath = loc.root + path;
	struct stat info;

	if (stat(fullPath.c_str(), &info) < 0)
		return error(404);

	if (S_ISDIR(info.st_mode)) {
		if (path[path.size() - 1] != '/')
			return redirect(301, path + "/");

		std::string indexPath = fullPath + loc.index;
		if (stat(indexPath.c_str(), &info) < 0) {
			if (loc.autoindex)
				return listDirectory(fullPath, path);
			return error(403);
		}
		fullPath = indexPath;
	}

	if (!S_ISREG(info.st_mode) || access(fullPath.c_str(), R_OK) < 0)
		return error(403);

	std::string body;
	if (!readFile(fullPath, body))
		return error(500);

	HttpResponse response;
	response.setHeader("Content-Type", getContentType(fullPath));
	response.setBody(body);
	return response;
}

HttpResponse RequestHandler::listDirectory(const std::string &fullPath, const std::string &path) const
{
	DIR *dir = opendir(fullPath.c_str());
	if (!dir)
		return error(403);

	std::vector<std::string> names;
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		std::string name = entry->d_name;
		struct stat info;

		if (name == ".")
			continue;
		if (name != ".." && stat((fullPath + name).c_str(), &info) == 0 && S_ISDIR(info.st_mode))
			name += "/";
		names.push_back(name);
	}
	closedir(dir);
	std::sort(names.begin(), names.end());

	std::string title = "Index of " + htmlEscape(path);
	std::string body = "<html><head><title>" + title + "</title></head><body><h1>" + title + "</h1><hr><ul>";
	for (size_t i = 0; i < names.size(); i++) {
		std::string name = names[i] == ".." ? "../" : names[i];
		body += "<li><a href=\"" + htmlEscape(name) + "\">" + htmlEscape(name) + "</a></li>";
	}
	body += "</ul><hr></body></html>";

	HttpResponse response;
	response.setHeader("Content-Type", "text/html");
	response.setBody(body);
	return response;
}

HttpResponse RequestHandler::servePost(const Location &loc, const std::string &path, const HttpRequest &request) const
{
	const std::map<std::string, std::string> &headers = request.getHeaders();
	std::map<std::string, std::string>::const_iterator type = headers.find("content-type");

	if (type != headers.end() && type->second.compare(0, 19, "multipart/form-data") == 0) {
		std::string dir = loc.uploadStore.empty() ? loc.root + path : loc.uploadStore;
		struct stat info;

		if (stat(dir.c_str(), &info) < 0 || !S_ISDIR(info.st_mode))
			return error(404);
		return saveMultipart(dir, request);
	}

	if (path[path.size() - 1] == '/')
		return error(409);

	std::string fullPath;
	if (loc.uploadStore.empty())
		fullPath = loc.root + path;
	else
		fullPath = loc.uploadStore + path.substr(path.rfind('/'));

	int code = saveFile(fullPath, request.getBody());
	if (code != 201)
		return error(code);

	HttpResponse response;
	response.setStatus(201, "Created");
	if (loc.uploadStore.empty())
		response.setHeader("Location", path);
	response.setHeader("Content-Type", "text/html");
	response.setBody("<h1>201 Created</h1>");
	return response;
}

static std::string getBoundary(const std::string &contentType)
{
	size_t pos = contentType.find("boundary=");
	if (pos == std::string::npos)
		return "";

	std::string boundary = contentType.substr(pos + 9);
	size_t end = boundary.find(';');
	if (end != std::string::npos)
		boundary.erase(end);
	if (boundary.size() >= 2 && boundary[0] == '"' && boundary[boundary.size() - 1] == '"')
		boundary = boundary.substr(1, boundary.size() - 2);
	return boundary;
}

// Content-Disposition: form-data; name="file"; filename="cat.png"  ->  "cat.png"
static std::string getFilename(const std::string &partHeaders)
{
	size_t pos = partHeaders.find("filename=\"");
	if (pos == std::string::npos)
		return "";

	pos += 10;
	size_t end = partHeaders.find('"', pos);
	if (end == std::string::npos)
		return "";

	std::string name = partHeaders.substr(pos, end - pos);
	size_t slash = name.find_last_of("/\\");
	if (slash != std::string::npos)
		name = name.substr(slash + 1);
	if (name == "." || name == "..")
		return "";
	return name;
}

// body looks like:
// --BOUNDARY\r\n
// Content-Disposition: form-data; name="file"; filename="a.txt"\r\n
// Content-Type: text/plain\r\n
// \r\n
// <file bytes>\r\n
// --BOUNDARY--\r\n
HttpResponse RequestHandler::saveMultipart(const std::string &dir, const HttpRequest &request) const
{
	const std::string &body = request.getBody();
	std::string boundary = getBoundary(request.getHeaders().find("content-type")->second);
	if (boundary.empty())
		return error(400);

	std::string delimiter = "--" + boundary;
	std::vector<std::string> saved;
	size_t pos = body.find(delimiter);
	if (pos == std::string::npos)
		return error(400);

	while (true) {
		pos += delimiter.size();
		if (body.compare(pos, 2, "--") == 0)
			break;
		if (body.compare(pos, 2, "\r\n") != 0)
			return error(400);
		pos += 2;

		size_t headersEnd = body.find("\r\n\r\n", pos);
		if (headersEnd == std::string::npos)
			return error(400);
		size_t contentStart = headersEnd + 4;
		size_t contentEnd = body.find("\r\n" + delimiter, contentStart);
		if (contentEnd == std::string::npos)
			return error(400);

		std::string filename = getFilename(body.substr(pos, headersEnd - pos));
		if (!filename.empty()) {
			int code = saveFile(dir + "/" + filename, body.substr(contentStart, contentEnd - contentStart));
			if (code != 201)
				return error(code);
			saved.push_back(filename);
		}
		pos = contentEnd + 2;
	}

	if (saved.empty())
		return error(400);

	std::string page = "<h1>201 Created</h1><ul>";
	for (size_t i = 0; i < saved.size(); i++)
		page += "<li>" + htmlEscape(saved[i]) + "</li>";
	page += "</ul>";

	HttpResponse response;
	response.setStatus(201, "Created");
	response.setHeader("Content-Type", "text/html");
	response.setBody(page);
	return response;
}

HttpResponse RequestHandler::serveDelete(const Location &loc, const std::string &path) const
{
	std::string fullPath = loc.root + path;
	struct stat info;

	if (stat(fullPath.c_str(), &info) < 0)
		return error(404);
	if (!S_ISREG(info.st_mode) || access(parentDir(fullPath).c_str(), W_OK) < 0)
		return error(403);
	if (std::remove(fullPath.c_str()) != 0)
		return error(500);

	HttpResponse response;
	response.setStatus(204, "No Content");
	return response;
}
