#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>

#include <uci.h>
#include <libubus.h>

#include "args.h"
#include "ubus.h"
#include "serial.h"

const char *options_const[] = { "devctl.devctl.log_level" };
const size_t options_count = sizeof(options_const) / sizeof(options_const[0]);

const int log_priorities[8] = { LOG_EMERG,   LOG_ALERT,	 LOG_CRIT, LOG_ERR,
				LOG_WARNING, LOG_NOTICE, LOG_INFO, LOG_DEBUG };

int main(void)
{
	int ret_val = EXIT_SUCCESS;

	openlog("devctl", LOG_PID | LOG_CONS, LOG_LOCAL0);

	char *option_names[] = {
		strdup(options_const[0]),
	};
	// Get program settings from UCI.
	struct uci_context *uci_ctx = uci_alloc_context();
	if (uci_ctx == NULL) {
		syslog(LOG_ERR, "Failed to allocate UCI context");
		ret_val = EXIT_FAILURE;
		goto cleanup_end;
	}
	struct uci_ptr uci_ptr;
	char *log_level_str = uci_get_option(uci_ctx, &uci_ptr, option_names[0],
					     options_const[0]);
	// All options must be specified.
	if (log_level_str == NULL) {
		ret_val = EXIT_FAILURE;
		goto cleanup_end;
	}
	int log_level;
	if (!str_to_digit(log_level_str, &log_level) || log_level > 7) {
		syslog(LOG_ERR, "Unrecognized value for option 'log_level': %s",
		       log_level_str);
		ret_val = EXIT_FAILURE;
		goto cleanup_end;
	}
	setlogmask(LOG_UPTO(log_priorities[log_level]));

	syslog(LOG_DEBUG, "Options: log_level: %d", log_level);

	struct ubus_context *ubus_ctx;
	if (!init_ubus(&ubus_ctx)) {
		goto cleanup_end;
	}

	int signum = uloop_run();
	if (signum != 0) {
		syslog(LOG_INFO, "Got signal to exit");
	}

	ubus_free(ubus_ctx);
	uloop_done();
cleanup_end:
	syslog(LOG_INFO, "Cleaning up resources and exiting");
	for (size_t i = 0; i < options_count; ++i) {
		free(option_names[i]);
	}
	uci_free_context(uci_ctx);
	closelog();
	return ret_val;
}
