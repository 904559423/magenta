// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _POSIX_C_SOURCE 200809L

#include "netprotocol.h"

#include <magenta/netboot.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <errno.h>
#include <stdint.h>

static uint32_t cookie = 0x12345678;

int netboot_discover(unsigned port, const char* ifname, on_device_cb callback, void* data) {
    if (!callback) {
        errno = EINVAL;
        return -1;
    }
    const char* hostname = "*";
    size_t hostname_len = strlen(hostname) + 1;

    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    inet_pton(AF_INET6, "ff02::1", &addr.sin6_addr);

    int s;
    if ((s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        fprintf(stderr, "error: cannot create socket: %s\n", strerror(errno));
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 250 * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    msg m;
    m.hdr.magic = NB_MAGIC;
    m.hdr.cookie = ++cookie;
    m.hdr.cmd = NB_QUERY;
    m.hdr.arg = 0;
    memcpy(m.data, hostname, hostname_len);

    struct ifaddrs* ifa;
    if (getifaddrs(&ifa) < 0) {
        fprintf(stderr, "error: cannot enumerate network interfaces\n");
        return -1;
    }

    for (int i = 0; i < 5; i++) {
        // transmit query on all local links
        for (; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL) {
                continue;
            }
            if (ifa->ifa_addr->sa_family != AF_INET6) {
                continue;
            }
            struct sockaddr_in6* in6 = (void*)ifa->ifa_addr;
            if (in6->sin6_scope_id == 0) {
                continue;
            }
            if (ifname && ifname[0] != 0 && strcmp(ifname, ifa->ifa_name))
                continue;
            // printf("tx %s (sid=%d)\n", ifa->ifa_name, in6->sin6_scope_id);
            size_t sz = sizeof(nbmsg) + hostname_len;
            addr.sin6_scope_id = in6->sin6_scope_id;

            int r;
            if ((r = sendto(s, &m, sz, 0, (struct sockaddr*)&addr, sizeof(addr))) != sz) {
                fprintf(stderr, "error: cannot send %d %s\n", errno, strerror(errno));
            }
        }

        // listen for replies
        struct sockaddr_in6 ra;
        socklen_t rlen = sizeof(ra);
        memset(&ra, 0, sizeof(ra));
        int r = recvfrom(s, &m, sizeof(m), 0, (void*)&ra, &rlen);
        if (r > sizeof(nbmsg)) {
            r -= sizeof(nbmsg);
            m.data[r] = 0;
            if ((m.hdr.magic == NB_MAGIC) &&
                (m.hdr.cookie == cookie) &&
                (m.hdr.cmd == NB_ACK)) {
                char tmp[INET6_ADDRSTRLEN];
                if (inet_ntop(AF_INET6, &ra.sin6_addr, tmp, sizeof(tmp)) == NULL) {
                    strcpy(tmp, "???");
                }
                // printf("found %s at %s/%d\n", (char*)m.data, tmp, ra.sin6_scope_id);
                if (strncmp("::", tmp, 2)) {
                    device_info_t info;
                    strncpy(info.nodename, (char*)m.data, MAX_NODENAME);
                    strncpy(info.inet6_addr_s, tmp, INET6_ADDRSTRLEN);
                    memcpy(&info.inet6_addr, &ra, sizeof(ra));
                    info.state = DEVICE;
                    if (!callback(&info, data)) {
                        break;
                    }
                }
            }
        }
    }
    close(s);
    return 0;
}

typedef struct netboot_open_cookie {
    struct sockaddr_in6 addr;
    char hostname[MAX_NODENAME];
    uint32_t index;
} netboot_open_cookie_t;

static bool netboot_open_callback(device_info_t* device, void* data) {
    netboot_open_cookie_t* cookie = data;
    cookie->index++;
    if (strcmp(cookie->hostname, "*") && strcmp(cookie->hostname, device->nodename)) {
        return true;
    }
    memcpy(&cookie->addr, &device->inet6_addr, sizeof(device->inet6_addr));
    return false;
}

// TODO use configuration file to prefer a listed device if *
int netboot_open(const char* hostname, const char* ifname) {
    if ((hostname == NULL) || (hostname[0] == 0)) {
        char* envname = getenv("MAGENTA_NODENAME");
        hostname = envname && envname[0] != 0 ? envname : "*";
    }
    size_t hostname_len = strlen(hostname) + 1;
    if (hostname_len > MAXSIZE) {
        errno = EINVAL;
        return -1;
    }

    netboot_open_cookie_t cookie;
    socklen_t rlen = sizeof(cookie.addr);
    memset(&(cookie.addr), 0, sizeof(cookie.addr));
    cookie.index = 0;
    strncpy(cookie.hostname, hostname, MAX_NODENAME);
    if (netboot_discover(NB_SERVER_PORT, ifname, netboot_open_callback, &cookie) < 0) {
        return -1;
    }
    // Device not found
    if (cookie.index == 0) {
        return -1;
    }

    int s;
    if ((s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        fprintf(stderr, "error: cannot create socket: %s\n", strerror(errno));
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 250 * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(s, (void*)&cookie.addr, rlen) < 0) {
        fprintf(stderr, "error: cannot connect UDP port\n");
        close(s);
        return -1;
    }
    return s;
}

// The netboot protocol ignores response packets that are invalid,
// retransmits requests if responses don't arrive in a timely
// fashion, and only returns an error upon eventual timeout or
// a specific (correctly formed) remote error packet.
int netboot_txn(int s, msg* in, msg* out, int outlen) {
    ssize_t r;

    out->hdr.magic = NB_MAGIC;
    out->hdr.cookie = ++cookie;

    int retry = 5;
resend:
    write(s, out, outlen);
    for (;;) {
        if ((r = recv(s, in, sizeof(*in), 0)) < 0) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                if (retry-- > 0) {
                    goto resend;
                }
                errno = ETIMEDOUT;
            }
            return -1;
        }
        if (r < (ssize_t)sizeof(in->hdr)) {
            fprintf(stderr, "netboot: response too short\n");
            continue;
        }
        if ((in->hdr.magic != NB_MAGIC) ||
            (in->hdr.cookie != out->hdr.cookie) ||
            (in->hdr.cmd != NB_ACK)) {
            fprintf(stderr, "netboot: bad ack header"
                            " (magic=0x%x, cookie=%x/%x, cmd=%d)\n",
                    in->hdr.magic, in->hdr.cookie, cookie, in->hdr.cmd);
            continue;
        }
        int arg = in->hdr.arg;
        if (arg < 0) {
            errno = -arg;
            return -1;
        }
        return r;
    }
}
