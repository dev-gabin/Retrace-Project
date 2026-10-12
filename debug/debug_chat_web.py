#!/usr/bin/env python3
"""Local web UI that bridges a browser to the existing Retrace TCP server."""

from collections import deque
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import socket
import threading
import urllib.parse


PAGE = r"""<!doctype html>
<html lang="ko">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Retrace CHAT Debug</title>
  <style>
    :root { color-scheme: dark; font: 15px/1.5 system-ui, sans-serif; }
    body { margin: 0; background: #10151d; color: #e8edf4; }
    main { max-width: 850px; margin: 32px auto; padding: 0 18px; }
    h1 { margin-bottom: 4px; font-size: 1.6rem; }
    .muted { color: #9aa8b8; }
    .card { background: #19222e; border: 1px solid #2c3949; border-radius: 12px; padding: 18px; margin: 16px 0; }
    .row { display: flex; flex-wrap: wrap; gap: 10px; align-items: end; }
    label { display: grid; gap: 5px; color: #b8c4d3; flex: 1 1 130px; }
    input, button { box-sizing: border-box; border-radius: 7px; border: 1px solid #3b4b60; padding: 10px 12px; background: #101720; color: inherit; font: inherit; }
    button { cursor: pointer; background: #2367d1; border-color: #3679e6; font-weight: 600; }
    button.secondary { background: #354252; border-color: #4a5b70; }
    button:disabled, input:disabled { opacity: .55; cursor: not-allowed; }
    #status { margin-left: 8px; font-weight: 600; }
    #log { height: min(48vh, 430px); overflow: auto; display: flex; flex-direction: column; gap: 7px; }
    .entry { white-space: pre-wrap; overflow-wrap: anywhere; padding: 8px 10px; border-radius: 7px; background: #111923; }
    .entry.sent { border-left: 3px solid #4e9cff; }
    .entry.received { border-left: 3px solid #55c99a; }
    .entry.error { border-left: 3px solid #ed7373; }
    #message { flex: 1 1 75%; }
    .notice { font-size: .92rem; }
  </style>
</head>
<body>
<main>
  <h1>Retrace CHAT 디버그</h1>
  <div class="muted">Windows 브리지를 통해 Jetson TCP 서버에 연결합니다. 명령만 입력하면 전송용 [SERVER] 경로를 붙여 보냅니다.</div>
  <section class="card">
    <form id="connect-form" class="row">
      <label>서버 주소<input id="host" value="10.10.16.116" required></label>
      <label>포트<input id="port" type="number" value="5000" min="1" max="65535" required></label>
      <label>Client ID<input id="client-id" autocomplete="username" required></label>
      <label>비밀번호<input id="password" type="password" autocomplete="current-password" required></label>
      <button id="connect-button">연결</button>
      <button id="disconnect-button" class="secondary" type="button" disabled>연결 해제</button>
    </form>
    <p>상태: <span id="status">연결 안 됨</span></p>
  </section>
  <section class="card">
    <div id="log" aria-live="polite"></div>
  </section>
  <section class="card">
    <form id="send-form" class="row">
      <label id="message-label">명령 또는 텍스트<input id="message" maxlength="2000" placeholder="예: LIST 또는 일반 텍스트" disabled></label>
      <button id="send-button" disabled>서버로 보내기</button>
    </form>
    <p class="muted notice">명령 앞에 [SERVER]를 직접 붙이지 마세요. 브리지가 전송 프레임을 붙여 보내며, 서버의 응답과 오류도 아래 로그에 표시됩니다.</p>
  </section>
</main>
<script>
const statusEl = document.querySelector('#status');
const logEl = document.querySelector('#log');
const connectButton = document.querySelector('#connect-button');
const disconnectButton = document.querySelector('#disconnect-button');
const messageInput = document.querySelector('#message');
const sendButton = document.querySelector('#send-button');
let lastSeq = 0;
let connected = false;

function addEntry(item) {
  const div = document.createElement('div');
  div.className = 'entry ' + item.kind;
  div.textContent = `${item.time}  ${item.text}`;
  logEl.append(div);
  logEl.scrollTop = logEl.scrollHeight;
}
async function api(path, body) {
  const options = body === undefined ? {} : {
    method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(body)
  };
  const response = await fetch(path, options);
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || response.statusText);
  return data;
}
document.querySelector('#connect-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  connectButton.disabled = true;
  try {
    const data = await api('/api/connect', {
      host: document.querySelector('#host').value.trim(),
      port: Number(document.querySelector('#port').value),
      client_id: document.querySelector('#client-id').value.trim(),
      password: document.querySelector('#password').value
    });
    connected = data.connected;
    statusEl.textContent = 'TCP 연결됨 (로그인 응답 대기)';
    disconnectButton.disabled = false;
    messageInput.disabled = false;
    sendButton.disabled = false;
    document.querySelector('#password').value = '';
    messageInput.focus();
  } catch (error) {
    statusEl.textContent = '연결 실패: ' + error.message;
  } finally {
    connectButton.disabled = false;
  }
});
document.querySelector('#disconnect-button').addEventListener('click', async () => {
  try { await api('/api/disconnect', {}); } catch (error) { statusEl.textContent = error.message; }
});
document.querySelector('#send-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  const message = messageInput.value;
  if (!message.trim()) return;
  sendButton.disabled = true;
  try {
    await api('/api/send', {message});
    messageInput.value = '';
  } catch (error) {
    statusEl.textContent = '전송 실패: ' + error.message;
  } finally {
    sendButton.disabled = !connected;
    messageInput.focus();
  }
});
async function poll() {
  try {
    const state = await api('/api/state?after=' + lastSeq);
    connected = state.connected;
    if (!connected) {
      statusEl.textContent = state.status || '연결 안 됨';
      disconnectButton.disabled = true;
      messageInput.disabled = true;
      sendButton.disabled = true;
    }
    for (const item of state.events) {
      addEntry(item);
      lastSeq = Math.max(lastSeq, item.seq);
    }
  } catch (error) {
    statusEl.textContent = '브리지 연결 오류: ' + error.message;
  }
}
setInterval(poll, 500);
poll();
</script>
</body>
</html>
"""


