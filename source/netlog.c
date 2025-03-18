#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <switch.h>
#include <sys/iosupport.h>
#include <sys/socket.h>
#include <unistd.h>

#include "netlog.h"

static int g_sock = -1;

int netlog_init(void)
{
    int ret, sock, flags;
    unsigned char ttl;
    struct sockaddr_in dest_addr;

    /* Create a UDP socket */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return sock;

    /* Set to non-blocking */
    flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) {
        close(sock);
        return flags;
    }

    ret = fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    if (ret != 0) {
        close(sock);
        return ret;
    }

    /* Set up the destination address (multicast group) */
    dest_addr = (struct sockaddr_in){
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = inet_addr(MULTICAST_ADDR),
    };

    /* Optional: Set TTL (Time To Live) for multicast packets */
    ttl = 1; /* Restrict to local network */
    ret = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    if (ret < 0) {
        close(sock);
        return ret;
    }

    /* Connect the socket to the multicast group */
    ret = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (ret < 0) {
        close(sock);
        return ret;
    }

    g_sock = sock;

    return 0;
}

int netlog_deinit(void)
{
    if (g_sock >= 0) {
        close(g_sock);
        g_sock = -1;
    }

    return 0;
}

ssize_t netlog_write(const void *buf, size_t size)
{
    return write(g_sock, buf, size);
}