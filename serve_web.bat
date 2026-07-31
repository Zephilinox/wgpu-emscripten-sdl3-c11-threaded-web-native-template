@echo off
setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0
set ROOT_DIR=%SCRIPT_DIR%
set BIN_DIR=%ROOT_DIR%\bin\web

set PORT=%1
if "%PORT%"=="" set PORT=8000

if not exist "%BIN_DIR%\index.html" (
    echo Web export directory missing or unbuilt! Running build_web.bat...
    call "%ROOT_DIR%\build_web.bat"
)

echo ==========================================================
echo  WebGPU + SDL3 WebAssembly Examples Web Server Running
echo ==========================================================
echo  Open any of the following URLs in your browser:
echo.
echo    - Suite Landing Page: http://localhost:%PORT%/
echo    - Example 01:        http://localhost:%PORT%/01_triangle.html
echo    - Example 02:        http://localhost:%PORT%/02_triangle_instancing.html
echo    - Example 03:        http://localhost:%PORT%/03_bouncing_particles.html
echo    - Example 04:        http://localhost:%PORT%/04_threaded_triangle.html
echo ==========================================================
echo  [Note] COOP/COEP Headers enabled for WebAssembly pthreads.

python -c "exec(\"\"\"
import http.server, socketserver, os

class COOPHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        super().end_headers()

os.chdir(r'%BIN_DIR%')
socketserver.TCPServer.allow_reuse_address = True
with socketserver.TCPServer(('', %PORT%), COOPHandler) as httpd:
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print('\nServer stopped.')
\"\"\")"
