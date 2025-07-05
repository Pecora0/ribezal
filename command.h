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
    [DROP]     = "*tbd*",
    [CLEAR]    = "*tbd*",
    [REQUEST]  = "(string ->)",
    [TG_GETME] = "(string -> string)",
    [PLUS]     = "(int int -> int)",
    [MINUS]    = "(int int -> int)",
    [TIMES]    = "(int int -> int)",
    [DIVIDE]   = "(int int -> int)",
};
static_assert(sizeof(command_stack_config) / sizeof(command_stack_config[0]) == COMMAND_COUNT);

const char *command_description[] = {
    [HELP]     = "Prints documentation for the commands.",
    [QUIT]     = "Closes this repl.",
    [PRINT]    = "Prints out the current stack.",
    [DROP]     = "Removes the top element from stack.",
    [CLEAR]    = "Removes all elements from stack.",
    [REQUEST]  = "Performs Https-Request to the given URL.",
    [TG_GETME] = "Constructs the URL for the Telegram method \"getMe\" out of the given bot token.",
    [PLUS]     = "Adds two numbers.", 
    [MINUS]    = "Subtracts one number from the other.", 
    [TIMES]    = "Multiplies two numbers.", 
    [DIVIDE]   = "Divides one number by the other.",
};
static_assert(sizeof(command_description) / sizeof(command_description[0]) == COMMAND_COUNT);

#endif // COMMAND_H
