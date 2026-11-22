// Minimal BSD socket service for MK8DX local wireless play. Guest sockaddr starts with len,family.
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>

#include "../game_sites.h"
#include "../hle.h"
#include "../ipc.h"

static _Atomic unsigned packet_log_count;

static int guest_addr(const IpcBuf *b, struct sockaddr_in *out) {
    if (!b || !b->addr || b->size < 8) return -1;
    const uint8_t *p = (const uint8_t *)(uintptr_t)b->addr;
    if (p[1] != 2) return -1;
    memset(out, 0, sizeof *out);
    out->sin_family = AF_INET;
    memcpy(&out->sin_port, p + 2, 2);
    memcpy(&out->sin_addr, p + 4, 4);
    return 0;
}

static void bsd_reply(IpcResp *resp, int ret, int err) {
    OUT(int32_t, 0) = ret;
    OUT(uint32_t, 4) = err;
    resp_size(resp, 8);
}

SERVICE("bsd:u", bsd_user) {
    (void)s;
    if (rt_is_botw()) return;
    int fd = req->data_size >= 4 ? IN(int32_t, 0) : -1;
    int result = 0, err = 0;
    switch (req->cmd) {
    case 0: case 1: break;  // RegisterClient / StartMonitoring
    case 2: {  // Socket
        int domain = IN(uint32_t, 0), type = IN(uint32_t, 4), protocol = IN(uint32_t, 8);
        result = (domain == 2 && (type == 1 || type == 2)) ? socket(AF_INET, type, protocol) : -1;
        if (result >= 0) fcntl(result, F_SETFL, fcntl(result, F_GETFL, 0) | O_NONBLOCK);
        else err = errno ? errno : EINVAL;
        LOG("[bsd] socket domain %d type %d proto %d -> %d (%d)\n", domain, type, protocol, result, err);
        break;
    }
    case 13: {  // Bind
        struct sockaddr_in addr;
        if (guest_addr(in_buf(req, 0), &addr) < 0) { result = -1; err = EINVAL; break; }
        if (addr.sin_addr.s_addr == INADDR_ANY) {
            const char *local_ip = getenv("SAVAGEWIND_LDN_IPV4");
            // Separate loopback instances need distinct binds. On a real LAN, keep
            // INADDR_ANY so the socket can receive subnet broadcast packets.
            if (local_ip && strncmp(local_ip, "127.", 4) == 0)
                inet_pton(AF_INET, local_ip, &addr.sin_addr);
        }
        result = bind(fd, (struct sockaddr *)&addr, sizeof addr);
        if (result < 0) err = errno;
        LOG("[bsd] bind fd %d port %u -> %d (%d)\n", fd, ntohs(addr.sin_port), result, err);
        break;
    }
    case 21: {  // SetSockOpt
        int level = IN(uint32_t, 4), opt = IN(uint32_t, 8);
        IpcBuf *b = in_buf(req, 0);
        int host_opt = opt == 4 ? SO_REUSEADDR : opt == 0x20 ? SO_BROADCAST :
                       opt == 0x1001 ? SO_SNDBUF : opt == 0x1002 ? SO_RCVBUF : -1;
        if (level == 0xffff && host_opt >= 0 && b && b->addr)
            result = setsockopt(fd, SOL_SOCKET, host_opt, (void *)(uintptr_t)b->addr, b->size);
        else result = 0;  // unsupported options are ignored for now
        if (result < 0) err = errno;
        LOG("[bsd] setsockopt fd %d level %d opt %#x -> %d (%d)\n", fd, level, opt, result, err);
        break;
    }
    case 9: {  // RecvFrom (nonblocking: the game's worker polls)
        IpcBuf *data = out_buf(req, 0), *addr_out = out_buf(req, 1);
        struct sockaddr_in addr = {0};
        socklen_t addr_len = sizeof addr;
        result = data && data->addr ? recvfrom(fd, (void *)(uintptr_t)data->addr, data->size, MSG_DONTWAIT,
                                               (struct sockaddr *)&addr, &addr_len) : -1;
        if (result < 0) err = data ? errno : EINVAL;
        if (result >= 0 && addr_out && addr_out->addr && addr_out->size >= 8) {
            uint8_t *p = (uint8_t *)(uintptr_t)addr_out->addr;
            memset(p, 0, addr_out->size);
            p[0] = 16; p[1] = 2;
            memcpy(p + 2, &addr.sin_port, 2);
            memcpy(p + 4, &addr.sin_addr, 4);
        }
        if (result > 0 && atomic_fetch_add(&packet_log_count, 1) < 40)
            LOG("[bsd] recvfrom fd %d bytes %d from %s:%u\n", fd, result,
                inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
        bsd_reply(resp, result, err);
        OUT(uint32_t, 8) = result >= 0 ? 16 : 0;
        resp_size(resp, 12);
        return;
    }
    case 8: {  // Recv
        IpcBuf *data = out_buf(req, 0);
        result = data && data->addr ? recv(fd, (void *)(uintptr_t)data->addr, data->size, MSG_DONTWAIT) : -1;
        if (result < 0) err = data ? errno : EINVAL;
        break;
    }
    case 10: case 24: {  // Send / Write
        IpcBuf *data = in_buf(req, 0);
        result = data && data->addr ? send(fd, (void *)(uintptr_t)data->addr, data->size, MSG_DONTWAIT) : -1;
        if (result < 0) err = data ? errno : EINVAL;
        break;
    }
    case 11: {  // SendTo
        IpcBuf *data = in_buf(req, 0);
        struct sockaddr_in addr;
        if (!data || !data->addr || guest_addr(in_buf(req, 1), &addr) < 0) {
            result = -1; err = EINVAL; break;
        }
        // Linux does not deliver directed loopback broadcasts to sockets bound to 127.0.0.2.
        // Route that local test case to the other instance; real LAN broadcasts keep their address.
        if (ntohl(addr.sin_addr.s_addr) == 0x7fffffff) {
            const char *local_ip = getenv("SAVAGEWIND_LDN_IPV4");
            const char *peer = local_ip && strcmp(local_ip, "127.0.0.2") == 0 ? "127.0.0.1" : "127.0.0.2";
            inet_pton(AF_INET, peer, &addr.sin_addr);
        }
        result = sendto(fd, (void *)(uintptr_t)data->addr, data->size, MSG_DONTWAIT,
                        (struct sockaddr *)&addr, sizeof addr);
        if (result < 0) err = errno;
        if (atomic_fetch_add(&packet_log_count, 1) < 40)
            LOG("[bsd] sendto fd %d bytes %d to %s:%u (%d)\n", fd, result,
                inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), err);
        break;
    }
    case 14: {  // Connect
        struct sockaddr_in addr;
        if (guest_addr(in_buf(req, 0), &addr) < 0) { result = -1; err = EINVAL; break; }
        result = connect(fd, (struct sockaddr *)&addr, sizeof addr);
        if (result < 0) err = errno;
        break;
    }
    case 20: {  // Fcntl GETFL/SETFL
        int cmd = IN(int32_t, 4), arg = IN(int32_t, 8);
        result = cmd == 3 ? fcntl(fd, F_GETFL) : cmd == 4 ? fcntl(fd, F_SETFL, arg) : -1;
        if (result < 0) err = cmd == 3 || cmd == 4 ? errno : EINVAL;
        break;
    }
    case 22: result = shutdown(fd, IN(int32_t, 4)); if (result < 0) err = errno; break;
    case 26: result = close(fd); if (result < 0) err = errno; break;
    default: LOG("[bsd] unhandled cmd %u\n", req->cmd); return;
    }
    bsd_reply(resp, result, err);
}
