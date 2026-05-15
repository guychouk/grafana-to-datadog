#define NOB_IMPLEMENTATION
#include "nob.h"

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);
    const char *program = nob_shift(argv, argc);
    if (argc < 1) {
        printf("%s <build|print-public-dir>\n", program);
        return 1;
    }
    char *current_dir = getcwd(NULL, 0);
    if (current_dir == NULL) {
        perror("getcwd");
        return 1;
    }
    const char *cmd = nob_shift(argv, argc);
    if (strcmp(cmd, "build") == 0) {
        nob_log(NOB_INFO, "--- Building gtd.guycho.uk ---");
        nob_mkdir_if_not_exists("./public");
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "rsync", "-a", "./assets/", "./public/");
        if (!nob_cmd_run_sync_and_reset(&cmd)) return 1;
        nob_cmd_append(&cmd, "rsync", "-a", "./src/", "./public/");
        if (!nob_cmd_run_sync_and_reset(&cmd)) return 1;
        nob_log(NOB_INFO, "--- Done: gtd.guycho.uk ---");
        return 0;
    }
    if (strcmp(cmd, "print-public-dir") == 0) {
        printf("%s/public\n", current_dir);
        return 0;
    }
    return 1;
}
