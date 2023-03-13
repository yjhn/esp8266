#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syslog.h>
#include <time.h>
#include <syslog.h>
#include <stdio.h>

#include <libubus.h>

#include "connection.h"
#include "send_info.h"
#include "signals.h"

int init_connections(struct ubus_context **ubus_ctx)
{
	// Connect to ubus
	*ubus_ctx = ubus_connect(NULL);
	if (*ubus_ctx == NULL) {
		syslog(LOG_ERR, "Failed to connect to ubus");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
