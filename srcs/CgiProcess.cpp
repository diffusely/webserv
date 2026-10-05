#include "CgiProcess.hpp"
#include <sstream>
#include <cstdlib>
#include <cctype>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>

CgiProcess::CgiProcess()
	: _pid(-1), _startTime(0), _inputFd(-1), _outputFd(-1), _inputSent(0)
{
}

//  server                         child (the script)
//  _inputFd  --- toChild pipe -->  stdin
//  _outputFd <-- fromChild pipe -- stdout
bool CgiProcess::start(const CgiRequest &cgi, const HttpRequest &request,
	int serverPort, const std::string &remoteAddr, const std::vector<int> &fdsToClose)
{
	int toChild[2];
	int fromChild[2];

	if (pipe(toChild) < 0)
		return false;
	if (pipe(fromChild) < 0) {
		close(toChild[0]);
		close(toChild[1]);
		return false;
	}

	// built before fork(): the child only has to execve()
	std::vector<std::string> envStrings = buildEnv(cgi, request, serverPort, remoteAddr);
	std::vector<char *> envp;
	for (size_t i = 0; i < envStrings.size(); i++)
		envp.push_back(const_cast<char *>(envStrings[i].c_str()));
	envp.push_back(NULL);

	char *argv[3];
	argv[0] = const_cast<char *>(cgi.interpreter.c_str());
	argv[1] = const_cast<char *>(cgi.scriptFile.c_str());
	argv[2] = NULL;

	_pid = fork();
	if (_pid < 0) {
		close(toChild[0]);
		close(toChild[1]);
		close(fromChild[0]);
		close(fromChild[1]);
		return false;
	}

	if (_pid == 0) {
		dup2(toChild[0], STDIN_FILENO);
		dup2(fromChild[1], STDOUT_FILENO);
		close(toChild[0]);
		close(toChild[1]);
		close(fromChild[0]);
		close(fromChild[1]);
		// the script must not keep our sockets open, or clients would never see their connection close
		for (size_t i = 0; i < fdsToClose.size(); i++)
			close(fdsToClose[i]);

		if (chdir(cgi.scriptDir.c_str()) == 0)
			execve(argv[0], argv, &envp[0]);
		std::exit(1);
	}

	close(toChild[0]);
	close(fromChild[1]);
	_inputFd = toChild[1];
	_outputFd = fromChild[0];
	_startTime = std::time(NULL);
	_input = request.getBody();
	_inputSent = 0;
	_output.clear();

	fcntl(_inputFd, F_SETFL, O_NONBLOCK);
	fcntl(_outputFd, F_SETFL, O_NONBLOCK);

	// nothing to send: closing stdin right away lets the script see EOF
	if (_input.empty())
		closeInput();
	return true;
}

std::vector<std::string> CgiProcess::buildEnv(const CgiRequest &cgi, const HttpRequest &request,
	int serverPort, const std::string &remoteAddr) const
{
	std::vector<std::string> env;
	std::ostringstream port;
	std::ostringstream length;

	port << serverPort;
	length << request.getBody().size();

	std::string host = request.getHeader("host");
	std::string serverName = host.substr(0, host.find(':'));
	if (serverName.empty())
		serverName = "localhost";

	env.push_back("GATEWAY_INTERFACE=CGI/1.1");
	env.push_back("SERVER_SOFTWARE=webserv/1.0");
	env.push_back("SERVER_PROTOCOL=" + request.getVersion());
	env.push_back("SERVER_NAME=" + serverName);
	env.push_back("SERVER_PORT=" + port.str());
	env.push_back("REQUEST_METHOD=" + request.getMethod());
	env.push_back("REQUEST_URI=" + request.getPath());
	env.push_back("SCRIPT_NAME=" + cgi.scriptName);
	env.push_back("SCRIPT_FILENAME=" + cgi.scriptFile);
	env.push_back("PATH_INFO=" + cgi.pathInfo);
	env.push_back("QUERY_STRING=" + cgi.query);
	env.push_back("REMOTE_ADDR=" + remoteAddr);
	env.push_back("REDIRECT_STATUS=200");
	if (request.getMethod() == "POST" || !request.getBody().empty())
		env.push_back("CONTENT_LENGTH=" + length.str());
	if (!request.getHeader("content-type").empty())
		env.push_back("CONTENT_TYPE=" + request.getHeader("content-type"));

	// every other request header goes in as HTTP_NAME: "User-Agent" -> HTTP_USER_AGENT
	const std::map<std::string, std::string> &headers = request.getHeaders();
	for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it) {
		if (it->first == "content-type" || it->first == "content-length")
			continue;
		std::string name = "HTTP_";
		for (size_t i = 0; i < it->first.size(); i++)
			name += it->first[i] == '-' ? '_' : std::toupper(it->first[i]);
		env.push_back(name + "=" + it->second);
	}
	return env;
}

