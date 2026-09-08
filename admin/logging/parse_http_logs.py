# SPDX-License-Identifier: GPL-3.0-or-later
# Klaas Freitag <k.freitag@opencloud.eu>, Copilot assisted

import html
import re
import json
import os
import sys
import time
from urllib.parse import urlparse

from mitmproxy import connection
from mitmproxy.http import Request, Response, HTTPFlow
from mitmproxy.io import FlowWriter

# Matches lines from the OpenCloud client logging in format like:
# 26-09-07 16:06:16:190 [ info sync.httplogger ]:    REQUEST <id> {json...}
LOG_LINE_RE = re.compile(
    r'^(?P<ts>\d{2}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}:\d{3})\s+'
    r'\[ info sync\.httplogger \]:\s+(?P<type>REQUEST|RESPONSE)\s+'
    r'(?P<id>[a-zA-Z0-9-]+)\s+(?P<json>.*)$'
)

TS_FORMAT = "%y-%m-%d %H:%M:%S"


def parse_timestamp(ts_str):
    """Convert the client's 'YY-MM-DD HH:MM:SS:mmm' timestamp into epoch seconds."""
    date_part, _, ms_part = ts_str.rpartition(':')
    try:
        epoch = time.mktime(time.strptime(date_part, TS_FORMAT))
        return epoch + int(ms_part) / 1000.0
    except ValueError:
        return None


def read_lines(file_path):
    """Read the log file, tolerating both UTF-8 and UTF-16 encoded files."""
    for encoding in ('utf-8', 'utf-16'):
        try:
            with open(file_path, 'r', encoding=encoding) as f:
                return f.readlines()
        except (UnicodeError, UnicodeDecodeError):
            continue
    print(f"Could not decode {file_path} as utf-8 or utf-16.")
    sys.exit(1)


def parse_log(file_path):
    data = {}
    try:
        lines = read_lines(file_path)
    except FileNotFoundError:
        print(f"File {file_path} not found.")
        sys.exit(1)

    for line in lines:
        if 'sync.httplogger' not in line:
            continue
        match = LOG_LINE_RE.search(line)
        if not match:
            continue
        log_type = match.group('type')
        log_id = match.group('id')
        json_str = match.group('json')
        timestamp = parse_timestamp(match.group('ts'))
        try:
            payload = json.loads(json_str)
        except json.JSONDecodeError as e:
            print(f"Error decoding JSON for {log_id}: {e}")
            continue

        entry = data.setdefault(log_id, {'request': None, 'response': None})
        if log_type == 'REQUEST':
            entry['request'] = extract_request(payload, timestamp)
        else:
            entry['response'] = extract_response(payload, timestamp)
    return data


def extract_request(payload, timestamp):
    req = payload.get('request', {})
    info = req.get('info', {})
    body = req.get('body', {})
    return {
        'method': info.get('method', 'GET'),
        'url': info.get('url', ''),
        'headers': req.get('header', {}) or {},
        'content': (body.get('data') or ''),
        'timestamp': timestamp,
    }


def extract_response(payload, timestamp):
    resp = payload.get('response', {})
    info = resp.get('info', {})
    reply = info.get('reply', {})
    body = resp.get('body', {})
    version = reply.get('version', 'HTTP/1.1')
    if version and not version.startswith('HTTP/'):
        # e.g. "HTTP 2" -> "HTTP/2.0"
        version = 'HTTP/' + version.split(' ')[-1] + ('.0' if '.' not in version else '')
    return {
        'status_code': reply.get('status', 200),
        'headers': resp.get('header', {}) or {},
        'content': (body.get('data') or ''),
        'http_version': version,
        'timestamp': timestamp,
    }


def make_flow(log_id, request, response):
    url = request['url']
    parsed = urlparse(url)
    host = parsed.hostname or 'localhost'
    port = parsed.port or (443 if parsed.scheme == 'https' else 80)

    client_conn = connection.Client(
        peername=('127.0.0.1', 0),
        sockname=('127.0.0.1', 0),
        timestamp_start=request['timestamp'],
    )
    server_conn = connection.Server(address=(host, port))
    server_conn.timestamp_start = request['timestamp']

    flow = HTTPFlow(client_conn, server_conn)
    flow.request = Request.make(
        request['method'],
        url,
        content=request['content'],
        headers=request['headers'],
    )
    flow.request.timestamp_start = request['timestamp']
    flow.request.timestamp_end = request['timestamp']

    if response:
        flow.response = Response.make(
            response['status_code'],
            content=response['content'],
            headers=response['headers'],
        )
        flow.response.http_version = response['http_version']
        flow.response.timestamp_start = response['timestamp']
        flow.response.timestamp_end = response['timestamp']

    return flow


def generate_html(data, output_file):
    try:
        with open(output_file, 'w') as f:
            f.write("<!DOCTYPE html><html><head><style>body { font-family: sans-serif; } pre { background: #eee; padding: 10px; white-space: pre-wrap; word-break: break-all; }</style></head><body><h1>HTTP Log</h1>")
            for log_id, entry in data.items():
                f.write(f"<h2>ID: {html.escape(log_id)}</h2>")
                request = entry['request']
                response = entry['response']
                if request:
                    f.write("<h3>Request</h3><pre>")
                    f.write(html.escape(f"{request['method']} {request['url']}\n"))
                    for key, value in request['headers'].items():
                        f.write(html.escape(f"{key}: {value}\n"))
                    if request['content']:
                        f.write(html.escape(f"\n{request['content']}"))
                    f.write("</pre>")
                if response:
                    f.write("<h3>Response</h3><pre>")
                    f.write(html.escape(f"{response['http_version']} {response['status_code']}\n"))
                    for key, value in response['headers'].items():
                        f.write(html.escape(f"{key}: {value}\n"))
                    if response['content']:
                        f.write(html.escape(f"\n{response['content']}"))
                    f.write("</pre>")
                f.write("<hr>")
            f.write("</body></html>")
    except IOError as e:
        print(f"Error writing to file {output_file}: {e}")


def generate_mitm_flows(data, output_file):
    try:
        with open(output_file, 'wb') as f:
            writer = FlowWriter(f)
            count = 0
            for log_id, entry in data.items():
                if not entry['request']:
                    # No request captured for this id, skip (can't build a flow without one).
                    continue
                flow = make_flow(log_id, entry['request'], entry['response'])
                writer.add(flow)
                count += 1
        print(f"Wrote {count} flow(s) to {output_file}")
    except IOError as e:
        print(f"Error writing to file {output_file}: {e}")
        sys.exit(1)


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("This script parses the http logs of an OpenCloud client and")
        print("generates human useable output for debugging.")
        print("")
        print("Usage: python parse_http_logs.py <input_file> <output_file>")
        print("       output_file extension decides the format:")
        print("         .mitm -> mitmproxy flow file, anything else -> HTML")
        sys.exit(1)
    output_file = sys.argv[2]
    data = parse_log(sys.argv[1])
    if os.path.splitext(output_file)[1].lower() == '.mitm':
        generate_mitm_flows(data, output_file)
    else:
        generate_html(data, output_file)
