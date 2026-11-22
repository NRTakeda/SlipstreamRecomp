// Local wireless IPC entry point. Network operations are added as they are exercised by MK8DX.
#include "../game_sites.h"
#include "../hle.h"
#include "../ipc.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/socket.h>

typedef struct {
    unsigned state;
    uint32_t event_handle;
    uint8_t info[0x480];
    uint8_t advertise[0x180];
    uint16_t advertise_size;
    KEvent *event;
    pthread_mutex_t info_lock;
    pthread_t responder;
    int discovery_fd;
    _Atomic int stop;
    bool responder_running;
    // Host: the stations that joined (their control address) and when each was last heard from.
    struct sockaddr_in stations[7];
    uint64_t station_seen[7];
    int nstations;
    // Station: the control socket kept open to the host after Connect, its heartbeat thread, the host's address.
    int ctl_fd;
    pthread_t guest;
    _Atomic int guest_stop;
    bool guest_running;
    struct sockaddr_in host_addr;
    _Atomic unsigned disconnect_reason;
} LdnUser;

// A peer not heard from for this long is gone (closed game, sleep, Wi-Fi lost): the other side leaves the room at once
// instead of waiting on the game's own long timeouts behind a black screen.
#define LDN_PEER_TIMEOUT_NS 4000000000ull

static uint64_t ldn_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void ldn_signal(LdnUser *user) {
    if (user->event) { klock(); kernel_signal(user->event); kunlock(); }
}

