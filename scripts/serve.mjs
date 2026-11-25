// Tiny static server for dist/ (not needed to use the app: dist/index.html also works from file://).
import http from "node:http";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
const dist = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "dist");
const port = Number(process.env.PORT ?? 8080);
const types = { ".html": "text/html; charset=utf-8", ".js": "text/javascript", ".mjs": "text/javascript", ".wasm": "application/wasm" };
http.createServer(async (req, res) => {
  const p = path.normalize(decodeURIComponent((req.url ?? "/").split("?")[0])).replace(/^(\.\.[/\\])+/, "");
  const file = path.join(dist, p === "/" ? "index.html" : p);
  if (!file.startsWith(dist)) { res.writeHead(403).end(); return; }
  try { const b = await readFile(file); res.writeHead(200, { "content-type": types[path.extname(file)] ?? "application/octet-stream" }).end(b); }
  catch { res.writeHead(404).end("not found"); }
}).listen(port, "127.0.0.1", () => console.log(`http://127.0.0.1:${port}/`));
