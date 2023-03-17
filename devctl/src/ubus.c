#include <syslog.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include <libubox/blobmsg_json.h>
#include <libubus.h>
#include <json-c/json.h>

#include "ubus.h"
#include "serial.h"

#define MAX_DEVS 10
#define MSG_MAXLEN 50
#define LIST_DEVICES_METHOD_NAME "list_devices"
#define TURN_ON_PIN_METHOD_NAME "turn_on_pin"
#define TURN_OFF_PIN_METHOD_NAME "turn_off_pin"
#define TURN_ON_PIN_SUCCESS_MSG "Pin was turned on"
#define TURN_OFF_PIN_SUCCESS_MSG "Pin was turned off"

// Returned status codes:
enum devctl_status_code {
	//  0 - operation performed successfully
	DEVCTL_OK = 0,
	//  1 - operation failed
	DEVCTL_OPERATION_FAILED,
	//  2 - failed to connect to device
	DEVCTL_CONNECT_FAIL,
	//  3 - failed to send message to device
	DEVCTL_SEND_FAIL,
	//  4 - failed to get response from device
	DEVCTL_RECV_FAIL,
	//  5 - device was disconnected
	DEVCTL_DISCONNECTED,
	//  6 - failed to parse response
	DEVCTL_PARSE_FAILURE,
	//  7 - response is in unexpected format
	DEVCTL_UNEXPECTED_RESPONSE,
	//  8 - unknown error related to device
	DEVCTL_UNKNOWN_ERROR,
	//  9 - internal error, unrelated to device
	DEVCTL_INTERNAL_ERROR
};

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
	[CTL_DEVICE_ID] = { .name = "device", .type = BLOBMSG_TYPE_STRING },
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

static void add_ubus_error_response(struct blob_buf *b,
				    enum devctl_status_code status,
				    char *error_msg)
{
	blobmsg_add_u32(b, "status", (uint32_t)status);
	blobmsg_add_string(b, "error", error_msg);
}

// Parses response from device and determines if the command was
// executed successfully.
// Example responses:
//  {"response": 0, "msg": "Pin was turned on"}\r\n
//  {"response": 0, "msg": "Pin was turned off"}\r\n
// Parameters:
//  turn_on - cammand was to turn on a pin, otherwise turn off a pin
// Return codes:
//  0 - command executed successfully
//  1 - command failed, error_buf is populated with error message
// -1 - failed to parse msg, it is invalid JSON, error_buf contains error
// -2 - msg does not contain required fields, error_buf contains error
// -3 - error_buf is too small, its contents are undefined
static int parse_device_response(const char *msg, bool turn_on, char *error_buf,
				 size_t error_len)
{
	int ret_val = 0;
	enum json_tokener_error err;
	struct json_object *json = json_tokener_parse_verbose(msg, &err);
	if (json == NULL) {
		if ((size_t)snprintf(error_buf, error_len,
				     "json-c error code %u",
				     err) >= error_len) {
			return -3;
		}
		return -1;
	}
	struct json_object *status = json_object_object_get(json, "response");
	if (status == NULL || json_object_get_type(status) != json_type_int) {
		ret_val = -2;
		if ((size_t)snprintf(
			    error_buf, error_len,
			    "'status' field of type number not found in msg") >=
		    error_len) {
			ret_val = -3;
		}
		goto cleanup_json;
	}
	struct json_object *message_obj = json_object_object_get(json, "msg");
	if (message_obj == NULL) {
		ret_val = -2;
		goto cleanup_json;
	}
	const char *message = json_object_get_string(message_obj);
	if (json_object_get_int64(status) != 0) {
		// Error.
		ret_val = 1;

		if (message == NULL) {
			ret_val = -2;
		} else if ((size_t)snprintf(error_buf, error_len, "%s",
					    message) >= error_len) {
			ret_val = -3;
		}
		goto cleanup_json;
	}
	// Success, just need to check if the expected action was performed.
	if (turn_on && strcmp(message, TURN_ON_PIN_SUCCESS_MSG) != 0) {
		ret_val = 1;
		if ((size_t)snprintf(error_buf, error_len, "%s", message) >=
		    error_len) {
			ret_val = -3;
		}
		goto cleanup_json;
	} else if (!turn_on && strcmp(message, TURN_OFF_PIN_SUCCESS_MSG) != 0) {
		ret_val = 1;
		if ((size_t)snprintf(error_buf, error_len, "%s", message) >=
		    error_len) {
			ret_val = -3;
		}
		goto cleanup_json;
	}
cleanup_json:
	json_object_put(json);
	return ret_val;
}