// called only when poll() said _inputFd is writable; returns false on error
bool CgiProcess::writeInput()
{
	ssize_t written = write(_inputFd, _input.c_str() + _inputSent, _input.size() - _inputSent);

	if (written < 0)
		return false;
	_inputSent += written;
	if (_inputSent == _input.size())
		closeInput();
	return true;
}

// called only when poll() said _outputFd is readable; 0 means the script closed stdout
ssize_t CgiProcess::readOutput()
{
	char buffer[8192];
	ssize_t bytesRead = read(_outputFd, buffer, sizeof(buffer));

	if (bytesRead > 0)
		_output.append(buffer, bytesRead);
	return bytesRead;
}

void CgiProcess::closeInput()
{
	if (_inputFd >= 0)
		close(_inputFd);
	_inputFd = -1;
}

void CgiProcess::closeOutput()
{
	if (_outputFd >= 0)
		close(_outputFd);
	_outputFd = -1;
}

void CgiProcess::kill()
{
	if (_pid > 0)
		::kill(_pid, SIGKILL);
}

// script output:  "Status: 404 Not Found\r\nContent-Type: text/html\r\n\r\n<body>"
// Status is optional (200 by default), Location without Status means 302
bool CgiProcess::buildResponse(HttpResponse &response) const
{
	size_t crlf = _output.find("\r\n\r\n");
	size_t lf = _output.find("\n\n");
	size_t end;
	size_t bodyStart;

	if (crlf != std::string::npos && (lf == std::string::npos || crlf < lf)) {
		end = crlf;
		bodyStart = crlf + 4;
	} else if (lf != std::string::npos) {
		end = lf;
		bodyStart = lf + 2;
	} else {
		return false;
	}

	std::istringstream headers(_output.substr(0, end));
	std::string line;
	bool hasStatus = false;
	bool hasContent = false;

	while (std::getline(headers, line)) {
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);

		size_t colon = line.find(':');
		if (colon == std::string::npos || colon == 0)
			return false;

		std::string name = line.substr(0, colon);
		std::string value = line.substr(colon + 1);
		while (!value.empty() && value[0] == ' ')
			value.erase(0, 1);

		std::string lower = name;
		for (size_t i = 0; i < lower.size(); i++)
			lower[i] = std::tolower(lower[i]);

		if (lower == "status") {
			if (value.size() < 3 || !std::isdigit(value[0]) || !std::isdigit(value[1]) || !std::isdigit(value[2]))
				return false;
			int code = (value[0] - '0') * 100 + (value[1] - '0') * 10 + (value[2] - '0');
			std::string reason = value.size() > 4 ? value.substr(4) : "";
			response.setStatus(code, reason);
			hasStatus = true;
		} else if (lower == "location") {
			response.setHeader("Location", value);
			if (!hasStatus)
				response.setStatus(302, "Found");
			hasContent = true;
		} else if (lower != "content-length") {
			response.setHeader(name, value);
			if (lower == "content-type")
				hasContent = true;
		}
	}

	if (!hasContent && !hasStatus)
		return false;
	response.setBody(_output.substr(bodyStart));
	return true;
}

int CgiProcess::getInputFd() const
{
	return _inputFd;
}

int CgiProcess::getOutputFd() const
{
	return _outputFd;
}

pid_t CgiProcess::getPid() const
{
	return _pid;
}

time_t CgiProcess::getStartTime() const
{
	return _startTime;
}