class Bridge:
    def __init__(self):
        self.lock = threading.RLock()
        self.send_lock = threading.Lock()
        self.sock = None
        self.connected = False
        self.status = "연결 안 됨"
        self.events = deque(maxlen=500)
        self.seq = 0

    def add_event(self, kind, text):
        with self.lock:
            self.seq += 1
            self.events.append({
                "seq": self.seq,
                "kind": kind,
                "time": datetime.now().strftime("%H:%M:%S"),
                "text": text,
            })

    def connect(self, host, port, client_id, password):
        if not host or not 1 <= port <= 65535:
            raise ValueError("서버 주소 또는 포트가 올바르지 않습니다.")
        if not client_id or any(ch in client_id for ch in "]: \r\n"):
            raise ValueError("Client ID에 빈 값, 공백, 콜론, ] 는 사용할 수 없습니다.")
        if not password or any(ch in password for ch in "]\r\n"):
            raise ValueError("비밀번호는 비어 있을 수 없고 ] 또는 줄바꿈을 포함할 수 없습니다.")
        with self.lock:
            if self.connected:
                raise RuntimeError("이미 서버에 연결되어 있습니다.")
        sock = socket.create_connection((host, port), timeout=8)
        try:
            sock.sendall(f"[{client_id}:{password}]".encode("utf-8"))
            sock.settimeout(None)
        except Exception:
            sock.close()
            raise
        with self.lock:
            self.sock = sock
            self.connected = True
            self.status = "TCP 연결됨 (로그인 응답 대기)"
        self.add_event("sent", f"{host}:{port} 에 연결 요청 ({client_id})")
        threading.Thread(target=self.receive_loop, args=(sock,), daemon=True).start()

    def receive_loop(self, sock):
        pending = b""
        try:
            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                pending += chunk
                while b"\n" in pending:
                    line, pending = pending.split(b"\n", 1)
                    text = line.rstrip(b"\r").decode("utf-8", errors="replace")
                    self.add_event("received", text)
        except OSError as exc:
            with self.lock:
                if self.sock is sock:
                    self.status = f"수신 오류: {exc}"
        finally:
            sock.close()
            with self.lock:
                if self.sock is sock:
                    self.sock = None
                    self.connected = False
                    if self.status.startswith("수신 오류"):
                        pass
                    else:
                        self.status = "서버 연결 종료"
            self.add_event("error", "서버 연결이 종료되었습니다.")

    def send_line(self, message):
        if not message or not message.strip():
            raise ValueError("메시지를 입력해 주세요.")
        if len(message.encode("utf-8")) > 2000:
            raise ValueError("메시지는 UTF-8 기준 2000바이트 이하여야 합니다.")
        if "\n" in message or "\r" in message:
            raise ValueError("한 번에 한 줄만 보낼 수 있습니다.")
        with self.lock:
            sock = self.sock
            if not self.connected or sock is None:
                raise RuntimeError("서버에 연결되어 있지 않습니다.")
        # The TCP server requires a routing target envelope. Keep the web
        # input focused on the protocol payload (e.g. SET@DRAWER:OPEN:N).
        line = ("[SERVER]" + message + "\n").encode("utf-8")
        try:
            with self.send_lock:
                sock.sendall(line)
        except OSError as exc:
            raise RuntimeError(f"전송 실패: {exc}") from exc
        self.add_event("sent", f"→ [SERVER]{message}")

    def disconnect(self):
        with self.lock:
            sock = self.sock
            self.sock = None
            self.connected = False
            self.status = "연결 해제됨"
        if sock is not None:
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            sock.close()

    def state_since(self, after):
        with self.lock:
            return {
                "connected": self.connected,
                "status": self.status,
                "events": [event for event in self.events if event["seq"] > after],
            }


