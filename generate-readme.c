#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "command.h"

#define FILE_NAME "README.md"

const char *readme_pre_doc[] = {
    "# ribezal",
    "",
    "Simple messaging server as an experimentation with asynchronous programming in C.",
    "",
    "## Quickstart",
    "",
    "Build with `make` and run `ribezal` preferably in background.",
    "",
    "```console",
    "$ make",
    "$ ./ribezal &",
    "```",
    "",
    "You can send \"messages\" to the server by writing to `input-fifo`, e.g.:",
    "",
    "```console",
    "$ echo \"your command\" > input-fifo",
    "```",
    "",
    "Stop the server with command `quit`:",
    "",
    "```console",
    "$ echo \"quit\" > input-fifo",
    "```",
    "",
    "## Documentation",
    "",
    "You can communicate with ribezal using a little stack based language.",
    "(At the moment this is just a simple calculator).",
    "Commands are separated by whitespace.",
    "Integers are recognised as such and pushed to the stack.",
    "All commands that are not integers or keywords are pushed as strings to the strings.",
    "",
    "There are the following keywords:",
};
#define README_PRE_DOC_COUNT (sizeof(readme_pre_doc) / sizeof(readme_pre_doc[0]))

const char *readme_post_doc[] = {
    "",
    "## Shoutouts",
    "",
    "Some cool ressources I am using as inspiration:",
    "",
    "- [c3fut](https://github.com/tsoding/c3fut): a futures implementation for the language C3 using interfaces",
    "- [Pool Allocator](https://www.gingerbill.org/article/2019/02/16/memory-allocation-strategies-004/):",
    "    excellent explanation of the principle of a pool allocator",
};
#define README_POST_DOC_COUNT (sizeof(readme_post_doc) / sizeof(readme_post_doc[0]))

int main() {
    FILE *f = fopen(FILE_NAME, "w");
    if (!f) {
        printf("[ERROR] Couldn't open file '%s': %s\n", FILE_NAME, strerror(errno));
        exit(1);
    }

    for (size_t i=0; i<README_PRE_DOC_COUNT; i++) {
        fprintf(f, "%s\n", readme_pre_doc[i]);
    }

    for (Command i=0; i<COMMAND_COUNT; i++) {
        fprintf(f, "- `%s`:\n", command_keyword[i]);
        fprintf(f, "    - Stack: %s\n", command_stack_config[i]);
        fprintf(f, "    - Description: %s\n", command_description[i]);
    }

    for (size_t i=0; i<README_POST_DOC_COUNT; i++) {
        fprintf(f, "%s\n", readme_post_doc[i]);
    }

    int r = fclose(f);
    if (r != 0) {
        printf("[ERROR] Couldn't close file '%s': %s\n", FILE_NAME, strerror(errno));
        exit(1);
    }
}
