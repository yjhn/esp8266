#ifndef ARGS_H
#define ARGS_H

#include <stdbool.h>
#include <uci.h>

// Get option from UCI. Only string option type is supported.
char *uci_get_option(struct uci_context *ctx, struct uci_ptr *ptr, char *option,
		     const char *const_option);

// Converts str to boolean value. String "1" is converted to true,
// "0" is converted to false.
// Returns true on successful conversion, false otherwise.
// Writes the parsed value to result.
bool str_to_bool(const char *str, bool *result);

// Converts string containg exactly one ASCII digit to that digit.
bool str_to_digit(const char *str, int *result);

#endif
