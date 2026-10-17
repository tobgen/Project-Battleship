#include "common.h"
#include "i18n.h"

#include <locale.h>
#include <stdio.h>
#include <string.h>

int server_main(int argc, char **argv);
int client_main(int argc, char **argv);
int board_main(int argc, char **argv);

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    const char *lang_arg = NULL;
    for (int i = 1; i + 1 < argc; i++)
        if (strcmp(argv[i], "--lang") == 0) lang_arg = argv[i + 1];
    i18n_init(lang_arg);

    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            printf("%s\n", VERSION_STRING);
            return 0;
        }
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        fprintf(stdout, "%s", tr(S_MAIN_USAGE));
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "server") == 0) return server_main(argc - 2, argv + 2);
    if (argc >= 2 && strcmp(argv[1], "client") == 0) return client_main(argc - 2, argv + 2);
    if (argc >= 2 && strcmp(argv[1], "board") == 0) return board_main(argc - 2, argv + 2);
    fprintf(stderr, "%s", tr(S_MAIN_USAGE));
    return 2;
}
