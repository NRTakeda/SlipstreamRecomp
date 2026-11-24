// Network interface manager (nifm) for MK8DX: there is no internet connection. The game asks for one in Time Trials
// (nn::nifm::SetLocalNetworkMode builds a RequestClient); with the logging stub CreateGeneralService returned no object
// and the sdk read through a null pointer (signal 11 in RequestClient::RequestClient). Requests are taken,
// go "on hold" when submitted and then fail as unavailable, as a console without a network answers.
#include <stdlib.h>
#include <string.h>

#include "../game_sites.h"
#include "../hle.h"
#include "../ipc.h"

#define NIFM_RESULT(desc) ((uint32_t)(110 | ((desc) << 9)))
#define NIFM_PENDING_CONNECTION NIFM_RESULT(111)
#define NIFM_COMMUNICATION_DISABLED NIFM_RESULT(1111)

enum { REQ_NOT_SUBMITTED = 1, REQ_INVALID = 1, REQ_ON_HOLD = 2 };

typedef struct {
    uint32_t state;
    KEvent *events[2];
    uint32_t readable[2];
} NifmRequest;

static void request_set_state(NifmRequest *r, uint32_t state) {
    r->state = state;
    klock();
    kernel_signal(r->events[0]);
    kunlock();
}

static void zero_out_buffers(IpcReq *req) {
    for (int i = 0; i < 2; i++) {
        IpcBuf *b = out_buf(req, i);
        if (b && b->size) memset((void *)(uintptr_t)b->addr, 0, b->size);
    }
}

static void nifm_request(Service *s, IpcReq *req, IpcResp *resp) {
    NifmRequest *r = s->state;
    switch (req->cmd) {
    case 0:  // GetRequestState
        OUT(uint32_t, 0) = r->state;
        resp_size(resp, 4);
        break;
    case 1:  // GetResult
        if (r->state == REQ_ON_HOLD) {
            request_set_state(r, REQ_INVALID);
            resp->result = NIFM_PENDING_CONNECTION;
        } else resp->result = NIFM_COMMUNICATION_DISABLED;
        break;
    case 2:  // GetSystemEventReadableHandles
        resp->copy_handles[resp->ncopy++] = r->readable[0];
        resp->copy_handles[resp->ncopy++] = r->readable[1];
        break;
    case 3:  // Cancel
        r->state = REQ_NOT_SUBMITTED;
        break;
    case 4:  // Submit
        if (r->state == REQ_NOT_SUBMITTED) request_set_state(r, REQ_ON_HOLD);
        break;
    case 21:  // GetAppletInfo: no applet to show
        zero_out_buffers(req);
        OUT(uint32_t, 0) = 0;
        OUT(uint32_t, 4) = 0;
        OUT(uint32_t, 8) = 0;
        resp_size(resp, 12);
        break;
    default:  // the Set* requirement calls and the rest: taken
        zero_out_buffers(req);
        break;
    }
    LOG("[nifm] IRequest cmd %u -> state %u result %#x\n", req->cmd, r->state, resp->result);
}

static void nifm_request_close(Service *s) { free(s->state); }

static void nifm_general(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 1: {  // GetClientId
        IpcBuf *b = out_buf(req, 0);
        if (b && b->size >= 4) {
            memset((void *)(uintptr_t)b->addr, 0, b->size);
            *(uint32_t *)(uintptr_t)b->addr = 1;
        }
        break;
    }
    case 4: {  // CreateRequest
        NifmRequest *r = calloc(1, sizeof *r);
        r->state = REQ_NOT_SUBMITTED;
        for (int i = 0; i < 2; i++) r->events[i] = kernel_create_event_pair(&(uint32_t){0}, &r->readable[i]);
        Service *rs = service_new("IRequest", nifm_request, r);
        rs->on_close = nifm_request_close;
        ipc_return_object(resp, rs);
        break;
    }
    case 5:   // GetCurrentNetworkProfile
    case 12:  // GetCurrentIpAddress
    case 15:  // GetCurrentIpConfigInfo
    case 18:  // GetInternetConnectionStatus
        zero_out_buffers(req);
        resp->result = NIFM_COMMUNICATION_DISABLED;
        break;
    default:  // the Is*Enabled / Is*Accepted queries answer false (zeroed), the rest are taken
        zero_out_buffers(req);
        resp_size(resp, 8);
        break;
    }
    LOG("[nifm] IGeneralService cmd %u -> %#x\n", req->cmd, resp->result);
}

static void nifm_static(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    if (rt_is_botw()) {  // BotW keeps the logging stub's answers
        LOG("[ipc] STUB %s cmd %u -> ok\n", s->name, req->cmd);
        resp_size(resp, 0x40);
        return;
    }
    if (req->cmd == 4 || req->cmd == 5) {  // CreateGeneralServiceOld / CreateGeneralService
        ipc_return_object(resp, service_new("IGeneralService", nifm_general, NULL));
        LOG("[nifm] created IGeneralService (no internet connection)\n");
    } else LOG("[nifm] static cmd %u\n", req->cmd);
}

SERVICE("nifm:u", nifm_u) { nifm_static(s, req, resp); }
SERVICE("nifm:a", nifm_a) { nifm_static(s, req, resp); }
SERVICE("nifm:s", nifm_s) { nifm_static(s, req, resp); }
