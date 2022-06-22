#ifndef PROTO_H__
#define PROTO_H__

#include "site_type.h"

#define DEFAULT_MGROUP      "225.2.2.2"
#define DEFAULT_RCVPORT     "1989"

#define CHANNR              100                                         // 总频道号
#define LISTCHNID           0                                           // 当前节目单所在频道
#define MINCHNID            1                                           // 最小节目号
#define MAXCHNID            (MINCHNID + CHNNR - 1)                      // 最大节目号

#define MSG_CHANNEL_MAX     (65536 - 20 - 8)                            // 包的长度减去ip协议的报头和UDP的报头
#define MAX_DATA            (MSG_CHANNEL_MAX - sizeof(chnid_t))

#define MSG_LIST_NAX        (65536 - 20 - 8)
#define MAX_ENTRY           (MSG_LIST_MAX - sizeof(chnid_t))

struct msg_channel_st {
    chnid_t chnid;                                                      // 值一定在区间[MINCHNID, MAXCHNID]
    uint8_t data[1];
}__attribute__((packed));                                               // 取消结构体在编译过程中的优化对齐

struct msg_listentry_st {
    chnid_t chnid;
    uint8_t desc[1];
}__attribute__((packed));

struct msg_list_st {
    chnid_t chnid;                                                      // 值一定是LISTCHNID
    struct msg_listentry_st entry[1];
    
}__attribute__((packed));



#endif