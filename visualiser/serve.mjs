import http from "node:http";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";

const root = fileURLToPath(new URL("./dist/", import.meta.url));
const port = Number(process.env.VISUALISER_PORT || 4173);
if (!Number.isInteger(port) || port < 1024 || port > 65535) {
  throw new Error("VISUALISER_PORT must be an integer between 1024 and 65535.");
}
const allowed = new Map([
  ["/", ["index.html", "text/html; charset=utf-8"]],
  ["/index.html", ["index.html", "text/html; charset=utf-8"]],
  ["/style.css", ["style.css", "text/css; charset=utf-8"]],
  ["/app.mjs", ["app.mjs", "text/javascript; charset=utf-8"]],
  ["/diagram.mjs", ["diagram.mjs", "text/javascript; charset=utf-8"]],
  ["/plan.mjs", ["plan.mjs", "text/javascript; charset=utf-8"]],
]);
const server = http.createServer(async (req, res) => {
  if (!["GET", "HEAD"].includes(req.method)) {
    res.writeHead(405, { Allow: "GET, HEAD" }).end();
    return;
  }
  const entry = allowed.get(new URL(req.url, "http://localhost").pathname);
  if (!entry) {
    res.writeHead(404, { "Content-Type": "text/plain" }).end("Not found");
    return;
  }
  try {
    const data = await readFile(path.join(root, entry[0]));
    res.writeHead(200, {
      "Content-Type": entry[1],
      "Cache-Control": "no-store",
      "X-Content-Type-Options": "nosniff",
      // Embedding is intentional: VS Code's Simple Browser uses a webview iframe.
      "Content-Security-Policy": "default-src 'self'; style-src 'self'; script-src 'self'; img-src 'self' data:; connect-src 'none'; object-src 'none'; base-uri 'none'",
    });
    res.end(req.method === "HEAD" ? undefined : data);
  } catch {
    res.writeHead(500, { "Content-Type": "text/plain" }).end("Unable to read visualiser file");
  }
});
server.on("error", (error) => {
  console.error(error.code === "EADDRINUSE"
    ? `Port ${port} is already in use. Open http://127.0.0.1:${port}, or set VISUALISER_PORT to another port.`
    : error.message);
  process.exitCode = 1;
});
server.listen(port, "127.0.0.1", () => {
  console.log(`Visualiser ready: http://127.0.0.1:${port}`);
  console.log("Local viewing only. No ESP connection. Press Ctrl+C to stop.");
});
