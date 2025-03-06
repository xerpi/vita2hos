#include <arpa/inet.h>
#include <stdio.h>
#include <switch.h>
#include <sys/iosupport.h>
#include <sys/socket.h>
#include <unistd.h>

#include "netlog.h"

static int g_socket = -1;
static struct sockaddr_in g_dest_addr;

ssize_t multicast_udp_send(const char *buffer, size_t buffer_len)
{
    return sendto(g_socket, buffer, buffer_len, 0, (struct sockaddr *)&g_dest_addr,
                  sizeof(g_dest_addr));
}

static ssize_t devoptab_netlog_write(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    return multicast_udp_send(ptr, len);
}

static const devoptab_t devoptab_netlog = {
    .name = "netlog",
    .write_r = devoptab_netlog_write,
};

int netlog_init(void)
{
    int ret, sock;
    unsigned char ttl;

    /* Create a UDP socket */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return sock;

    /* Set up the destination address (multicast group) */
    g_dest_addr = (struct sockaddr_in){ .sin_family = AF_INET,
                                        .sin_port = htons(PORT),
                                        .sin_addr.s_addr = inet_addr(MULTICAST_ADDR) };

    /* Optional: Set TTL (Time To Live) for multicast packets */
    ttl = 1; /* Restrict to local network */
    ret = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    if (ret < 0) {
        close(sock);
        return ret;
    }

    g_socket = sock;

    devoptab_list[STD_OUT] = &devoptab_netlog;
    devoptab_list[STD_ERR] = &devoptab_netlog;
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    return 0;
}