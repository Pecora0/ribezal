#ifndef COMMAND_H
#define COMMAND_H

#include <assert.h>

typedef enum {
    HELP,
    QUIT,
    PRINT,
    DROP,
    CLEAR,
    REQUEST,
    TG_GETME,
    PLUS,
    MINUS,
    TIMES,
    DIVIDE,
    COMMAND_COUNT,
} Command;

const char *command_keyword[] = {
    [HELP]     = "help",
    [QUIT]     = "quit",
    [PRINT]    = "print",
    [DROP]     = "drop",
    [CLEAR]    = "clear",
    [REQUEST]  = "request",
    [TG_GETME] = "tg-getMe",
    [PLUS]     = "+", 
    [MINUS]    = "-", 
    [TIMES]    = "*", 
    [DIVIDE]   = "/",
};
static_assert(sizeof(command_keyword) / sizeof(command_keyword[0]) == COMMAND_COUNT);

const char *command_stack_config[] = {
    [HELP]     = "(->)",
    [QUIT]     = "(->)",
    [PRINT]    = "(->)",
    [DROP]     = "*no stack configuration provided*",
    [CLEAR]    = "*no stack configuration provided*",
    [REQUEST]  = "*no stack configuration provided*",
    [TG_GETME] = "*no stack configuration provided*",
    [PLUS]     = "*no stack configuration provided*",
    [MINUS]    = "*no stack configuration provided*",
    [TIMES]    = "*no stack configuration provided*",
    [DIVIDE]   = "*no stack configuration provided*",
};
static_assert(sizeof(command_stack_config) / sizeof(command_stack_config[0]) == COMMAND_COUNT);

const char *command_description[] = {
    [HELP]     = "Prints documentation for the commands.",
    [QUIT]     = "Closes this repl.",
    [PRINT]    = "Prints out the current stack.",
    [DROP]     = "*no description provided*",
    [CLEAR]    = "*no description provided*",
    [REQUEST]  = "*no description provided*",
    [TG_GETME] = "*no description provided*",
    [PLUS]     = "*no description provided*", 
    [MINUS]    = "*no description provided*", 
    [TIMES]    = "*no description provided*", 
    [DIVIDE]   = "*no description provided*",
};
static_assert(sizeof(command_description) / sizeof(command_description[0]) == COMMAND_COUNT);

#endif // COMMAND_H
