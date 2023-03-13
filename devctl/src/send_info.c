#include <syslog.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include <libubox/blobmsg_json.h>
#include <libubus.h>

#include "send_info.h"

struct memory_data {
	uint64_t total;
	uint64_t free;
	uint64_t shared;
	uint64_t buffered;
	uint64_t available;
	uint64_t cached;
};

struct ubus_response {
	bool successful;
	char *data_json;
	struct memory_data data;
};

enum {
	TOTAL_MEMORY,
	FREE_MEMORY,
	SHARED_MEMORY,
	BUFFERED_MEMORY,
	AVAILABLE_MEMORY,
	CACHED_MEMORY,
	__MEMORY_MAX,
};

enum {
	MEMORY_DATA,
	__INFO_MAX,
};

static const struct blobmsg_policy memory_policy[__MEMORY_MAX] = {
	[TOTAL_MEMORY] = { .name = "total", .type = BLOBMSG_TYPE_INT64 },
	[FREE_MEMORY] = { .name = "free", .type = BLOBMSG_TYPE_INT64 },
	[SHARED_MEMORY] = { .name = "shared", .type = BLOBMSG_TYPE_INT64 },
	[BUFFERED_MEMORY] = { .name = "buffered", .type = BLOBMSG_TYPE_INT64 },
	[AVAILABLE_MEMORY] = { .name = "available",
			       .type = BLOBMSG_TYPE_INT64 },
	[CACHED_MEMORY] = { .name = "cached", .type = BLOBMSG_TYPE_INT64 }
};

static const struct blobmsg_policy info_policy[__INFO_MAX] = {
	[MEMORY_DATA] = { .name = "memory", .type = BLOBMSG_TYPE_TABLE },
};

// req contains request info
// req->priv is data passed to priv parameter of ubus_invoke
static void ubus_received_data_process_callback(struct ubus_request *req,
						int type, struct blob_attr *msg)
{
	(void)type;

	struct ubus_response *resp = (struct ubus_response *)req->priv;
	resp->successful = false;
	struct memory_data *data = &resp->data;
	struct blob_attr *tb[__INFO_MAX];
	struct blob_attr *memory[__MEMORY_MAX];

	if (blobmsg_parse(info_policy, __INFO_MAX, tb, blob_data(msg),
			  blob_len(msg)) != 0) {
		syslog(LOG_ERR, "Failed to parse answer from ubus");
		return;
	}

	if (tb[MEMORY_DATA] == NULL) {
		syslog(LOG_ERR, "No memory data received from ubus");
		return;
	}

	if (blobmsg_parse(memory_policy, __MEMORY_MAX, memory,
			  blobmsg_data(tb[MEMORY_DATA]),
			  blobmsg_data_len(tb[MEMORY_DATA])) != 0) {
		syslog(LOG_ERR, "Failed to parse memory data received");
		return;
	}

	resp->successful = true;
	resp->data_json = blobmsg_format_json(tb[MEMORY_DATA], true);
	data->available = blobmsg_get_u64(memory[AVAILABLE_MEMORY]);
	data->free = blobmsg_get_u64(memory[FREE_MEMORY]);
	data->cached = blobmsg_get_u64(memory[CACHED_MEMORY]);
	data->total = blobmsg_get_u64(memory[TOTAL_MEMORY]);
	data->shared = blobmsg_get_u64(memory[SHARED_MEMORY]);
	data->buffered = blobmsg_get_u64(memory[BUFFERED_MEMORY]);
}

bool send_info(tuya_mqtt_context_t *tuya_ctx, struct ubus_context *ubus_ctx)
{
	uint32_t id;
	int ret_val = ubus_lookup_id(ubus_ctx, "system", &id);
	if (ret_val != UBUS_STATUS_OK) {
		syslog(LOG_ERR, "'system' not found in ubus: %s",
		       ubus_strerror(ret_val));
		return false;
	}

	struct ubus_response resp;
	// int ubus_invoke(struct ubus_context *ctx, uint32_t obj, const char *method,
	//	    struct blob_attr *msg, ubus_data_handler_t cb, void *priv,
	//	    int timeout)
	ret_val = ubus_invoke(ubus_ctx, id, "info", NULL,
			      ubus_received_data_process_callback, &resp,
			      10000);
	if (ret_val != UBUS_STATUS_OK) {
		syslog(LOG_ERR, "Failed to get memory info from ubus: %s",
		       ubus_strerror(ret_val));
		return false;
	} else if (!resp.successful) {
		return false;
	}

	// mem_data now contains info about device memory
	syslog(LOG_DEBUG, "Memory info from ubus: %s", resp.data_json);

	// All Tuya error codes are < 0,
	// they are defined in tuya-iot-core-sdk/utils/tuya_error_code.h
	if (tuyalink_thing_property_report_with_ack(tuya_ctx, NULL,
						    resp.data_json) < 0) {
		syslog(LOG_ERR,
		       "Failed to send message to Tuya. Message data: %s",
		       resp.data_json);
		return false;
	}

	return true;
}
