#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <switch.h>
#include <sys/iosupport.h>
#include <sys/socket.h>
#include <unistd.h>

#include "netlog.h"

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

    /* Redirect stdout */
    fflush(stdout);
    dup2(sock, STDOUT_FILENO);

    /* Redirect stderr */
    fflush(stderr);
    dup2(sock, STDERR_FILENO);

    /* Close the original socket descriptor */
    close(sock);

    return 0;
}