static bool same_addr(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

// Host: forget station i (caller holds info_lock) and mark its node slot free.
static void host_drop_station(LdnUser *user, int i, const char *why) {
    uint8_t *ip = (uint8_t *)&user->stations[i].sin_addr.s_addr;
    LOG("[ldn] station %u.%u.%u.%u left (%s)\n", ip[0], ip[1], ip[2], ip[3], why);
    user->stations[i] = user->stations[--user->nstations];
    user->station_seen[i] = user->station_seen[user->nstations];
    if (!user->nstations) { memset(user->info + 0xa8, 0, 0x40); user->info[0x67] = 1; }
}

static _Atomic unsigned poll_log_count, scan_log_count;

static uint16_t discovery_port(void) {
    const char *value = getenv("SAVAGEWIND_LDN_PORT");
    if (value && *value) {
        char *end;
        long n = strtol(value, &end, 10);
        if (!*end && n > 1024 && n <= 65535) return (uint16_t)n;
    }
    return 24874;
}

static void guest_ipv4(uint8_t out[4], const char *text, const char *fallback) {
    uint8_t net[4];
    if (!text || inet_pton(AF_INET, text, net) != 1) inet_pton(AF_INET, fallback, net);
    for (int i = 0; i < 4; i++) out[i] = net[3 - i];
}

static void *ldn_respond(void *arg) {
    LdnUser *user = arg;
    uint8_t query[4 + 0x40];
    uint8_t reply[4 + 0x480];
    memcpy(reply, "SWA1", 4);
    while (!atomic_load(&user->stop)) {
        struct sockaddr_in from;
        socklen_t n = sizeof from;
        ssize_t got = recvfrom(user->discovery_fd, query, sizeof query, 0, (struct sockaddr *)&from, &n);
        if (got == 4 && !memcmp(query, "SWQ1", 4)) {
            pthread_mutex_lock(&user->info_lock);
            memcpy(reply + 4, user->info, sizeof user->info);
            pthread_mutex_unlock(&user->info_lock);
            sendto(user->discovery_fd, reply, sizeof reply, 0, (struct sockaddr *)&from, n);
        } else if (got == sizeof query && !memcmp(query, "SWC1", 4)) {
            uint8_t *node = query + 4;
            pthread_mutex_lock(&user->info_lock);
            memcpy(user->info + 0xa8, node, 0x40);
            user->info[0xaf] = 1;
            user->info[0x67] = 2;
            memcpy(reply + 4, user->info, sizeof user->info);
            pthread_mutex_unlock(&user->info_lock);
            memcpy(reply, "SWJ1", 4);
            sendto(user->discovery_fd, reply, sizeof reply, 0, (struct sockaddr *)&from, n);
            pthread_mutex_lock(&user->info_lock);
            int k = 0;
            while (k < user->nstations && !same_addr(&user->stations[k], &from)) k++;
            if (k == user->nstations && k < 7) user->stations[user->nstations++] = from;
            if (k < 7) user->station_seen[k] = ldn_now();
            pthread_mutex_unlock(&user->info_lock);
            LOG("[ldn] station joined: %u.%u.%u.%u\n", node[3], node[2], node[1], node[0]);
            ldn_signal(user);
            memcpy(reply, "SWA1", 4);
        } else if (got == 4 && (!memcmp(query, "SWH1", 4) || !memcmp(query, "SWL1", 4))) {
            bool leave = !memcmp(query, "SWL1", 4), dropped = false;
            pthread_mutex_lock(&user->info_lock);
            for (int k = 0; k < user->nstations; k++)
                if (same_addr(&user->stations[k], &from)) {
                    if (leave) { host_drop_station(user, k, "disconnected"); dropped = true; }
                    else user->station_seen[k] = ldn_now();
                    break;
                }
            pthread_mutex_unlock(&user->info_lock);
            if (!leave) sendto(user->discovery_fd, "SWH1", 4, 0, (struct sockaddr *)&from, n);
            if (dropped) ldn_signal(user);
        }
        bool dropped = false;
        uint64_t now = ldn_now();
        pthread_mutex_lock(&user->info_lock);
        for (int k = user->nstations - 1; k >= 0; k--)
            if (now - user->station_seen[k] > LDN_PEER_TIMEOUT_NS) { host_drop_station(user, k, "no signal"); dropped = true; }
        pthread_mutex_unlock(&user->info_lock);
        if (dropped) ldn_signal(user);
    }
    return NULL;
}

static void ldn_stop(LdnUser *user) {
    if (!user->responder_running) return;
    pthread_mutex_lock(&user->info_lock);
    for (int k = 0; k < user->nstations; k++)  // the room closes: each station leaves it now
        for (int r = 0; r < 3; r++)
            sendto(user->discovery_fd, "SWD1", 4, 0, (struct sockaddr *)&user->stations[k], sizeof user->stations[k]);
    user->nstations = 0;
    pthread_mutex_unlock(&user->info_lock);
    atomic_store(&user->stop, 1);
    pthread_join(user->responder, NULL);
    close(user->discovery_fd);
    user->discovery_fd = -1;
    user->responder_running = false;
}

// Station: heartbeat to the host once a second; the host closing the room (SWD1) or going silent puts this station
// back to the Station state with a disconnect reason and signals the game, like a real local-wireless drop.
static void *ldn_guest(void *arg) {
    LdnUser *user = arg;
    uint64_t seen = ldn_now(), sent = 0;
    unsigned reason = 0;
    while (!atomic_load(&user->guest_stop)) {
        uint64_t now = ldn_now();
        if (now - sent >= 1000000000ull) {
            sendto(user->ctl_fd, "SWH1", 4, 0, (struct sockaddr *)&user->host_addr, sizeof user->host_addr);
            sent = now;
        }
        uint8_t msg[8];
        ssize_t got = recv(user->ctl_fd, msg, sizeof msg, 0);
        if (got == 4 && !memcmp(msg, "SWH1", 4)) seen = ldn_now();
        else if (got == 4 && !memcmp(msg, "SWD1", 4)) { reason = 3; break; }  // DestroyedByUser
        if (ldn_now() - seen > LDN_PEER_TIMEOUT_NS) { reason = 6; break; }      // SignalLost
    }
    if (reason) {
        pthread_mutex_lock(&user->info_lock);
        user->state = 4;
        atomic_store(&user->disconnect_reason, reason);
        pthread_mutex_unlock(&user->info_lock);
        LOG("[ldn] disconnected from the room (%s)\n", reason == 3 ? "host closed it" : "no signal");
        ldn_signal(user);
    }
    return NULL;
}

static void guest_stop(LdnUser *user, bool tell_host) {
    if (user->guest_running) {
        atomic_store(&user->guest_stop, 1);
        pthread_join(user->guest, NULL);
        user->guest_running = false;
    }
    if (user->ctl_fd >= 0) {
        if (tell_host)
            for (int r = 0; r < 3; r++)
                sendto(user->ctl_fd, "SWL1", 4, 0, (struct sockaddr *)&user->host_addr, sizeof user->host_addr);
        close(user->ctl_fd);
        user->ctl_fd = -1;
    }
}

static void ldn_close(Service *s) {
    LdnUser *user = s->state;
    guest_stop(user, true);
    ldn_stop(user);
    pthread_mutex_destroy(&user->info_lock);
    free(user);
}

static void ldn_listen(LdnUser *user) {
    if (user->responder_running) return;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct timeval timeout = {.tv_sec = 0, .tv_usec = 200000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(discovery_port()), .sin_addr.s_addr = INADDR_ANY};
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        LOG("[ldn] discovery bind port %u failed\n", discovery_port());
        close(fd);
        return;
    }
    user->discovery_fd = fd;
    atomic_store(&user->stop, 0);
    user->responder_running = pthread_create(&user->responder, NULL, ldn_respond, user) == 0;
    if (!user->responder_running) close(fd);
    else LOG("[ldn] discovery listening on UDP %u\n", discovery_port());
}

