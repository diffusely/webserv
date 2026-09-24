#!/bin/bash
# Automated checks for webserv. Run from the repo root: ./tests/run_tests.sh
# SKIP_SLOW=1 ./tests/run_tests.sh  skips the 30-second timeout test.

cd "$(dirname "$0")/.." || exit 1

GREEN="\033[32m"; RED="\033[31m"; YELLOW="\033[33m"; RESET="\033[0m"
PASS=0; FAIL=0
P1=18080; P2=18081; P3=18082
TMP=$(mktemp -d)
SERVER_PID=""

cleanup() {
	[ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null
	chmod -R u+rwx "$TMP" 2>/dev/null
	rm -rf "$TMP"
}
trap cleanup EXIT

section() { printf "\n${YELLOW}== %s ==${RESET}\n" "$1"; }

# check "description" "expected" "actual"
check() {
	if [ "$2" == "$3" ]; then
		printf "  ${GREEN}OK${RESET}   %s\n" "$1"
		PASS=$((PASS + 1))
	else
		printf "  ${RED}FAIL${RESET} %s  (expected '%s', got '%s')\n" "$1" "$2" "$3"
		FAIL=$((FAIL + 1))
	fi
}

# status code of a curl request: code <curl args...>
code() { curl -s -o /dev/null -w '%{http_code}' --max-time 5 "$@"; }

# sends raw bytes over TCP and prints the first line of the answer: raw <port> <printf-format>
raw() {
	exec 3<>/dev/tcp/127.0.0.1/"$1" || { echo "no connection"; return; }
	printf "$2" >&3
	timeout 3 head -n 1 <&3 | tr -d '\r'
	exec 3<&-
}

server_alive() { kill -0 "$SERVER_PID" 2>/dev/null && echo yes || echo no; }

# ---------------------------------------------------------------- setup

make -s || { echo "build failed"; exit 1; }

SITE="$TMP/site"; OTHER="$TMP/other"; STORE="$TMP/store"
mkdir -p "$SITE/dir" "$SITE/files/sub" "$SITE/noindex" "$SITE/up" "$OTHER/images" "$STORE" "$TMP/vhost"
echo '<h1>home</h1>' > "$SITE/index.html"
echo '<h1>dir index</h1>' > "$SITE/dir/index.html"
echo '<h1>custom 404</h1>' > "$SITE/404.html"
echo 'body {}' > "$SITE/style.css"
echo 'secret' > "$SITE/secret.txt"; chmod 000 "$SITE/secret.txt"
echo 'a' > "$SITE/files/a.txt"
echo 'cat' > "$OTHER/images/cat.png"
echo '<h1>port two</h1>' > "$OTHER/index.html"
echo '<h1>vhost b</h1>' > "$TMP/vhost/index.html"
head -c 5000000 /dev/urandom > "$SITE/big.bin"

cat > "$TMP/test.conf" <<EOF
server {
	listen 127.0.0.1:$P1;
	server_name a.local;
	root $SITE;
	client_max_body_size 100K;
	error_page 404 /404.html;

	location / { methods GET; }
	location /files { methods GET DELETE; autoindex on; }
	location /noindex { methods GET; }
	location /images { root $OTHER; }
	location /old { return 301 http://example.com/new; }
	location /upload { methods GET POST; upload_store $STORE; }
	location /up { methods GET POST DELETE; }
}

server {
	listen $P2;
	root $OTHER;
}

server {
	listen 127.0.0.1:$P1;
	server_name b.local;
	root $TMP/vhost;
	client_max_body_size 10;
	location / { methods GET POST; }
}

server {
	listen $P3;
	root $SITE;
}
EOF

./webserv "$TMP/test.conf" > "$TMP/server.log" 2>&1 &
SERVER_PID=$!
for _ in $(seq 50); do
	curl -s -o /dev/null "http://127.0.0.1:$P1/" && break
	sleep 0.1
done

URL="http://127.0.0.1:$P1"

# ---------------------------------------------------------------- tests

section "GET static files"
check "GET /"                          200 "$(code $URL/)"
check "GET /style.css content-type"    "text/css" "$(curl -s -o /dev/null -w '%{content_type}' $URL/style.css)"
check "GET /style.css?v=2 (query)"     200 "$(code "$URL/style.css?v=2")"
check "GET /dir -> 301"                301 "$(code $URL/dir)"
check "GET /dir/ -> index"             "<h1>dir index</h1>" "$(curl -s $URL/dir/)"
check "GET /nope -> 404"               404 "$(code $URL/nope)"
check "custom 404 page"                "<h1>custom 404</h1>" "$(curl -s $URL/nope)"
check "GET /secret.txt (chmod 000)"    403 "$(code $URL/secret.txt)"
check "GET /../../etc/passwd"          403 "$(code --path-as-is "$URL/../../etc/passwd")"
check "GET /noindex/ no autoindex"     403 "$(code $URL/noindex/)"
curl -s -o "$TMP/big.out" "$URL/big.bin"
check "GET 5MB binary intact"          same "$(cmp -s "$TMP/big.out" "$SITE/big.bin" && echo same || echo different)"

section "locations"
check "/images uses its own root"      200 "$(code $URL/images/cat.png)"
check "/imagesX does not match /images" 404 "$(code $URL/imagesX)"
check "return 301"                     301 "$(code $URL/old/anything)"
check "redirect Location header"       "http://example.com/new" "$(curl -s -o /dev/null -w '%{redirect_url}' $URL/old)"
check "autoindex lists a.txt"          yes "$(curl -s $URL/files/ | grep -q 'a.txt' && echo yes || echo no)"
check "POST on GET-only location"      405 "$(code -X POST -d x $URL/index.html)"
check "405 has Allow header"           "Allow: GET" "$(curl -s -D - -o /dev/null -X POST -d x $URL/ | grep -i '^allow' | tr -d '\r')"
check "unknown method PUT"             405 "$(code -X PUT $URL/files/a.txt)"

section "POST / DELETE"
check "raw POST -> 201"                201 "$(code --data-binary 'hello' $URL/up/raw.txt)"
check "uploaded file content"          hello "$(cat "$SITE/up/raw.txt")"
check "POST into missing dir"          404 "$(code --data-binary x $URL/up/nodir/x.txt)"
head -c 50000 /dev/urandom > "$TMP/f.bin"; echo "text file" > "$TMP/f.txt"
check "multipart upload -> 201"        201 "$(code -F "a=@$TMP/f.txt" -F "b=@$TMP/f.bin" -F "note=hi" $URL/upload)"
check "multipart binary intact"        same "$(cmp -s "$STORE/f.bin" "$TMP/f.bin" && echo same || echo different)"
check "multipart without file"         400 "$(code -F "note=hi" $URL/upload)"
check "DELETE file"                    204 "$(code -X DELETE $URL/up/raw.txt)"
check "DELETE again"                   404 "$(code -X DELETE $URL/up/raw.txt)"
check "DELETE directory"               403 "$(code -X DELETE $URL/files/sub)"
head -c 200000 /dev/urandom > "$TMP/big.post"
check "body over limit -> 413"         413 "$(code --data-binary @"$TMP/big.post" $URL/up/big.post)"
check "413 file not created"           no "$([ -e "$SITE/up/big.post" ] && echo yes || echo no)"

section "chunked"
head -c 30000 /dev/urandom > "$TMP/chunk.bin"
check "chunked POST -> 201"            201 "$(code -H 'Transfer-Encoding: chunked' --data-binary @"$TMP/chunk.bin" $URL/up/chunk.bin)"
check "chunked body intact"            same "$(cmp -s "$SITE/up/chunk.bin" "$TMP/chunk.bin" && echo same || echo different)"
check "chunked over limit -> 413"      413 "$(code -H 'Transfer-Encoding: chunked' --data-binary @"$TMP/big.post" $URL/up/x)"
check "manual chunks"                  "HTTP/1.1 201 Created" "$(raw $P1 'POST /up/m.txt HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n')"
check "manual chunks content"          "hello world" "$(cat "$SITE/up/m.txt")"
check "bad chunk size"                 "HTTP/1.1 400 Bad Request" "$(raw $P1 'POST /up/m.txt HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n')"

section "malformed requests"
check "garbage"                        "HTTP/1.1 400 Bad Request" "$(raw $P1 'GARBAGE\r\n\r\n')"
check "lowercase method"               "HTTP/1.1 400 Bad Request" "$(raw $P1 'get / HTTP/1.1\r\nHost: x\r\n\r\n')"
check "HTTP/2.0"                       "HTTP/1.1 505 HTTP Version Not Supported" "$(raw $P1 'GET / HTTP/2.0\r\nHost: x\r\n\r\n')"
check "HTTP/1.1 without Host"          "HTTP/1.1 400 Bad Request" "$(raw $P1 'GET / HTTP/1.1\r\n\r\n')"
check "HTTP/1.0 without Host is fine"  "HTTP/1.1 200 OK" "$(raw $P1 'GET / HTTP/1.0\r\n\r\n')"
check "header without colon"           "HTTP/1.1 400 Bad Request" "$(raw $P1 'GET / HTTP/1.1\r\nHost: x\r\nbroken\r\n\r\n')"
check "Content-Length: abc"            "HTTP/1.1 400 Bad Request" "$(raw $P1 'POST /up/x HTTP/1.1\r\nHost: x\r\nContent-Length: abc\r\n\r\n')"
check "Content-Length + chunked"       "HTTP/1.1 400 Bad Request" "$(raw $P1 'POST /up/x HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\nTransfer-Encoding: chunked\r\n\r\n')"
check "Transfer-Encoding: gzip"        "HTTP/1.1 501 Not Implemented" "$(raw $P1 'POST /up/x HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: gzip\r\n\r\n')"
LONG=$(head -c 10000 /dev/zero | tr '\0' 'a')
check "URI too long"                   414 "$(code "$URL/$LONG")"
check "header too big"                 431 "$(code -H "X-Big: $LONG" $URL/)"
check "bare \\n line endings (nc)"     "HTTP/1.1 200 OK" "$(raw $P1 'GET / HTTP/1.1\nHost: x\n\n')"

section "connections"
check "keep-alive reuses connection"   yes "$(curl -sv $URL/ $URL/style.css 2>&1 | grep -qi 're-using\|reusing' && echo yes || echo no)"
check "two pipelined requests"         2 "$(printf 'GET / HTTP/1.1\r\nHost: x\r\n\r\nGET /style.css HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n' | timeout 3 nc 127.0.0.1 $P1 | grep -c '^HTTP/1.1 200')"
check "Connection: close header"       "Connection: close" "$(curl -s -D - -o /dev/null -H 'Connection: close' $URL/ | grep -i '^connection' | tr -d '\r')"

section "multiple servers"
check "second port, other root"        "<h1>port two</h1>" "$(curl -s http://127.0.0.1:$P2/)"
check "third port"                     200 "$(code http://127.0.0.1:$P3/)"
check "Host: a.local"                  "<h1>home</h1>" "$(curl -s -H 'Host: a.local' $URL/)"
check "Host: b.local (same port)"      "<h1>vhost b</h1>" "$(curl -s -H 'Host: b.local' $URL/)"
check "unknown Host -> first server"   "<h1>home</h1>" "$(curl -s -H 'Host: zzz' $URL/)"
check "b.local own body limit (10)"    413 "$(code -H 'Host: b.local' --data-binary 'more than ten bytes' $URL/x)"

section "robustness"
for _ in $(seq 20); do
	exec 3<>/dev/tcp/127.0.0.1/$P1
	printf 'GET /big.bin HTTP/1.1\r\nHost: x\r\n\r\n' >&3
	exec 3<&-
done
sleep 0.5
check "20 clients drop mid-download (SIGPIPE)" yes "$(server_alive)"
for _ in $(seq 10); do
	head -c 3000 /dev/urandom > "$TMP/junk"
	timeout 1 bash -c "cat '$TMP/junk' > /dev/tcp/127.0.0.1/$P1" 2>/dev/null
done
check "random binary junk"             yes "$(server_alive)"
PIDS=""
for i in $(seq 100); do
	code $URL/ > "$TMP/c.$i" &
	PIDS="$PIDS $!"
done
wait $PIDS
OK=$(cat "$TMP"/c.* | grep -o 200 | wc -l | tr -d ' ')
check "100 parallel requests all 200"  100 "$OK"
if [ -d /proc/$SERVER_PID/fd ]; then
	sleep 0.5
	check "no leaked fds (3 listen + 3 std)" 6 "$(ls /proc/$SERVER_PID/fd | wc -l)"
fi

if [ "$SKIP_SLOW" != "1" ]; then
	section "timeouts (waits 31s)"
	exec 4<>/dev/tcp/127.0.0.1/$P1
	printf 'GET / HTTP/1.1\r\nHost: x\r\n' >&4
	exec 5<>/dev/tcp/127.0.0.1/$P1
	sleep 31
	check "stuck mid-request -> 408"   "HTTP/1.1 408 Request Timeout" "$(timeout 2 head -n 1 <&4 | tr -d '\r')"
	check "idle connection closed"     "" "$(timeout 2 cat <&5)"
	exec 4<&- 5<&-
fi

section "config errors"
bad() { printf "$2" > "$TMP/bad.conf"; check "$1" 1 "$(timeout 2 ./webserv "$TMP/bad.conf" > /dev/null 2>&1; echo $?)"; }
bad "missing file"          ""
bad "port with letters"     'server { listen 80a; root .; }'
bad "port too big"          'server { listen 70000; root .; }'
bad "missing ;"             'server { listen 8080 root .; }'
bad "no listen"             'server { root .; }'
bad "unclosed block"        'server { listen 8080; root .;'
bad "unknown directive"     'server { listen 8080; root .; foo bar; }'
bad "duplicate location"    'server { listen 8080; root .; location /a { } location /a { } }'
bad "bad method"            'server { listen 8080; root .; location /a { methods PUT; } }'
bad "bad redirect code"     'server { listen 8080; root .; location /a { return 305 x; } }'
check "two arguments"       1 "$(./webserv a b > /dev/null 2>&1; echo $?)"

check "server still alive at the end" yes "$(server_alive)"

# ---------------------------------------------------------------- summary

printf "\n${GREEN}%d passed${RESET}, " "$PASS"
if [ "$FAIL" -eq 0 ]; then
	printf "0 failed\n"
else
	printf "${RED}%d failed${RESET}  (server log: kept at $TMP/server.log)\n" "$FAIL"
	trap - EXIT
	kill "$SERVER_PID" 2>/dev/null
fi
[ "$FAIL" -eq 0 ]
