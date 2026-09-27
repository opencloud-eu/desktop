"""Loopback-only WebDAV fixture for disposable signed FileProvider tests."""
import base64
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Lock
from urllib.parse import parse_qs, quote, unquote, urlsplit
from xml.sax.saxutils import escape
from xml.etree import ElementTree
import hashlib
import json
import time
import re


class Fixture(ThreadingHTTPServer):
    def __init__(self):
        super().__init__(('127.0.0.1', 0), Handler)
        self.files = {}
        self.lock = Lock()
        self.unauthorized = 0
        self.requests = []
        self.identifiers = {}
        self.offline = set()
        self.trash = {}

    def add_domain(self, identifier):
        self.files['/dav/spaces/' + identifier + '/'] = None
        self.files['/dav/spaces/' + identifier + '/hello.txt'] = ('hello ' + identifier).encode()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        if self.command in ("GET", "PUT", "DELETE", "PROPFIND", "MOVE", "MKCOL", "REPORT") and format.startswith('"%s"'):
            self.server.requests.append(f"{self.command} {self.path} {args[1]}")

    def path_and_auth(self):
        path = unquote(urlsplit(self.path).path)
        parts = path.strip('/').split('/')
        domain = parts[3] if parts[:3] == ['dav', 'spaces', 'trash-bin'] else parts[2] if parts[:2] == ['dav', 'spaces'] else ''
        expected = 'Basic ' + base64.b64encode(('review:test-' + domain).encode()).decode()
        if self.headers.get('Authorization') != expected:
            self.server.unauthorized += 1
            self.send_error(401)
            return None
        if domain in self.server.offline and self.command != 'POST':
            self.send_error(503)
            return None
        return path

    def etag(self, path):
        return '"' + hashlib.sha256(self.server.files[path] or b'directory').hexdigest() + '"'

    def send_data(self, status, data=b'', content_type='application/octet-stream'):
        self.send_response(status)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        path = self.path_and_auth()
        if path is None:
            return
        parts = path.strip('/').split('/')
        domain = parts[3] if parts[:3] == ['dav', 'spaces', 'trash-bin'] else parts[2] if parts[:2] == ['dav', 'spaces'] else ''
        payload = json.loads(self.rfile.read(int(self.headers.get('Content-Length', '0'))))
        if payload['offline']:
            self.server.offline.add(domain)
        else:
            self.server.offline.discard(domain)
        self.send_data(200)

    def do_PROPFIND(self):
        path = self.path_and_auth()
        if path is None:
            return
        if path.startswith('/dav/spaces/trash-bin/'):
            self.list_trash(path)
            return
        with self.server.lock:
            if path not in self.server.files and path + '/' in self.server.files:
                path += '/'
            if path not in self.server.files:
                self.send_error(404)
                return
            paths = [path]
            if self.headers.get('Depth') != '0' and path.endswith('/'):
                paths += [key for key in self.server.files if key != path and key.startswith(path)
                          and '/' not in key[len(path):].rstrip('/')]
            responses = []
            for key in paths:
                data = self.server.files[key]
                collection = '<d:collection/>' if data is None else ''
                identifier = self.server.identifiers.setdefault(key, hashlib.sha256(key.encode()).hexdigest())
                responses.append(f'<d:response><d:href>{escape(key)}</d:href><d:propstat><d:prop>'
                    f'<oc:id>{identifier}</oc:id><oc:fileid>{identifier}</oc:fileid><d:resourcetype>{collection}</d:resourcetype>'
                    f'<d:getetag>{escape(self.etag(key))}</d:getetag>'
                    f'<d:getcontentlength>{len(data or b"")}</d:getcontentlength>'
                    '<d:getlastmodified>Sat, 26 Sep 2026 12:00:00 GMT</d:getlastmodified>'
                    '<oc:permissions>RDNVW</oc:permissions></d:prop>'
                    '<d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>')
            xml = '<d:multistatus xmlns:d="DAV:" xmlns:oc="http://owncloud.org/ns">' + ''.join(responses) + '</d:multistatus>'
        self.send_data(207, xml.encode(), 'application/xml')

    def do_GET(self):
        path = self.path_and_auth()
        if path is None:
            return
        with self.server.lock:
            if path not in self.server.files:
                self.send_error(404)
                return
            if self.headers.get('If-Match') not in (None, self.etag(path)):
                self.send_error(412)
                return
            data = self.server.files[path]
        if parse_qs(urlsplit(self.path).query).get('preview') == ['1']:
            png = base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aD1sAAAAASUVORK5CYII=')
            self.send_data(200, png, 'image/png')
            return
        self.send_data(200, data or b'')

    def do_REPORT(self):
        path = unquote(urlsplit(self.path).path).rstrip('/')
        if not path.startswith('/dav/spaces/') or '/' in path[len('/dav/spaces/'):]:
            self.send_error(404)
            return
        try:
            auth = self.headers.get('Authorization', '').removeprefix('Basic ')
            user, password = base64.b64decode(auth, validate=True).decode().split(':', 1)
            domain = password.removeprefix('test-')
            root = '/dav/spaces/' + domain + '/'
            if user != 'review' or not password.startswith('test-') or root not in self.server.files or path != root.rstrip('/'):
                raise ValueError('Unknown fixture account')
        except (ValueError, UnicodeError):
            self.send_error(401)
            return
        if domain in self.server.offline:
            self.send_error(503)
            return
        try:
            document = ElementTree.fromstring(self.rfile.read(int(self.headers.get('Content-Length', '0'))))
            pattern = document.findtext('.//{http://owncloud.org/ns}pattern', '')
            # The real WebDAV handler derives scope from /dav/spaces/<id>.
            # Its query parser does not accept quoted scope directives.
            match = re.fullmatch(r'name:"\*(.*)\*"', pattern)
            if not match or ' AND scope:' in pattern:
                raise ValueError('Wrong fixture search syntax')
            term = match[1].replace('\\"', '"').replace('\\\\', '\\').casefold()
            limit = min(1000, max(1, int(document.findtext('.//{http://owncloud.org/ns}limit', '100'))))
        except (ValueError, ElementTree.ParseError):
            self.send_error(400)
            return
        responses = []
        with self.server.lock:
            for path, data in self.server.files.items():
                if path == root or not path.startswith(root) or term not in path.rstrip('/').rsplit('/', 1)[-1].casefold():
                    continue
                identifier = self.server.identifiers.setdefault(path, hashlib.sha256(path.encode()).hexdigest())
                collection = '<d:collection/>' if data is None else ''
                responses.append(f'<d:response><d:href>{escape(path)}</d:href><d:propstat><d:prop>'
                    f'<oc:id>{identifier}</oc:id><oc:fileid>{identifier}</oc:fileid>'
                    f'<d:resourcetype>{collection}</d:resourcetype><d:getetag>{escape(self.etag(path))}</d:getetag>'
                    f'<d:getcontentlength>{len(data or b"")}</d:getcontentlength><oc:permissions>RDNVW</oc:permissions>'
                    '</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>')
                if len(responses) >= limit:
                    break
        self.send_data(207, ('<d:multistatus xmlns:d="DAV:" xmlns:oc="http://owncloud.org/ns">'
                            + ''.join(responses) + '</d:multistatus>').encode(), 'application/xml')

    def do_PUT(self):
        path = self.path_and_auth()
        if path is None:
            return
        data = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        with self.server.lock:
            exists = path in self.server.files
            if exists and self.headers.get('If-None-Match') == '*':
                self.send_error(412)
                return
            if self.headers.get('If-Match') and (not exists or self.headers['If-Match'] != self.etag(path)):
                self.send_error(412)
                return
            self.server.files[path] = data
        self.send_data(201)

    def list_trash(self, path):
        root = path.rstrip('/')
        with self.server.lock:
            if len(root.split('/')) > 5 and root not in self.server.trash:
                self.send_error(404)
                return
            paths = [key for key in self.server.trash if key.startswith(root + '/')
                     and '/' not in key[len(root) + 1:]]
            responses = [f'<d:response><d:href>{quote(root + "/", safe="/")}</d:href>'
                         '<d:propstat><d:prop><d:resourcetype><d:collection/></d:resourcetype>'
                         '</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>']
            for key in paths:
                entry = self.server.trash[key]
                collection = '<d:collection/>' if entry['data'] is None else ''
                original = entry['original'].split('/', 4)[4].rstrip('/')
                responses.append(f'<d:response><d:href>{quote(key, safe="/")}</d:href><d:propstat><d:prop>'
                    f'<d:resourcetype>{collection}</d:resourcetype><d:getcontentlength>{len(entry["data"] or b"")}</d:getcontentlength>'
                    f'<oc:trashbin-original-location>{escape(original)}</oc:trashbin-original-location>'
                    f'<oc:trashbin-original-filename>{escape(original.split("/")[-1])}</oc:trashbin-original-filename>'
                    f'<oc:trashbin-delete-timestamp>{entry["deleted"]}</oc:trashbin-delete-timestamp>'
                    '</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>')
            xml = '<d:multistatus xmlns:d="DAV:" xmlns:oc="http://owncloud.org/ns">' + ''.join(responses) + '</d:multistatus>'
        self.send_data(207, xml.encode(), 'application/xml')

    def do_DELETE(self):
        path = self.path_and_auth()
        if path is None:
            return
        with self.server.lock:
            if path.startswith('/dav/spaces/trash-bin/'):
                for key in list(self.server.trash):
                    if key == path.rstrip('/') or key.startswith(path.rstrip('/') + '/'):
                        del self.server.trash[key]
                self.send_data(204)
                return
            if path not in self.server.files and path + '/' in self.server.files:
                path += '/'
            if path not in self.server.files:
                self.send_error(404)
                return
            if self.headers.get('If-Match') not in (None, self.etag(path)):
                self.send_error(412)
                return
            identifier = self.server.identifiers.setdefault(path, hashlib.sha256(path.encode()).hexdigest())
            domain = path.strip('/').split('/')[2]
            trash_root = '/dav/spaces/trash-bin/' + domain + '/' + identifier
            for key in list(self.server.files):
                if key == path or key.startswith(path.rstrip('/') + '/'):
                    suffix = key[len(path.rstrip('/')):].strip('/')
                    target = trash_root + ('/' + suffix if suffix else '')
                    self.server.trash[target] = {'original': key, 'data': self.server.files.pop(key),
                        'id': self.server.identifiers.pop(key, hashlib.sha256(key.encode()).hexdigest()), 'deleted': int(time.time())}
        self.send_data(204)

    def do_MKCOL(self):
        path = self.path_and_auth()
        if path is None:
            return
        with self.server.lock:
            if path.rstrip('/') + '/' in self.server.files:
                self.send_error(405)
                return
            self.server.files[path.rstrip('/') + '/'] = None
        self.send_data(201)

    def do_MOVE(self):
        path = self.path_and_auth()
        if path is None:
            return
        destination = unquote(urlsplit(self.headers.get('Destination', '')).path)
        with self.server.lock:
            is_trash = path.startswith('/dav/spaces/trash-bin/')
            if is_trash:
                path = path.rstrip('/')
                if path not in self.server.trash:
                    self.send_error(404)
                    return
                is_directory = self.server.trash[path]['data'] is None
            else:
                if path not in self.server.files and path + '/' in self.server.files:
                    path += '/'
                if path not in self.server.files:
                    self.send_error(404)
                    return
                is_directory = self.server.files[path] is None
            if destination in self.server.files or destination.rstrip('/') + '/' in self.server.files:
                if self.headers.get('Overwrite') == 'F':
                    self.send_error(412)
                    return
            destination = destination.rstrip('/') + ('/' if is_directory else '')
            if is_trash:
                for key in list(self.server.trash):
                    if key == path or key.startswith(path + '/'):
                        entry = self.server.trash.pop(key)
                        target = destination.rstrip('/') + key[len(path):] + ('/' if entry['data'] is None else '')
                        self.server.files[target] = entry['data']
                        self.server.identifiers[target] = entry['id']
            else:
                for key in list(self.server.files):
                    if key == path or key.startswith(path.rstrip('/') + '/'):
                        target = destination.rstrip('/') + key[len(path.rstrip('/')):]
                        self.server.files[target] = self.server.files.pop(key)
                        if key in self.server.identifiers:
                            self.server.identifiers[target] = self.server.identifiers.pop(key)
        self.send_data(201)
