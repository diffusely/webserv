#!/usr/bin/python3
# GET /cgi-bin/hello.py?name=you  - shows what the server put in the environment
import os
from urllib.parse import parse_qs

query = parse_qs(os.environ.get("QUERY_STRING", ""))
name = query.get("name", ["stranger"])[0]

print("Content-Type: text/html")
print()
print("<h1>Hello, %s!</h1>" % name)
print("<h2>CGI environment</h2><ul>")
for key in sorted(os.environ):
    if key.startswith(("REQUEST_", "SCRIPT_", "PATH_INFO", "QUERY_", "SERVER_", "CONTENT_", "HTTP_", "REMOTE_", "GATEWAY")):
        print("<li><b>%s</b> = %s</li>" % (key, os.environ[key]))
print("</ul><p><a href=\"/cgi.html\">back</a></p>")
