#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <stdlib.h>
#include <stdbool.h>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/types.h>

#include <libserialport.h>

#include "serial.h"

#define MSG_MAXLEN 50

// USB VID and PID of NodeMCU 8266V3
const int VENDOR_ID = 0x10C4;
const int PRODUCT_ID = 0xEA60;

// Gets NodeMCU 8266V3 device file names.
// devices - array to put device names in
// max_devices - array size
// num_devices - actual number of devices found
// returns true on success, false on failure
bool get_devices(char *devices[], unsigned int *num_devices,
		 const unsigned int max_devices)
{
	bool ret_val = true;
	struct sp_port **port_list;

	enum sp_return result = sp_list_ports(&port_list);
	if (result != SP_OK) {
		syslog(LOG_ERR, "sp_list_ports() failed with code %d", result);
		return false;
	}

	*num_devices = 0;
	for (unsigned int i = 0; port_list[i] != NULL; ++i) {
		struct sp_port *port = port_list[i];

		// Port name on Linux is device file name.
		char *dev_file = sp_get_port_name(port);

		if (sp_get_port_transport(port) == SP_TRANSPORT_USB) {
			int usb_vid, usb_pid;
			result = sp_get_port_usb_vid_pid(port, &usb_vid,
							 &usb_pid);
			if (result != SP_OK) {
				syslog(LOG_ERR,
				       "sp_get_port_usb_vid_pid() failed with with code %d",
				       result);
				ret_val = false;
				goto cleanup;
			}

			syslog(LOG_DEBUG, "VID: %04X, PID: %04X",
			       (unsigned)usb_vid, (unsigned)usb_pid);
			if (usb_vid == VENDOR_ID && usb_pid == PRODUCT_ID) {
				syslog(LOG_DEBUG, "Found device: %s", dev_file);
				if (*num_devices == max_devices) {
					syslog(LOG_WARNING,
					       "Devices do not fit into the provided array");
					goto cleanup;
				}
				devices[*num_devices] = strdup(dev_file);
				*num_devices += 1;
			}
		}
	}

cleanup:
	sp_free_port_list(port_list);
	return ret_val;
}

