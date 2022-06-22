#ifndef CLIENT_H__
#define CLIENT_H__

#define DEFAULT_PLAYERCMD   "/usr/local/bin/mplayer > /dev/null"

struct client_conf_st {
    char *rcvport;
    char *mgroup;
    char *player_cmd;
};

struct option {
    const char *name;               // 选项名字
    int has_arg;                    // 是否带参（0表示不带，1表示1个参数，2表示2个参数）
    int *flag;                      // 表示返回结果
    int val;                        // 返回结果的值
};

struct ip_mreqn {
    struct in_addr imr_multiaddr;   // 多播组的ip地址
    struct in_addr imr_address;     // 设置加入多播组的网卡ip
    int imr_ifindex;   // 设置加入多播组的网卡index，优先级高于上面的网卡ip
};

extern struct client_conf_st client_conf;

#endif