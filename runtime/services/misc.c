// Small services: parental control, add-on content, and other mostly-stubbed interfaces.
#include <string.h>

#include "../hle.h"
#include "../ipc.h"

static void ok_logger(Service *s, IpcReq *req, IpcResp *resp) {
    LOG("[svc] %s cmd %u (ok)\n", s->name, req->cmd);
    resp_size(resp, 0x10);
}

static uint32_t new_event_handle(void) {
    uint32_t w, r;
    kernel_create_event_pair(&w, &r);
    return r;
}

// ------------------------------------------------------------------ pctl
static void pctl_service(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 1031: case 1035: case 1018: OUT(uint8_t, 0) = 0; resp_size(resp, 1); break;  // restrictions off
    case 1032: OUT(uint32_t, 0) = 0; resp_size(resp, 4); break;
    default: LOG("[pctl] IParentalControlService cmd %u (ok)\n", req->cmd); resp_size(resp, 0x10);
    }
}

static void pctl_root(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    if (req->cmd == 0 || req->cmd == 1)
        ipc_return_object(resp, service_new("IParentalControlService", pctl_service, NULL));
}
SERVICE("pctl", pctl) { pctl_root(s, req, resp); }
SERVICE("pctl:s", pctl_s) { pctl_root(s, req, resp); }
SERVICE("pctl:a", pctl_a) { pctl_root(s, req, resp); }
SERVICE("pctl:r", pctl_r) { pctl_root(s, req, resp); }

// ------------------------------------------------------------------ aoc:u
// The add-on contents set up (services/fs.c: an image per index) are the ones installed; their data is mounted
// through fsp-srv OpenDataStorageByDataId.
static int aoc_list(uint32_t *out, int max) {
    int n = 0;
    for (int i = 1; i <= 8; i++)
        if (fs_aoc_present(i)) { if (out && n < max) out[n] = (uint32_t)i; n++; }
    return n;
}

SERVICE("aoc:u", aoc_u) {
    (void)s;
    switch (req->cmd) {
    case 0: case 2: OUT(uint32_t, 0) = (uint32_t)aoc_list(NULL, 0); resp_size(resp, 4); break;  // CountAddOnContent
    case 1: case 3: {  // ListAddOnContent(offset, count) -> B buffer of indices
        uint32_t idx[8], offset = IN(uint32_t, 0), count = IN(uint32_t, 4);
        int n = aoc_list(idx, 8), w = 0;
        IpcBuf *b = out_buf(req, 0);
        uint32_t room = b ? (uint32_t)(b->size / 4) : 0;
        if (count > room) count = room;
        for (uint32_t i = offset; i < (uint32_t)n && (uint32_t)w < count; i++)
            ((uint32_t *)(uintptr_t)b->addr)[w++] = idx[i];
        LOG("[aoc] ListAddOnContent(%u, %u): %d of %d\n", offset, count, w, n);
        OUT(uint32_t, 0) = (uint32_t)w;
        resp_size(resp, 4);
        break;
    }
    case 4: case 5: OUT(uint64_t, 0) = fs_aoc_base_id(); resp_size(resp, 8); break;  // GetAddOnContentBaseId
    case 6: case 7: LOG("[aoc] PrepareAddOnContent %u\n", IN(uint32_t, 0)); break;
    case 8: case 10: resp->copy_handles[resp->ncopy++] = new_event_handle(); break;  // ...ListChangedEvent
    default: ok_logger(s, req, resp);
    }
}