static void ldn_scan(LdnUser *user, IpcReq *req, IpcResp *resp) {
    (void)user;
    OUT(int16_t, 0) = 0;
    resp_size(resp, 2);
    IpcBuf *out = out_buf(req, 0);
    if (!out || !out->addr || out->size < 0x480) return;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;
    struct timeval timeout = {.tv_sec = 0, .tv_usec = 35000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    const char *peer = getenv("SAVAGEWIND_LDN_PEER");
    if (!peer || !*peer) peer = "127.0.0.1";
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(discovery_port())};
    if (inet_pton(AF_INET, peer, &addr.sin_addr) == 1) {
        sendto(fd, "SWQ1", 4, 0, (struct sockaddr *)&addr, sizeof addr);
        uint8_t reply[4 + 0x480];
        ssize_t n = recv(fd, reply, sizeof reply, 0);
        if (n == sizeof reply && !memcmp(reply, "SWA1", 4)) {
            memcpy((void *)(uintptr_t)out->addr, reply + 4, 0x480);
            OUT(int16_t, 0) = 1;
            if (atomic_fetch_add(&scan_log_count, 1) < 8)
                LOG("[ldn] scan found room via %s:%u\n", peer, discovery_port());
        }
    }
    close(fd);
}

static void ldn_connect(LdnUser *user, IpcReq *req, IpcResp *resp) {
    if (req->data_size < 0x7c) { resp->result = MAKE_RESULT(203, 96); return; }
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { resp->result = MAKE_RESULT(203, 31); return; }
    struct timeval timeout = {.tv_sec = 0, .tv_usec = 500000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    const char *peer = getenv("SAVAGEWIND_LDN_PEER");
    if (!peer || !*peer) peer = "127.0.0.1";
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(discovery_port())};
    uint8_t request[4 + 0x40] = {0};
    memcpy(request, "SWC1", 4);
    uint8_t *node = request + 4;
    const char *local_ip = getenv("SAVAGEWIND_LDN_IPV4");
    guest_ipv4(node, local_ip, "127.0.0.2");
    node[4] = 2; node[5] = 0x53; node[6] = 0x57; node[7] = 0x4d; node[8] = 0x4b; node[9] = 9;
    node[10] = 1; node[11] = 1;
    memcpy(node + 12, req->data + 0x44, 0x21);
    memcpy(node + 0x2e, req->data + 0x74, 2);
    if (inet_pton(AF_INET, peer, &addr.sin_addr) == 1) {
        sendto(fd, request, sizeof request, 0, (struct sockaddr *)&addr, sizeof addr);
        uint8_t reply[4 + 0x480];
        ssize_t n = recv(fd, reply, sizeof reply, 0);
        if (n == sizeof reply && !memcmp(reply, "SWJ1", 4)) {
            memcpy(user->info, reply + 4, sizeof user->info);
            user->state = 5;
            LOG("[ldn] connected to room %s:%u\n", peer, discovery_port());
            guest_stop(user, false);
            struct timeval tick = {.tv_sec = 0, .tv_usec = 200000};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tick, sizeof tick);
            user->ctl_fd = fd;
            user->host_addr = addr;
            atomic_store(&user->disconnect_reason, 0);
            atomic_store(&user->guest_stop, 0);
            user->guest_running = pthread_create(&user->guest, NULL, ldn_guest, user) == 0;
            return;
        }
    }
    resp->result = MAKE_RESULT(203, 31);
    LOG("[ldn] connect to %s:%u failed\n", peer, discovery_port());
    close(fd);
}

