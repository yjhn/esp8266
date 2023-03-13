#include <stdbool.h>
#include <string.h>
#include <syslog.h>
#include <stdlib.h>

#include <uci.h>

#include "args.h"

static bool uci_check_success(int ret, const struct uci_ptr *ptr,
			      const struct uci_context *ctx,
			      const char *option_name)
{
	if (ret == UCI_ERR_NOTFOUND) {
		char *error;
		uci_get_errorstr(ctx, &error, "");
		syslog(LOG_ERR, "Option '%s' not found%s", option_name, error);
		free(error);
		return false;
	} else if ((ptr->flags & UCI_LOOKUP_COMPLETE) == 0) {
		syslog(LOG_ERR, "Option '%s' not found", option_name);
		return false;
	}
	return true;
}

// option must not be const as it will be modified by uci_lookup_ptr
// Returs NULL on failure.
char *uci_get_option(struct uci_context *ctx, struct uci_ptr *ptr, char *option,
		     const char *const_option)
{
	int ret = uci_lookup_ptr(ctx, ptr, option, false);
	if (!uci_check_success(ret, ptr, ctx, const_option)) {
		return NULL;
	}
	if (ptr->o->type != UCI_TYPE_STRING) {
		syslog(LOG_ERR,
		       "Option '%s' value is of unexpected type: expected UCI_TYPE_STRING, got %u",
		       const_option, ptr->o->type);
		return NULL;
	}

	return ptr->o->v.string;
}

bool str_to_bool(const char *str, bool *result)
{
	if (strcmp(str, "1") == 0) {
		*result = true;
		return true;
	} else if (strcmp(str, "0") == 0) {
		*result = false;
		return true;
	} else {
		return false;
	}
}

bool str_to_digit(const char *str, int *result)
{
	// String must consist of exactly one character
	if (strlen(str) != 1) {
		return false;
	}
	// 48 == 0, 57 == 9
	if (str[0] < 48 || str[0] > 57) {
		return false;
	}
	*result = str[0] - 48;
	return true;
}
