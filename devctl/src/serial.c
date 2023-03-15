#include <stdint.h>
#include <sys/types.h>
#include <syslog.h>
#include <stdlib.h>

#include <libserialport.h>

#include "serial.h"

const uint16_t VENDOR_ID = 0x10C4;
const uint16_t PRODUCT_ID = 0xEA60;

void log_dev_info()
{
	// TODO: move context initialization to main.
	int ret = libusb_init(NULL);
	if (ret != 0) {
		syslog(LOG_ERR, "Failed to initialize libusb: %s",
		       libusb_strerror(ret));
		return;
	}

	libusb_device **device_list;
	ssize_t dev_count = libusb_get_device_list(NULL, &device_list);
	if (dev_count < 0) {
		syslog(LOG_ERR, "Failed to get USB device list: %s",
		       libusb_strerror(dev_count));
		return;
	}

	for (ssize_t i = 0; i < dev_count; ++i) {
		libusb_device *dev = device_list[i];
		struct libusb_device_descriptor desc;
		libusb_get_device_descriptor(dev, &desc);

		syslog(LOG_DEBUG, "Vendor:Device = %04x:%04x", desc.idVendor,
		       desc.idProduct);
		if (desc.idVendor == VENDOR_ID &&
		    desc.idProduct == PRODUCT_ID) {
			syslog(LOG_INFO, "Found ABESP 8266V3 device");
		}
	}

	libusb_free_device_list(device_list, 1);
	libusb_exit(NULL);
}
