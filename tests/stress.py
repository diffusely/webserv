#!/usr/bin/env python3
"""Load test for webserv - a small replacement for siege.

    ./tests/stress.py                          # 50 clients, 10 s, GET /
    ./tests/stress.py -c 200 -t 20 / /cgi.html  # 200 clients, 20 s, two paths
    ./tests/stress.py --abuse                  # same, while attackers hammer the server

Every client keeps one keep-alive connection and sends requests back to back.
Availability = successful requests / all requests. siege's "-b" mode is the same idea.
"""

import argparse
import http.client
import os
import random
import socket
import threading
import time


class Stats:
    def __init__(self):
        self.lock = threading.Lock()
        self.ok = 0
        self.failed = 0
        self.errors = {}
        self.latencies = []
        self.bytes = 0

    def success(self, latency, size):
        with self.lock:
            self.ok += 1
            self.latencies.append(latency)
            self.bytes += size

    def failure(self, reason):
        with self.lock:
            self.failed += 1
            self.errors[reason] = self.errors.get(reason, 0) + 1


def client(host, port, paths, deadline, stats):
    conn = None
    while time.time() < deadline:
        path = random.choice(paths)
        start = time.time()
        try:
            if conn is None:
                conn = http.client.HTTPConnection(host, port, timeout=10)
            conn.request("GET", path)
            response = conn.getresponse()
            body = response.read()
            if response.status >= 500:
                stats.failure("HTTP %d" % response.status)
            else:
                stats.success(time.time() - start, len(body))
            if response.getheader("Connection", "").lower() == "close":
                conn.close()
                conn = None
        except Exception as error:
            stats.failure(type(error).__name__)
            if conn is not None:
                conn.close()
            conn = None
    if conn is not None:
        conn.close()


# ------------------------------------------------------------------ attackers

def slowloris(host, port, count, deadline):
    """Opens many connections, sends half a request and goes silent."""
    sockets = []
    for _ in range(count):
        try:
            s = socket.create_connection((host, port), timeout=5)
            s.send(b"GET / HTTP/1.1\r\nHost: x\r\n")
            sockets.append(s)
        except OSError:
            break
    while time.time() < deadline:
        time.sleep(0.5)
    for s in sockets:
        s.close()
    return len(sockets)


def garbage(host, port, deadline):
    """Random bytes, broken requests, uploads cut in the middle."""
    payloads = [
        lambda: os.urandom(random.randint(1, 5000)),
        lambda: b"GET / HTTP/1.1\r\n" + b"X: " + b"a" * 20000 + b"\r\n\r\n",
        lambda: b"POST /uploads/x HTTP/1.1\r\nHost: x\r\nContent-Length: 1000000\r\n\r\n" + os.urandom(1000),
        lambda: b"POST /uploads/x HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\nfffff\r\n" + os.urandom(100),
        lambda: b"\r\n\r\n\r\n",
        lambda: b"GET /" + b"../" * 200 + b" HTTP/1.1\r\nHost: x\r\n\r\n",
    ]
    while time.time() < deadline:
        try:
            s = socket.create_connection((host, port), timeout=2)
            s.send(random.choice(payloads)())
            if random.random() < 0.5:
                s.settimeout(0.2)
                try:
                    s.recv(100)
                except OSError:
                    pass
            s.close()
        except OSError:
            pass
        time.sleep(0.01)


# ------------------------------------------------------------------ main

def main():
    parser = argparse.ArgumentParser(description="webserv load test")
    parser.add_argument("paths", nargs="*", default=["/"])
    parser.add_argument("-H", "--host", default="127.0.0.1")
    parser.add_argument("-p", "--port", type=int, default=8080)
    parser.add_argument("-c", "--clients", type=int, default=50)
    parser.add_argument("-t", "--time", type=float, default=10)
    parser.add_argument("--abuse", action="store_true", help="run attackers at the same time")
    args = parser.parse_args()

    stats = Stats()
    deadline = time.time() + args.time
    threads = []

    print("%d clients, %.0f s, paths %s%s" % (args.clients, args.time, " ".join(args.paths),
          ", with attackers" if args.abuse else ""))

    if args.abuse:
        held = [0]
        def hold():
            held[0] = slowloris(args.host, args.port, 300, deadline)
        threads.append(threading.Thread(target=hold))
        for _ in range(5):
            threads.append(threading.Thread(target=garbage, args=(args.host, args.port, deadline)))

    for _ in range(args.clients):
        threads.append(threading.Thread(target=client, args=(args.host, args.port, args.paths, deadline, stats)))

    started = time.time()
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    elapsed = time.time() - started

    total = stats.ok + stats.failed
    availability = 100.0 * stats.ok / total if total else 0.0
    latencies = sorted(stats.latencies)

    print()
    print("Transactions:        %8d" % total)
    print("Availability:        %8.2f %%" % availability)
    print("Elapsed time:        %8.2f s" % elapsed)
    print("Transaction rate:    %8.2f req/s" % (total / elapsed))
    print("Data transferred:    %8.2f MB" % (stats.bytes / 1e6))
    if latencies:
        print("Average response:    %8.2f ms" % (1000 * sum(latencies) / len(latencies)))
        print("Slowest (p99):       %8.2f ms" % (1000 * latencies[int(len(latencies) * 0.99) - 1]))
    print("Failed transactions: %8d" % stats.failed)
    for reason, count in sorted(stats.errors.items()):
        print("    %-20s %d" % (reason, count))
    if args.abuse:
        print("Slowloris connections held: %d" % held[0])

    raise SystemExit(0 if stats.failed == 0 else 1)


if __name__ == "__main__":
    main()
