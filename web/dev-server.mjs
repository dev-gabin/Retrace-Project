// 로컬 프런트엔드 확인용 정적 서버. Jetson의 API 서버가 아닙니다.
import http from 'node:http';
import { readFile, stat } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.dirname(fileURLToPath(import.meta.url));
const port = Number(process.env.PORT ?? 5173);
if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('PORT must be 1..65535');
const mime = { '.html': 'text/html; charset=utf-8', '.css': 'text/css; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.mjs': 'text/javascript; charset=utf-8', '.json': 'application/json; charset=utf-8', '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg', '.webp': 'image/webp' };

const server = http.createServer(async (request, response) => {
  const send = (status, body) => { response.writeHead(status, { 'Content-Type': 'text/plain; charset=utf-8' }); response.end(body); };
  if (!['GET', 'HEAD'].includes(request.method)) { send(405, 'Static preview only; no device API.'); return; }
  try {
    const pathname = decodeURIComponent(new URL(request.url, 'http://127.0.0.1').pathname);
    if (pathname.startsWith('/api/')) { send(404, 'No Jetson API on this preview server.'); return; }
    const file = path.resolve(root, '.' + (pathname === '/' ? '/index.html' : pathname));
    if (!file.startsWith(root + path.sep) || pathname.includes('\\')) { send(403, 'Forbidden'); return; }
    if (!(await stat(file)).isFile()) { send(404, 'Not found'); return; }
    const content = await readFile(file);
    response.writeHead(200, { 'Content-Type': mime[path.extname(file)] ?? 'application/octet-stream', 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff' });
    response.end(request.method === 'HEAD' ? undefined : content);
  } catch (error) { send(error.code === 'ENOENT' || error.code === 'ENOTDIR' ? 404 : 400, 'Not found or invalid path'); }
});
server.listen(port, '127.0.0.1', () => process.stdout.write(`Retrace preview: http://127.0.0.1:${port}/ (sample mode; no Jetson API)\n`));
server.on('error', error => { process.stderr.write(`${error.message}\n`); process.exitCode = 1; });
