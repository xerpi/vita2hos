#ifndef NETLOG_H
#define NETLOG_H

#include <stdint.h>

/* Port 28771 (nc -ulk 28771) */
#define MULTICAST_ADDR "224.0.0.1"
#define PORT           28771

int netlog_init(void);

#endif