static void copy_out(IpcReq *req, int index, const void *src, size_t size) {
    IpcBuf *b = out_buf(req, index);
    if (b && b->addr && b->size >= size) memcpy((void *)(uintptr_t)b->addr, src, size);
}

static void ldn_created(LdnUser *user, IpcReq *req) {
    if (req->data_size < 0x98) return;
    uint8_t *info = user->info;
    const uint8_t *config = req->data;
    memset(info, 0, sizeof user->info);
    memcpy(info, config + 0x78, 0x10);  // IntentId
    uint64_t session = (uint64_t)time(NULL) ^ ((uint64_t)getpid() << 32);
    memcpy(info + 0x10, &session, 8);
    session = session * 0x9e3779b97f4a7c15ull + 1;
    memcpy(info + 0x18, &session, 8);
    info[0x20] = 2; info[0x21] = 0x53; info[0x22] = 0x57; info[0x23] = 0x4d; info[0x24] = 0x4b; info[0x25] = 8;
    info[0x26] = 8; memcpy(info + 0x27, "SW-MK8DX", 8);
    memcpy(info + 0x48, config + 0x88, 2);  // WifiChannel
    if (!info[0x48]) info[0x48] = 6;
    info[0x4a] = 2; info[0x4b] = 2;         // Good, Ldn
    memcpy(info + 0x60, config, 2);         // SecurityMode
    info[0x66] = config[0x8a];             // max nodes
    info[0x67] = 1;                        // host
    const char *ip_text = getenv("SAVAGEWIND_LDN_IPV4");
    guest_ipv4(info + 0x68, ip_text, "127.0.0.1");
    memcpy(info + 0x6c, info + 0x20, 6);
    info[0x72] = 0; info[0x73] = 1;
    memcpy(info + 0x74, config + 0x44, 0x21);  // host name
    memcpy(info + 0x96, config + 0x8c, 2);    // version
    memcpy(info + 0x26a, &user->advertise_size, 2);
    memcpy(info + 0x26c, user->advertise, user->advertise_size);
    user->state = 3;
    ldn_listen(user);
    LOG("[ldn] network created id %llx, nodes %u, host %u.%u.%u.%u\n",
        (unsigned long long)*(uint64_t *)info, info[0x66], info[0x6b], info[0x6a], info[0x69], info[0x68]);
}

