#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "signals.h"

volatile sig_atomic_t keep_running = 1;

void signal_handler(int signum)
{
	// Avoid unused parameter warning.
	(void)signum;
	keep_running = 0;
}

void set_up_signal_handler(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = signal_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
}
