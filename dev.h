// dev.h - the `nob dev` loop for the static sites. Include after nob.h.
//
// One child process: dev-serve.ts (bun, vendored alongside this file), which
// serves ./public and pushes reloads into the open tab over SSE. The file
// watching that triggers a *rebuild* and the process supervision live here
// instead of in nodemon/concurrently, because nob already knows how to spawn
// processes and compare mtimes; dev-serve.ts only watches ./public itself to
// know when to tell the browser to reload.

#ifndef DEV_H_
#define DEV_H_

#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define DEV_POLL_MS 300

typedef bool (*Dev_Build)(void);

// Serve ./public on port and rebuild whenever anything under watch[] changes.
// Each entry may be a file or a directory; directories are walked recursively,
// dotfiles skipped. Returns only on failure; SIGINT and SIGTERM tear the child
// down and exit, which matters under paddock since it signals this pid alone
// and a leaked dev-serve.ts would sit on the port.
static bool dev_serve(const char *port, const char **watch, size_t watch_count, Dev_Build build);

#endif // DEV_H_

static pid_t dev_child = -1;

static void dev_teardown(int sig)
{
    if (dev_child > 0) kill(dev_child, SIGTERM);
    _exit(128 + sig);
}

static time_t dev_mtime(const char *path)
{
    struct stat st;
    if (stat(path, &st) < 0) return 0;
    if (!S_ISDIR(st.st_mode)) return st.st_mtime;

    time_t latest = st.st_mtime;
    Nob_File_Paths children = {0};
    if (!nob_read_entire_dir(path, &children)) return latest;
    for (size_t i = 0; i < children.count; i++) {
        const char *name = children.items[i];
        if (name[0] == '.') continue;
        time_t child = dev_mtime(nob_temp_sprintf("%s/%s", path, name));
        if (child > latest) latest = child;
    }
    nob_da_free(children);
    return latest;
}

static time_t dev_latest(const char **watch, size_t watch_count)
{
    size_t mark = nob_temp_save();
    time_t latest = 0;
    for (size_t i = 0; i < watch_count; i++) {
        time_t t = dev_mtime(watch[i]);
        if (t > latest) latest = t;
    }
    nob_temp_rewind(mark);
    return latest;
}

// Check the port before spawning so a busy port is a clear error message
// instead of an EADDRINUSE stack trace surfacing async from the bun child.
// SO_REUSEADDR so a TIME_WAIT socket from the previous run does not read as
// occupied.
static bool dev_port_free(const char *port)
{
    int fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd < 0) return true;
    int on = 1, off = 0;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
    struct sockaddr_in6 addr = {0};
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;
    addr.sin6_port = htons((uint16_t)atoi(port));
    bool free_ = bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0;
    close(fd);
    return free_;
}

static bool dev_serve(const char *port, const char **watch, size_t watch_count, Dev_Build build)
{
    if (!dev_port_free(port)) {
        nob_log(NOB_ERROR, "dev: port %s is already in use", port);
        nob_log(NOB_ERROR, "dev: find the squatter with `lsof -nP -iTCP:%s -sTCP:LISTEN`", port);
        return false;
    }
    if (!build()) return false;

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "mise", "exec", "--", "bun", "dev-serve.ts", "public", port);
    Nob_Proc proc = nob_cmd_run_async_and_reset(&cmd);
    if (proc == NOB_INVALID_PROC) return false;
    dev_child = proc;
    signal(SIGINT, dev_teardown);
    signal(SIGTERM, dev_teardown);

    nob_log(NOB_INFO, "dev: watching for changes, serving on http://localhost:%s", port);

    time_t last = dev_latest(watch, watch_count);
    time_t last_nob = dev_mtime("nob.c");
    for (;;) {
        usleep(DEV_POLL_MS * 1000);

        int status;
        if (waitpid(dev_child, &status, WNOHANG) == dev_child) {
            nob_log(NOB_ERROR, "dev: dev-serve.ts exited");
            dev_child = -1;
            return false;
        }

        // An edit to the build itself restarts dev, the way GO_REBUILD_URSELF
        // handles it for every other command.
        if (dev_mtime("nob.c") > last_nob) {
            nob_log(NOB_INFO, "dev: nob.c changed, restarting");
            nob_cmd_append(&cmd, "cc", "-o", "nob", "nob.c");
            if (!nob_cmd_run_sync_and_reset(&cmd)) {
                last_nob = dev_mtime("nob.c");
                continue;
            }
            kill(dev_child, SIGTERM);
            waitpid(dev_child, &status, 0);
            execl("./nob", "./nob", "dev", (char *)NULL);
            nob_log(NOB_ERROR, "dev: could not exec ./nob: %s", strerror(errno));
            return false;
        }

        time_t now = dev_latest(watch, watch_count);
        if (now <= last) continue;
        if (!build()) nob_log(NOB_ERROR, "dev: build failed, waiting for the next change");
        // Re-baseline after the build: some builds write into a watched
        // directory (haiku regenerates assets/data/entries.json), which would
        // otherwise rebuild forever.
        last = dev_latest(watch, watch_count);
    }
}
