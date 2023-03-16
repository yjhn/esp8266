#include <syslog.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include <libubox/blobmsg_json.h>
#include <libubus.h>

#include "ubus.h"
#include "serial.h"

#define MAX_DEVS 10
#define MSG_MAXLEN 50
#define LIST_DEVICES_METHOD_NAME "list_devices"
#define TURN_ON_PIN_METHOD_NAME "turn_on_pin"
#define TURN_OFF_PIN_METHOD_NAME "turn_off_pin"

// Turn specified pin from specified device on or off.
static int control_pin(struct ubus_context *ctx, struct ubus_object *obj,
		       struct ubus_request_data *req, const char *method,
		       struct blob_attr *msg);

// List connected devices.
static int list_devices(struct ubus_context *ctx, struct ubus_object *obj,
			struct ubus_request_data *req, const char *method,
			struct blob_attr *msg);

enum { CTL_DEVICE_ID, CTL_PIN, __CTL_MAX };

static const struct blobmsg_policy command_policy[] = {
	[CTL_DEVICE_ID] = { .name = "device_id", .type = BLOBMSG_TYPE_STRING },
	[CTL_PIN] = { .name = "pin", .type = BLOBMSG_TYPE_INT32 }
};

static const struct ubus_method devctl_methods[] = {
	UBUS_METHOD_NOARG(LIST_DEVICES_METHOD_NAME, list_devices),
	UBUS_METHOD(TURN_ON_PIN_METHOD_NAME, control_pin, command_policy),
	UBUS_METHOD(TURN_OFF_PIN_METHOD_NAME, control_pin, command_policy)
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
	int ret_val = UBUS_STATUS_OK;

	syslog(LOG_DEBUG, "Received ubus message of type '%s': %s", method,
	       blobmsg_format_json(msg, true));
	struct blob_attr *tb[__CTL_MAX];

	blobmsg_parse(command_policy, __CTL_MAX, tb, blob_data(msg),
		      blob_len(msg));
	if (tb[CTL_DEVICE_ID] == NULL || tb[CTL_PIN] == NULL) {
		syslog(LOG_WARNING, "Failed to parse ubus message");
		return UBUS_STATUS_INVALID_ARGUMENT;
	}

	char *dev_id = blobmsg_get_string(tb[CTL_DEVICE_ID]);
	uint32_t dev_pin = blobmsg_get_u32(tb[CTL_PIN]);
	syslog(LOG_DEBUG, "dev_id = %s, dev_pin = %u", dev_id, dev_pin);

	struct blob_buf b = {};
	blob_buf_init(&b, 0);

	char msg_buf[MSG_MAXLEN];
	if (strcmp(method, TURN_ON_PIN_METHOD_NAME) == 0) {
		sprintf(msg_buf, "{\"action\":\"on\",\"pin\":%u}", pin);
	} else {
		sprintf(msg_buf, "{\"action\":\"off\",\"pin\":%u}", pin);
	}

	char response_buf[MSG_MAXLEN];
	int ret = send_msg(dev_id, msg_buf, strlen(msg_buf), response_buf,
			   sizeof(response_buf));
	switch (ret) {
	case 0:
		blobmsg_add_string(&b, NULL, response);
		break;
	case -1:
		blobmsg_add_string(&b, NULL, "Failed to open device file");
		break;
	case -2:
		blobmsg_add_string(
			&b, NULL,
			"Failed to lock the device for exclusive access");
		break;
	case -3:
		blobmsg_add_string(&b, NULL,
				   "Failed to configure serial connection");
		break;
	case -4:
		blobmsg_add_string(&b, NULL,
				   "Failed to send message to device");
		break;
	case -5:
		blobmsg_add_string(&b, NULL,
				   "Failed to get response from device");
		break;
	case -6:
		blobmsg_add_string(&b, NULL,
				   "Response is too big for the buffer");
		break;
	default:
		syslog(LOG_ERR, "Unrecognized send_msg() return code: %d", ret);
		blobmsg_add_string(&b, NULL, "Internal error");
	}
	blobmsg_add_string(&b, "second_example_key", "second example value");
	ret = ubus_send_reply(ctx, req, b.head);
	if (ret != 0) {
		syslog(LOG_ERR, "Failed to send ubus reply: %s",
		       ubus_strerror(ret));
		ret_val = ret;
		goto cleanup;
	}
cleanup:
	blob_buf_free(&b);

	return ret_val;
}

// List connected devices.
static int list_devices(struct ubus_context *ctx, struct ubus_object *obj,
			struct ubus_request_data *req, const char *method,
			struct blob_attr *msg)
{
	(void)msg;
	(void)obj;

	int ret_val = UBUS_STATUS_OK;

	syslog(LOG_DEBUG, "Received ubus message of type '%s'", method);
	char *devices[MAX_DEVS];
	unsigned int num_devs;
	if (!get_devices(devices, &num_devs, MAX_DEVS)) {
		ret_val = UBUS_STATUS_UNKNOWN_ERROR;
		goto cleanup_devs;
	}

	struct blob_buf b = {};
	blob_buf_init(&b, 0);

	void *array = blobmsg_open_array(&b, "devices");
	for (unsigned int i = 0; i < num_devs; ++i) {
		blobmsg_add_string(&b, NULL, devices[i]);
	}
	blobmsg_close_array(&b, array);
	int ret = ubus_send_reply(ctx, req, b.head);
	if (ret != 0) {
		syslog(LOG_ERR, "Failed to send ubus reply: %s",
		       ubus_strerror(ret));
		ret_val = ret;
		goto cleanup_blob;
	}

cleanup_blob:
	blob_buf_free(&b);
cleanup_devs:
	for (unsigned int i = 0; i < num_devs; ++i) {
		free(devices[i]);
	}
	return ret_val;
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