// Status codes:
// 0 - success,
// 1 - error on our side,
// 2 - device reported error.
static int control_pin(struct ubus_context *ctx, struct ubus_object *obj,
		       struct ubus_request_data *req, const char *method,
		       struct blob_attr *msg)
{
	(void)obj;
	int ret_val = UBUS_STATUS_OK;
	bool turn_on_pin = false;
	if (strcmp(method, TURN_ON_PIN_METHOD_NAME) == 0) {
		turn_on_pin = true;
	}
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
	if (turn_on_pin) {
		sprintf(msg_buf, "{\"action\":\"on\",\"pin\":%u}", dev_pin);
	} else {
		sprintf(msg_buf, "{\"action\":\"off\",\"pin\":%u}", dev_pin);
	}

	char response_buf[MSG_MAXLEN];
	int ret = send_msg(dev_id, msg_buf, strlen(msg_buf), response_buf,
			   sizeof(response_buf));
	switch (ret) {
	case 0:
		syslog(LOG_DEBUG, "Received response '%s'", response_buf);
		ret = parse_device_response(response_buf, turn_on_pin, msg_buf,
					    sizeof(msg_buf));
		switch (ret) {
		case 0:
			blobmsg_add_u32(&b, "status", 0);
			break;
		case 1:
			add_ubus_error_response(&b, DEVCTL_OPERATION_FAILED,
						msg_buf);
			break;
		case -1:
		case -2:
			syslog(LOG_ERR,
			       "Failed to parse response from device. Response: '%s', error: %s",
			       response_buf, msg_buf);
			add_ubus_error_response(
				&b, DEVCTL_PARSE_FAILURE,
				"Failed to parse response from device");
			break;
		case -3:
			syslog(LOG_ERR,
			       "Insufficient error buffer size. Device response: %s",
			       response_buf);
			add_ubus_error_response(&b, DEVCTL_UNKNOWN_ERROR,
						"Insufficient buffer size");
			break;
		default:
			syslog(LOG_ERR,
			       "Unrecognized parse_device_response() return code: %d",
			       ret);
			add_ubus_error_response(&b, DEVCTL_INTERNAL_ERROR,
						"Internal error");
		}
		break;
	case -1:
		add_ubus_error_response(&b, DEVCTL_CONNECT_FAIL,
					"Failed to open device file");
		break;
	case -2:
		add_ubus_error_response(
			&b, DEVCTL_CONNECT_FAIL,
			"Failed to lock the device for exclusive access");
		break;
	case -3:
		add_ubus_error_response(
			&b, DEVCTL_CONNECT_FAIL,
			"Failed to configure serial connection");
		break;
	case -4:
		add_ubus_error_response(&b, DEVCTL_SEND_FAIL,
					"Failed to send message to device");
		break;
	case -5:
		add_ubus_error_response(&b, DEVCTL_RECV_FAIL,
					"Failed to get response from device");
		break;
	case -6:
		add_ubus_error_response(
			&b, DEVCTL_INTERNAL_ERROR,
			"Device response is too big for the buffer");
		break;
	case -7:
		add_ubus_error_response(&b, DEVCTL_DISCONNECTED,
					"Device was disconnected");
		break;
	default:
		syslog(LOG_ERR, "Unrecognized send_msg() return code: %d", ret);
		add_ubus_error_response(&b, DEVCTL_INTERNAL_ERROR,
					"Internal error");
	}
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
