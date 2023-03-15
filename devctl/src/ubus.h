#ifndef SEND_INFO_H
#define SEND_INFO_H

#include <stdbool.h>

#include <libubus.h>

/*
 * Returns true on success, false on failure.
 */
bool init_ubus(struct ubus_context **ubus_ctx);

#endif
