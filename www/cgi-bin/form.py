#!/usr/bin/python3
# POST /cgi-bin/form.py  - reads the body from stdin and echoes the fields back
import os
import sys
from urllib.parse import parse_qs

length = int(os.environ.get("CONTENT_LENGTH", "0") or 0)
body = sys.stdin.read(length) if length > 0 else ""
fields = parse_qs(body)

print("Content-Type: text/html")
print()
print("<h1>Got a %s with %d bytes</h1><ul>" % (os.environ["REQUEST_METHOD"], length))
for key, values in fields.items():
    print("<li><b>%s</b> = %s</li>" % (key, ", ".join(values)))
print("</ul><p><a href=\"/cgi.html\">back</a></p>")
