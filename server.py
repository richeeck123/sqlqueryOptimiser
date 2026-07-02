import http.server
import json
import subprocess
import os
import sys

print("--------------------------------------------------", flush=True)
print("Python is successfully reading server.py file!", flush=True)
print("--------------------------------------------------", flush=True)

class DatabaseBridgeHandler(http.server.SimpleHTTPRequestHandler):
    def do_POST(self):
        if self.path == '/optimize':
            content_length = int(self.headers['Content-Length'])
            post_data = self.rfile.read(content_length)
            payload = json.loads(post_data.decode('utf-8'))
            user_sql = payload.get('query', '')

            print(f"\n[BRIDGE] Hit Received! Processing Custom Query...", flush=True)
            sys.stdout.flush()

            exe_path = os.path.join("build", "src", "query_optimizer.exe")
            if not os.path.exists(exe_path):
                exe_path = os.path.join("build", "src", "Release", "query_optimizer.exe")

            # Added a 5-second timeout protection so the server NEVER hangs indefinitely
            try:
                print(f"[BRIDGE] Invoking C++ Subprocess Subcommand...", flush=True)
                result = subprocess.run([exe_path, "--json", user_sql], capture_output=True, text=True, encoding='utf-8', timeout=5)
                response_data = result.stdout if result.stdout.strip() else json.dumps({
                    "initial_cost": 0, "optimized_cost": 0, "performance_boost": "0.00",
                    "initial_plan": f"Engine Error: {result.stderr.strip()}",
                    "rbo_plan": "Pipeline Failed", "cbo_plan": "Pipeline Failed"
                })
            except subprocess.TimeoutExpired:
                print("[ERROR] C++ Engine Deadlocked! Subprocess hit 5-second timeout limit.", flush=True)
                response_data = json.dumps({
                    "initial_cost": 0, "optimized_cost": 0, "performance_boost": "0.00",
                    "initial_plan": "Execution Deadlock: C++ Iterator loop failed to terminate.",
                    "rbo_plan": "Loop Timeout", "cbo_plan": "Loop Timeout"
                })

            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            self.wfile.write(response_data.encode('utf-8'))
        else:
            self.send_error(404)

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'POST, GET, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()

if __name__ == '__main__':
    print("SQL Query Optimiser Engine Bridge Active on http://localhost:8765", flush=True)
    server = http.server.HTTPServer(('localhost', 8765), DatabaseBridgeHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping Bridge Server Safely.", flush=True)