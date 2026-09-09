# gtd.guycho.uk

Static site served at https://gtd.guycho.uk. Sources in `src/`, static files in `assets/`, generated output in `public/` (gitignored).

## Interface

`./nob` builds. `./nob dev` rebuilds on change and serves `public/` at http://localhost:4102 with live reload. `./nob print-public-dir` prints the absolute output directory. There is no `install`: this project produces no binary.

`nob` is gitignored, so bootstrap it after a clone with `cc -o nob nob.c`; it rebuilds itself when `nob.c` changes, during `dev` too. The dev loop is the vendored `dev.h` plus `dev-serve.ts`, shared verbatim by all eight sites: nob polls mtimes over the watch list at the top of the `dev` branch and rebuilds in-process, and spawns exactly one child, a small bun static server (`dev-serve.ts`) that serves `public/` and pushes an SSE reload to the browser whenever it changes, through `mise exec -- bun` (so `mise install` first). No nodemon, no concurrently, no browser-sync. It kills that child on SIGINT and SIGTERM, which is what stops `paddock stop` from leaking a process holding the port. `package.json` scripts are thin wrappers over `./nob`; nob is the interface, not bun.

`dev` refuses to start when port 4102 is already taken, and says so. If it refuses, find the squatter with `lsof -nP -iTCP:4102 -sTCP:LISTEN` rather than moving the port.

This site is a paddock target named `gtd.guycho.uk`, so `paddock start gtd.guycho.uk` is the normal way to bring the dev server up and leave it running.

## Build

Rsync `assets/` and `src/` into `public/`. Unlike the other sites this one does not wipe `public/` first, so a deleted source file lingers until the directory is removed by hand.

## Deploy

`git push piku main`. Piku reads `Procfile`: the release step compiles `nob.c` on the server and builds, then `public/` is served statically. There is no gitea remote here; the `github` remote points at `guychouk/grafana-to-datadog`, which is a different project and almost certainly a leftover mistake.
