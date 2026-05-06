# Special serve method to allow atomics and threads in a web browser.
from http.server import SimpleHTTPRequestHandler, HTTPServer

class CORSRequestHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        super().end_headers()

port = 8000
httpd = HTTPServer(('localhost', port), CORSRequestHandler)
print(f"Serving on http://localhost:{port}")
httpd.serve_forever()
