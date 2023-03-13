#ifndef SIGNALS_H
#define SIGNALS_H

#include <signal.h>

extern volatile sig_atomic_t keep_running;

void signal_handler(int signum);
void set_up_signal_handler(void);

#endif
