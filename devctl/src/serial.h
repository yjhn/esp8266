#ifndef SERIAL_H
#define SERIAL_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

// Gets ABESP 8266V3 device file names.
// devices - array to put device names in
// max_devices - array size
// num_devices - actual number of devices found
// returns true on success, false on failure
bool get_devices(char *devices[], unsigned int *num_devices,
		 unsigned int max_devices);

// Returns:
// 0 on success
// -1 if failed to open device file
// -2 if failed to lock the device for exclusive access
// -3 if failed to configure serial connection
// -4 if writing to device fails
// -5 if reading from device fails
// -6 if the response buffer is too small
// -7 if device was disconnected
int send_msg(const char *device, const char *msg, const size_t msg_len,
	     char *response, size_t resp_len);

#endif
