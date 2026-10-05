#pragma once

#include <string>
#include <vector>
#include <ctime>
#include <sys/types.h>
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"

// what RequestHandler found out about the script: /cgi-bin/test.py/extra?x=1
struct CgiRequest
{
	std::string interpreter;	// /usr/bin/python3
	std::string scriptDir;		// ./www/cgi-bin - we chdir() here
	std::string scriptFile;		// test.py
	std::string scriptName;		// /cgi-bin/test.py
	std::string pathInfo;		// /extra
	std::string query;			// x=1
};

class CgiProcess
{
public:
	CgiProcess();

	bool start(const CgiRequest &cgi, const HttpRequest &request,
		int serverPort, const std::string &remoteAddr, const std::vector<int> &fdsToClose);

	int getInputFd() const;
	int getOutputFd() const;
	pid_t getPid() const;
	time_t getStartTime() const;

	bool writeInput();
	ssize_t readOutput();
	void closeInput();
	void closeOutput();
	void kill();

	bool buildResponse(HttpResponse &response) const;

private:
	pid_t _pid;
	time_t _startTime;
	int _inputFd;
	int _outputFd;
	std::string _input;
	size_t _inputSent;
	std::string _output;

	std::vector<std::string> buildEnv(const CgiRequest &cgi, const HttpRequest &request,
		int serverPort, const std::string &remoteAddr) const;
};
