#include <syslog.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include <libubox/blobmsg_json.h>
#include <libubus.h>

#include "ubus.h"

// Set pin on/off.
static int control_pin(struct ubus_context *ctx, struct ubus_object *obj,
		       struct ubus_request_data *req, const char *method,
		       struct blob_attr *msg);

// List connected devices.
static int list_devices(struct ubus_context *ctx, struct ubus_object *obj,
			struct ubus_request_data *req, const char *method,
			struct blob_attr *msg);

enum { CTL_DEVICE_ID, CTL_METHOD, CTL_PIN, __CTL_MAX };

static const struct blobmsg_policy command_policy[] = {
	[CTL_DEVICE_ID] = { .name = "device_id", .type = BLOBMSG_TYPE_STRING },
	[CTL_METHOD] = { .name = "method", .type = BLOBMSG_TYPE_STRING },
	[CTL_PIN] = { .name = "pin", .type = BLOBMSG_TYPE_INT32 }
};

static const struct ubus_method devctl_methods[] = {
	UBUS_METHOD_NOARG("list_devices", list_devices),
	UBUS_METHOD("control_pin", control_pin, command_policy)
};

static struct ubus_object_type devctl_object_type =
	UBUS_OBJECT_TYPE("devctl", devctl_methods);

static struct ubus_object devctl_object = { .name = "devctl",
					    .type = &devctl_object_type,
					    .methods = devctl_methods,
					    .n_methods = ARRAY_SIZE(
						    devctl_methods) };

static int control_pin(struct ubus_context *ctx, struct ubus_object *obj,
		       struct ubus_request_data *req, const char *method,
		       struct blob_attr *msg)
{
	(void)obj;

	syslog(LOG_DEBUG, "Received ubus message of type '%s': %s", method,
	       blobmsg_format_json(msg, true));
	struct blob_attr *tb[__CTL_MAX];

	blobmsg_parse(command_policy, __CTL_MAX, tb, blob_data(msg),
		      blob_len(msg));
	if (tb[CTL_DEVICE_ID] == NULL || tb[CTL_METHOD] == NULL ||
	    tb[CTL_PIN] == NULL) {
		syslog(LOG_WARNING, "Failed to parse ubus message");
		return UBUS_STATUS_INVALID_ARGUMENT;
	}

	char *dev_id = blobmsg_get_string(tb[CTL_DEVICE_ID]);
	char *dev_method = blobmsg_get_string(tb[CTL_METHOD]);
	uint32_t dev_pin = blobmsg_get_u32(tb[CTL_PIN]);
	syslog(LOG_DEBUG, "dev_id = %s, dev_method = %s, dev_pin = %u", dev_id,
	       dev_method, dev_pin);

	struct blob_buf b = {};
	blob_buf_init(&b, 0);

	blobmsg_add_string(&b, "second_example_key", "second example value");
	int ret = ubus_send_reply(ctx, req, b.head);
	if (ret != 0) {
		syslog(LOG_ERR, "Failed to send ubus reply: %s",
		       ubus_strerror(ret));
		return ret;
	}
	blob_buf_free(&b);

	return UBUS_STATUS_OK;
}

// List connected devices.
static int list_devices(struct ubus_context *ctx, struct ubus_object *obj,
			struct ubus_request_data *req, const char *method,
			struct blob_attr *msg)
{
	(void)msg;
	(void)obj;

	syslog(LOG_DEBUG, "Received ubus message of type '%s'", method);
	struct blob_buf b = {};
	blob_buf_init(&b, 0);

	blobmsg_add_string(&b, "example_key", "example value");
	int ret = ubus_send_reply(ctx, req, b.head);
	if (ret != 0) {
		syslog(LOG_ERR, "Failed to send ubus reply: %s",
		       ubus_strerror(ret));
		return ret;
	}
	blob_buf_free(&b);

	return UBUS_STATUS_OK;
}

bool init_ubus(struct ubus_context **ubus_ctx)
{
	uloop_init();

	// Connect to ubus
	*ubus_ctx = ubus_connect(NULL);
	if (*ubus_ctx == NULL) {
		syslog(LOG_ERR, "Failed to connect to ubus");
		return false;
	}

	ubus_add_uloop(*ubus_ctx);
	int ret = ubus_add_object(*ubus_ctx, &devctl_object);
	if (ret != 0) {
		syslog(LOG_ERR, "Error adding ubus object: %s",
		       ubus_strerror(ret));
	}

	return true;
}
