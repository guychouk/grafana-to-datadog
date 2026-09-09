// dev-serve.ts - static file server + SSE live reload for `nob dev`.
// Replaces browser-sync: serves argv[2] on argv[3], watches that same
// directory with fs.watch, and pushes one SSE message per change. The
// client-side half is the two-line snippet injected into every HTML
// response below. No bundler, no npm dependency.

import { watch } from "node:fs";
import { extname, join } from "node:path";

const dir = process.argv[2];
const port = Number(process.argv[3]);

let clients: ReadableStreamDefaultController[] = [];

function broadcast() {
  for (const c of clients) {
    try {
      c.enqueue("data: reload\n\n");
    } catch {}
  }
}

watch(dir, { recursive: true }, broadcast);

const LIVERELOAD = `<script>new EventSource("/__events").onmessage=()=>location.reload();</script>`;

Bun.serve({
  port,
  async fetch(req) {
    const url = new URL(req.url);

    if (url.pathname === "/__events") {
      const stream = new ReadableStream({
        start(controller) {
          clients.push(controller);
        },
        cancel(controller) {
          clients = clients.filter((c) => c !== controller);
        },
      });
      return new Response(stream, {
        headers: {
          "Content-Type": "text/event-stream",
          "Cache-Control": "no-cache",
          Connection: "keep-alive",
        },
      });
    }

    const reqPath = url.pathname === "/" ? "/index.html" : url.pathname;
    let filePath = join(dir, reqPath);
    let file = Bun.file(filePath);
    if (!(await file.exists()) && extname(reqPath) === "") {
      filePath = join(dir, reqPath, "index.html");
      file = Bun.file(filePath);
    }
    if (!(await file.exists())) return new Response("not found", { status: 404 });

    if (extname(filePath) === ".html") {
      const html = await file.text();
      const injected = html.includes("</body>")
        ? html.replace("</body>", `${LIVERELOAD}</body>`)
        : html + LIVERELOAD;
      return new Response(injected, { headers: { "Content-Type": "text/html" } });
    }

    return new Response(file);
  },
});

console.log(`dev-serve: http://localhost:${port}`);