static void ldn_user(Service *s, IpcReq *req, IpcResp *resp) {
    LdnUser *user = s->state;
    if ((req->cmd != 0 && req->cmd != 1 && req->cmd != 2 && req->cmd != 3 && req->cmd != 102) ||
        atomic_fetch_add(&poll_log_count, 1) < 20)
        LOG("[ldn] IUserLocalCommunicationService cmd %u (in %u bytes, x%d a%d b%d c%d)\n",
            req->cmd, req->data_size, req->nx, req->na, req->nb, req->nc);
    switch (req->cmd) {
    case 400: case 402: user->state = 1; break;  // Initialize / Initialize2
    case 401: guest_stop(user, true); ldn_stop(user); user->state = 0; break;  // Finalize
    case 0: OUT(uint32_t, 0) = user->state; resp_size(resp, 4); break;
    case 1:  // GetNetworkInfo
        pthread_mutex_lock(&user->info_lock);
        copy_out(req, 0, user->info, sizeof user->info);
        pthread_mutex_unlock(&user->info_lock);
        break;
    case 2: {  // GetIpv4Address and subnet mask
        const char *ip_text = getenv("SAVAGEWIND_LDN_IPV4");
        guest_ipv4(resp->data, ip_text, "127.0.0.1");
        const char *mask = getenv("SAVAGEWIND_LDN_MASK");
        guest_ipv4(resp->data + 4, mask, resp->data[3] == 127 ? "255.0.0.0" : "255.255.255.0");
        resp_size(resp, 8);
        break;
    }
    case 3: OUT(int16_t, 0) = (int16_t)atomic_load(&user->disconnect_reason); resp_size(resp, 2); break;  // GetDisconnectReason
    case 4:  // GetSecurityParameter
        memcpy(resp->data + 16, user->info + 0x10, 16);
        resp_size(resp, 32);
        break;
    case 5:  // GetNetworkConfig
        memcpy(resp->data, user->info, 16);
        memcpy(resp->data + 0x10, user->info + 0x48, 2);
        resp->data[0x12] = user->info[0x66];
        memcpy(resp->data + 0x14, user->info + 0x96, 2);
        resp_size(resp, 0x20);
        break;
    case 101:  // GetNetworkInfoLatestUpdate
        pthread_mutex_lock(&user->info_lock);
        copy_out(req, 0, user->info, sizeof user->info);
        pthread_mutex_unlock(&user->info_lock);
        if (out_buf(req, 1) && out_buf(req, 1)->addr)
            memset((void *)(uintptr_t)out_buf(req, 1)->addr, 0, out_buf(req, 1)->size);
        break;
    case 200: user->state = 2; break;  // OpenAccessPoint
    case 201: ldn_stop(user); user->state = 1; break;  // CloseAccessPoint
    case 202: ldn_created(user, req); break;
    case 204: ldn_stop(user); user->state = 2; break;
    case 300: user->state = 4; break;  // OpenStation
    case 301: guest_stop(user, true); user->state = 1; break;  // CloseStation
    case 102: case 103: ldn_scan(user, req, resp); break;
    case 302: ldn_connect(user, req, resp); break;
    case 304: guest_stop(user, true); user->state = 4; break;  // Disconnect
    case 206: {  // SetAdvertiseData
        IpcBuf *b = in_buf(req, 0);
        if (b && b->addr && b->size <= 0x180) {
            pthread_mutex_lock(&user->info_lock);
            user->advertise_size = b->size;
            memcpy(user->advertise, (void *)(uintptr_t)b->addr, user->advertise_size);
            memcpy(user->info + 0x26a, &user->advertise_size, 2);
            memcpy(user->info + 0x26c, user->advertise, user->advertise_size);
            pthread_mutex_unlock(&user->info_lock);
        }
        break;
    }
    case 100:  // AttachStateChangeEvent
        if (!user->event_handle) user->event = kernel_create_event_pair(&(uint32_t){0}, &user->event_handle);
        resp->copy_handles[resp->ncopy++] = user->event_handle;
        break;
    }
}

SERVICE("ldn:u", ldn_creator) {
    (void)s;
    if (rt_is_botw()) return;
    if (req->cmd == 0) {
        LdnUser *state = calloc(1, sizeof *state);
        state->discovery_fd = -1;
        state->ctl_fd = -1;
        pthread_mutex_init(&state->info_lock, NULL);
        Service *user = service_new("IUserLocalCommunicationService", ldn_user, state);
        user->on_close = ldn_close;
        ipc_return_object(resp, user);
        LOG("[ldn] created IUserLocalCommunicationService\n");
    } else LOG("[ldn] creator cmd %u\n", req->cmd);
}
