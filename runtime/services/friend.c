// Friends service used by the MK8DX local play UI. No online friends are exposed.
#include "../game_sites.h"
#include "../hle.h"
#include "../ipc.h"

static void friend_user(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    LOG("[friend] IFriendService cmd %u\n", req->cmd);
    switch (req->cmd) {
    case 0: {
        uint32_t w, r;
        kernel_create_event_pair(&w, &r);
        resp->copy_handles[resp->ncopy++] = r;
        break;
    }
    case 10101: case 10400: case 20100: case 20101: case 20200: case 22010:
        OUT(uint32_t, 0) = 0;
        resp_size(resp, 4);
        break;
    case 10120: case 10420:
        OUT(uint8_t, 0) = 1;
        resp_size(resp, 1);
        break;
    }
}

SERVICE("friend:u", friend_creator) {
    (void)s;
    if (rt_is_botw()) return;
    if (req->cmd == 0) {
        ipc_return_object(resp, service_new("IFriendService", friend_user, NULL));
        LOG("[friend] created IFriendService\n");
    } else LOG("[friend] creator cmd %u\n", req->cmd);
}