BRIDGE = Bridge()


class Handler(BaseHTTPRequestHandler):
    server_version = "RetraceChatDebug/1.0"

    def log_message(self, fmt, *args):
        # Keep access logs local and avoid logging request bodies or credentials.
        print("[web] " + (fmt % args))

    def send_json(self, status, payload):
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def read_json(self):
        length = int(self.headers.get("Content-Length", "0"))
        if length > 8192:
            raise ValueError("요청 데이터가 너무 큽니다.")
        data = json.loads(self.rfile.read(length).decode("utf-8"))
        if not isinstance(data, dict):
            raise ValueError("JSON 객체가 필요합니다.")
        return data

    def do_GET(self):
        path = urllib.parse.urlsplit(self.path)
        if path.path == "/":
            body = PAGE.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
        elif path.path == "/api/state":
            query = urllib.parse.parse_qs(path.query)
            try:
                after = max(0, int(query.get("after", ["0"])[0]))
            except ValueError:
                after = 0
            self.send_json(200, BRIDGE.state_since(after))
        else:
            self.send_json(404, {"error": "Not found"})

    def do_POST(self):
        try:
            data = self.read_json()
            path = urllib.parse.urlsplit(self.path).path
            if path == "/api/connect":
                BRIDGE.connect(
                    str(data.get("host", "")).strip(),
                    int(data.get("port", 0)),
                    str(data.get("client_id", "")).strip(),
                    str(data.get("password", "")),
                )
                self.send_json(200, {"connected": True})
            elif path == "/api/send":
                BRIDGE.send_line(str(data.get("message", "")))
                self.send_json(200, {"sent": True})
            elif path == "/api/disconnect":
                BRIDGE.disconnect()
                self.send_json(200, {"disconnected": True})
            else:
                self.send_json(404, {"error": "Not found"})
        except (ValueError, RuntimeError, OSError, json.JSONDecodeError) as exc:
            self.send_json(400, {"error": str(exc)})


def main():
    address = ("127.0.0.1", 8765)
    server = ThreadingHTTPServer(address, Handler)
    print(f"Retrace CHAT Debug UI: http://{address[0]}:{address[1]}")
    print("Local-only bridge. Press Ctrl+C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping web bridge...")
    finally:
        BRIDGE.disconnect()
        server.server_close()


if __name__ == "__main__":
    main()