// ------------------------------------------------------------------ mii:e / mii:u / miiimg
// Valid default nn::mii::CharInfo (0x58 bytes) so CharInfoAccessor::IsValid() passes.
static void fill_default_mii(uint8_t *ci, uint32_t idx) {
    memset(ci, 0, 0x58);
    ci[0] = (uint8_t)(0x10 + (idx & 0x7f));
    ci[6] = 0x40;
    ci[8] = 0x80;
    ci[15] = 0x01;
    static const uint16_t name[] = {'P', 'l', 'a', 'y', 'e', 'r', 0};
    memcpy(ci + 0x10, name, sizeof name);
    ci[0x27] = (uint8_t)(idx % 12);  // favorite_color
    ci[0x28] = (uint8_t)(idx & 1);   // gender
    ci[0x29] = 64;                   // height
    ci[0x2A] = 64;                   // build
    ci[0x2E] = 2;                    // faceline_color
    ci[0x31] = 30;                   // hair_type
    ci[0x32] = 8;                    // hair_color
    ci[0x34] = 2;                    // eye_type
    ci[0x35] = 8;                    // eye_color
    ci[0x36] = 4;                    // eye_scale
    ci[0x37] = 3;                    // eye_aspect
    ci[0x38] = 4;                    // eye_rotate
    ci[0x39] = 2;                    // eye_x
    ci[0x3A] = 12;                   // eye_y
    ci[0x3B] = 6;                    // eyebrow_type
    ci[0x3C] = 8;                    // eyebrow_color
    ci[0x3D] = 4;                    // eyebrow_scale
    ci[0x3E] = 3;                    // eyebrow_aspect
    ci[0x3F] = 6;                    // eyebrow_rotate
    ci[0x40] = 2;                    // eyebrow_x
    ci[0x41] = 10;                   // eyebrow_y (>= 3)
    ci[0x42] = 1;                    // nose_type
    ci[0x43] = 4;                    // nose_scale
    ci[0x44] = 9;                    // nose_y
    ci[0x45] = 23;                   // mouth_type
    ci[0x46] = 19;                   // mouth_color
    ci[0x47] = 4;                    // mouth_scale
    ci[0x48] = 3;                    // mouth_aspect
    ci[0x49] = 13;                   // mouth_y
    ci[0x4A] = 8;                    // beard_color
    ci[0x4D] = 4;                    // mustache_scale
    ci[0x4E] = 10;                   // mustache_y
    ci[0x50] = 8;                    // glass_color
    ci[0x51] = 4;                    // glass_scale
    ci[0x52] = 10;                   // glass_y
    ci[0x54] = 4;                    // mole_scale
    ci[0x55] = 2;                    // mole_x
    ci[0x56] = 20;                   // mole_y
}

static void mii_database(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0: case 1: case 20:  // IsUpdated, IsFullDatabase, IsBrokenDatabaseWithClearFlag -> false
        OUT(uint8_t, 0) = 0;
        resp_size(resp, 1);
        break;
    case 2:  // GetCount(SourceFlag) -> u32
        OUT(uint32_t, 0) = (IN(uint32_t, 0) & 2) ? 6 : 1;
        resp_size(resp, 4);
        break;
    case 3: {  // Get(SourceFlag) -> u32 count, B buffer of CharInfoElement (0x5c bytes)
        IpcBuf *b = out_buf(req, 0);
        uint32_t max = b ? (uint32_t)(b->size / 0x5c) : 0;
        uint32_t want = (IN(uint32_t, 0) & 2) ? 6 : 1;
        uint32_t n = want < max ? want : max;
        for (uint32_t i = 0; i < n; i++) {
            uint8_t *dst = (uint8_t *)(uintptr_t)b->addr + i * 0x5c;
            fill_default_mii(dst, i);
            *(uint32_t *)(dst + 0x58) = (IN(uint32_t, 0) & 2) ? 1 : 0;
        }
        OUT(uint32_t, 0) = n;
        resp_size(resp, 4);
        break;
    }
    case 4: {  // Get1(SourceFlag) -> u32 count, B buffer of CharInfo (0x58 bytes)
        IpcBuf *b = out_buf(req, 0);
        uint32_t max = b ? (uint32_t)(b->size / 0x58) : 0;
        uint32_t want = (IN(uint32_t, 0) & 2) ? 6 : 1;
        uint32_t n = want < max ? want : max;
        for (uint32_t i = 0; i < n; i++) fill_default_mii((uint8_t *)(uintptr_t)b->addr + i * 0x58, i);
        OUT(uint32_t, 0) = n;
        resp_size(resp, 4);
        break;
    }
    case 5:  // UpdateLatest(CharInfo, SourceFlag) -> CharInfo
        if (req->data_size >= 0x58) memcpy(resp->data, req->data, 0x58);
        else fill_default_mii(resp->data, 0);
        resp_size(resp, 0x58);
        break;
    case 6: case 7:  // BuildRandom, BuildDefault -> CharInfo (0x58 bytes)
        fill_default_mii(resp->data, IN(uint32_t, 0));
        resp_size(resp, 0x58);
        break;
    case 11: case 21:  // FindIndex, GetIndex -> s32
        OUT(int32_t, 0) = 0;
        resp_size(resp, 4);
        break;
    default:
        LOG("[mii] IDatabaseService cmd %u (ok)\n", req->cmd);
        resp_size(resp, 0x58);
    }
}

static void mii_static(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    if (req->cmd == 0) ipc_return_object(resp, service_new("IDatabaseService", mii_database, NULL));
    else ok_logger(s, req, resp);
}
SERVICE("mii:e", mii_e) { mii_static(s, req, resp); }
SERVICE("mii:u", mii_u) { mii_static(s, req, resp); }
SERVICE("miiimg", miiimg) { ok_logger(s, req, resp); }