// Opens and configures the device with appropirate settings.
// Returns true on success.
static bool config_serial(const int *dev, const char *dev_name)
{
	// struct termios must be initialized with a call to tcgetattr.
	struct termios tty;
	if (tcgetattr(*dev, &tty) != 0) {
		syslog(LOG_ERR,
		       "Failed to get current port configuration for %s: %m",
		       dev_name);
		return false;
	}

	// All the casts are needed because otherwise compiler complains
	// about implicit signed to unsigned conversions.
	// Clear the parity bit.
	tty.c_cflag &= ~(tcflag_t)PARENB;
	// Use one stop bit.
	tty.c_cflag &= ~(tcflag_t)CSTOPB;
	// Use 8-bit bytes.
	tty.c_cflag &= ~(tcflag_t)CSIZE;
	tty.c_cflag |= CS8;
	// Disable RTS/CTS hardware flow control.
	tty.c_cflag &= ~(tcflag_t)CRTSCTS;
	// Enable reading and ignore control lines.
	tty.c_cflag |= CREAD | CLOCAL;
	// Read and write raw data (disable special handling of newlines
	// and other control characters)
	//tty.c_lflag &= ~(tcflag_t)ICANON;
	tty.c_lflag |= (tcflag_t)ICANON;
	// Disable echo.
	tty.c_lflag &= ~(tcflag_t)ECHO;
	// Disable erasure.
	tty.c_lflag &= ~(tcflag_t)ECHOE;
	// Disable new-line echo.
	tty.c_lflag &= ~(tcflag_t)ECHONL;
	// Disable special handling of signal chars.
	tty.c_lflag &= ~(tcflag_t)ISIG;
	// Turn off software flow control.
	tty.c_iflag &= ~(tcflag_t)(IXON | IXOFF | IXANY);
	// Disable special handling of input bytes.
	tty.c_iflag &= ~(tcflag_t)(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR |
				   IGNCR | ICRNL);
	// Disable special handling of output bytes.
	tty.c_oflag &= ~(tcflag_t)OPOST;
	// Prevent \n -> \r\n conversion.
	tty.c_oflag &= ~(tcflag_t)ONLCR;
	// Wait for input for 5 seconds max.
	tty.c_cc[VTIME] = 50;
	tty.c_cc[VMIN] = 0;
	// Set baud rate to 9600.
	cfsetispeed(&tty, B9600);
	cfsetospeed(&tty, B9600);

	// Save the settings.
	if (tcsetattr(*dev, TCSANOW, &tty) != 0) {
		syslog(LOG_ERR,
		       "Failed to set serial port settings for device %s: %m",
		       dev_name);
		return false;
	}
	// TODO: Since tcsetattr reports success if any of the provided settings
	// were applied (not neccessarily all), we should check it manually.

	return true;
}
/*
static ssize_t read_msg(int fd, char *buf, size_t bufsize)
{
	ssize_t read_bytes = read(fd, buf, bufsize - 1);
	if (read_bytes == -1) {
		syslog(LOG_ERR, "Error reading from device %s: %m", device);
		ret_val = -5;
		goto cleanup;
	} else if (read_bytes == 0) {
		syslog(LOG_DEBUG,
		       "Read 0 bytes from device, maybe disconnected?");
		return read_bytes;
	} else {
		syslog(LOG_DEBUG, "Read %zd bytes from device %s", read_bytes,
		       device);
	}
	// read() does not append null terminator.
	buf[read_bytes] = '\0';
}
*/
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
	     char *response, size_t resp_len)
{
	int ret_val = 0;
	int dev_fd = open(device, O_RDWR);
	if (dev_fd == -1) {
		syslog(LOG_ERR, "Failed to open device file %s: %m", device);
		return -1;
	}
	// Lock the device to prevent other processes from interacting with it.
	if (flock(dev_fd, LOCK_EX | LOCK_NB) == -1) {
		syslog(LOG_ERR,
		       "Failed to lock device %s for exclusive access: %m",
		       device);
		ret_val = -2;
		goto cleanup;
	}

	if (!config_serial(&dev_fd, device)) {
		ret_val = -3;
		goto cleanup;
	}

	ssize_t written = write(dev_fd, msg, msg_len);
	if (written == -1) {
		syslog(LOG_ERR, "Error writing to device %s: %m", device);
		ret_val = -4;
		goto cleanup;
	}
	if ((size_t)written < msg_len) {
		syslog(LOG_ERR, "Written %zd out of %zu bytes to device %s",
		       written, msg_len, device);
		ret_val = -4;
		goto cleanup;
	} else {
		syslog(LOG_DEBUG, "Wrote %zd bytes to device %s", written,
		       device);
	}

	char msg_buf[MSG_MAXLEN];
	ssize_t read_bytes = read(dev_fd, msg_buf, sizeof(msg_buf) - 1);
	if (read_bytes == -1) {
		syslog(LOG_ERR, "Error reading from device %s: %m", device);
		ret_val = -5;
		goto cleanup;
	} else if (read_bytes == 0) {
		syslog(LOG_DEBUG,
		       "Read 0 bytes from device, maybe disconnected?");
		ret_val = -7;
		goto cleanup;
	} else {
		syslog(LOG_DEBUG, "Read %zd bytes from device %s", read_bytes,
		       device);
	}
	// read() does not append null terminator.
	msg_buf[read_bytes] = '\0';

	if (resp_len < (size_t)read_bytes) {
		ret_val = -6;
		goto cleanup;
	}
	strcpy(response, msg_buf);

cleanup:
	close(dev_fd);
	return ret_val;
}
