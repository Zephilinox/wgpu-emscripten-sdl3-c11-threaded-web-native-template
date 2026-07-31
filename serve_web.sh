#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$SCRIPT_DIR"
BIN_DIR="$ROOT_DIR/bin/web"

PORT=${1:-8000}

if [ ! -d "$BIN_DIR" ] || [ ! -f "$BIN_DIR/index.html" ]; then
    echo "Web export directory missing or unbuilt! Running build_web.sh..."
    "$ROOT_DIR/build_web.sh"
fi

echo "=========================================================="
echo " WebGPU + SDL3 WebAssembly Examples Web Server Running"
echo "=========================================================="
echo " Open any of the following URLs in your browser:"
echo ""
echo "   - Suite Landing Page: http://localhost:$PORT/"
echo "   - Example 01:        http://localhost:$PORT/01_triangle.html"
echo "   - Example 02:        http://localhost:$PORT/02_triangle_instancing.html"
echo "   - Example 03:        http://localhost:$PORT/03_bouncing_particles.html"
echo "   - Example 04:        http://localhost:$PORT/04_threaded_triangle.html"
echo "=========================================================="
echo " [Note] COOP/COEP Headers enabled for WebAssembly pthreads."

python3 -c "
import http.server, socketserver, os, sys

class COOPHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        super().end_headers()

os.chdir('$BIN_DIR')
socketserver.TCPServer.allow_reuse_address = True
with socketserver.TCPServer(('', $PORT), COOPHandler) as httpd:
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print('\nServer stopped.')
"
