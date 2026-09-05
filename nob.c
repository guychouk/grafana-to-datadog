#define NOB_IMPLEMENTATION
#include "nob.h"

#include "dev.h"

#define DEV_PORT "4102"
#define OUT_DIR "./public"
#define SITE_URL "https://gtd.guycho.uk"
#define CF_PASS_PREFIX "cloudflare/guycho.uk"

#include "cloudflare.h"

static bool build(void)
{
    nob_log(NOB_INFO, "--- Building gtd.guycho.uk ---");
    nob_mkdir_if_not_exists("./public");
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "rsync", "-a", "./assets/", "./public/");
    if (!nob_cmd_run_sync_and_reset(&cmd)) return false;
    nob_cmd_append(&cmd, "rsync", "-a", "./src/", "./public/");
    if (!nob_cmd_run_sync_and_reset(&cmd)) return false;
    nob_log(NOB_INFO, "--- Done: gtd.guycho.uk ---");
    return true;
}

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);
    const char *program = nob_shift(argv, argc);
    char *current_dir = getcwd(NULL, 0);
    if (current_dir == NULL) {
        perror("getcwd");
        return 1;
    }
    const char *cmd = argc > 0 ? nob_shift(argv, argc) : "build";
    if (strcmp(cmd, "build") == 0) {
        if (!build()) return 1;
        return 0;
    }
    if (strcmp(cmd, "print-public-dir") == 0) {
        printf("%s/public\n", current_dir);
        return 0;
    }
    if (strcmp(cmd, "dev") == 0) {
        const char *watch[] = {"src", "assets"};
        return dev_serve(DEV_PORT, watch, NOB_ARRAY_LEN(watch), build) ? 0 : 1;
    }
    if (strcmp(cmd, "purge-cache") == 0) {
        if (!purge_cache()) return 1;
        return 0;
    }
    nob_log(NOB_ERROR, "unknown command: %s", cmd);
    nob_log(NOB_ERROR, "usage: %s [build|dev|print-public-dir|purge-cache]", program);
    return 1;
}
