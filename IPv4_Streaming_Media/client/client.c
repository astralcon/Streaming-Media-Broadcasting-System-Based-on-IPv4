#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <net/if.h>

#include "client.h"
#include <proto.h>
/*
 *  -M --mgroup  指定多播组
 *  -P --port    指定接收端口
 *  -p --player  指定播放器
 *  -H --help    获取帮助
 * 
 * */

struct client_conf_st client_conf = {
        .rcvport = DEFAULT_RCVPORT,
        .mgroup = DEFAULT_MGROUP,
        .player_cmd = DEFAULT_PLAYERCMD
};

static void printf_help() {
    printf("-P --port   指定接收端口\n"
           "-M --mgroup   指定多播组\n"
           "-p --player   指定播放器\n"
           "-H --help   获取帮助\n");
}

static ssize_t writen(int fd, const char *buf, size_t len) {
    int ret, pos = 0;
    while (len > 0) {
        ret = write(fd, buf + pos, len);
        if (ret < 0) {
            if (errno = EINTR)
                continue;
            perror("write()");
            return -1;
        }
        len -= ret;
        pos += ret;
    }
    return 0;
}

int main(int argc, char **argv) {
    struct sockaddr_in laddr, serveraddr, raddr;
    socklen_t serveraddr_len, raddr_len;
    int index = 0;
    struct ip_mreqn mreq;
    struct option argarr[] = {
        {"port", 1, NULL, 'P'}, 
        {"mgroup", 1, NULL, 'M'}, 
        {"player", 1, NULL, 'p'}, 
        {"help", 0, NULL, 'H'},
        {NULL, 0, NULL, 0}
    };
    /*
     * 初始化
     * 级别：默认值，配置文件，环境变量，命令行参数
    */
    int c;
    while (1) {
        c = getopt_long(argc, argv, "P:M:p:H", argarr, &index);             // getopt只能处理短选项，而getopt_long短选项和长选项都能处理
        if (c < 0) 
            break;
        switch (c) {
            case 'P':
                client_conf.rcvport = optarg;
                break;
            case 'M':
                client_conf.mgroup = optarg;
                break;
            case 'p':
                client_conf.player_cmd = optarg;
                break;
            case 'H':
                printf_help();
                exit(0);
                break;
            default:
                abort();
                break;
        }
    }

    int sd;
    sd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sd < 0) {
        perror("socket()");
        exit(1);
    }
    inet_pton(AF_INET, client_conf.mgroup, &mreq.imr_multiaddr);
    // if error
    inet_pton(AF_INET, "0.0.0.0", &mreq.imr_address);
    mreq.imr_ifindex = if_nametoindex("eth0");          // 将网卡名称转换为对应的index
    if (setsockopt(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        perror("setsockopt()");
        exit(1);
    }

    int pd[2];          // 一端作为读端，一端作为写端
    int val = 1;
    if (setsockopt(sd, IPPROTO_IP, IP_MULTICAST_LOOP, &val, sizeof(val)) < 0) {
        perror("setsockopt()");
        exit(1);
    }
    laddr.sin_family = AF_INET;
    laddr.sin_port = htons(atoi(client_conf.rcvport));
    inet_pton(AF_INET, "0.0.0.0", &laddr.sin_addr);
    if (bind(sd, (void *)&laddr, sizeof(laddr)) < 0) {
        perror("bind()");
        exit(1);
    }
    if (pipe(pd) < 0) {
        perror("pipe()");
        exit(1);
    }

    pid_t pid;
    pid = fork();
    if (pid < 0) {
        perror("fork()");
        exit(1);
    }
    if (pid == 0) {             
        // 子进程： 调用解码器
        close(sd);
        close(pd[1]);
        dup2(pd[0], 0);
        if (pd[0] > 0)
            close(pd[0]);
        execl("/bin/sh", "sh", "-c", client_conf.player_cmd, NULL);
        perror("execl()");
        exit(1);
    }

    // 父进程：从网络上收包，发送给子进程
    // 收节目单
    struct msg_list_st *msg_list;
    msg_list = malloc(MSG_LIST_NAX);
    if (msg_list == NULL) {
        perror("malloc()");
        exit(1);
    }

    int len;
    while (1) {
        len = recvfrom(sd, msg_list, MSG_LIST_NAX, 0, (void *)&serveraddr, &serveraddr_len);
        fprintf(stderr, "server_addr:%d\n", serveraddr.sin_addr.s_addr);
        if (len < sizeof(struct msg_list_st)) {
            fprintf(stderr, "message is too small.\n");
            continue;
        }
        if (msg_list->chnid != LISTCHNID) {
            fprintf(stderr, "current chnid:%d\n", msg_list->chnid);
            fprintf(stderr, "chnid is not match.\n");
            continue;
        }
        break;
    }

    // 打印节目单并选择频道
    struct msg_listentry_st *pos;
    for (pos = msg_list->entry; (char *)pos < ((char *)msg_list + len); pos = (void *)((char *)pos + ntohs(pos->len))) {
        printf("channel %d:%s\n", pos->chnid, pos->desc);
    }
    free(msg_list);

    int chosenid, ret = 0;
    while (ret < 1) {
        ret = scanf("%d", &chosenid);
        if (ret != 1)
            exit(1);
    }

    // 收频道包，发送给子进程
    struct msg_channel_st *msg_channel;
    msg_channel = malloc(MSG_CHANNEL_MAX);
    if (msg_channel == NULL) {
        perror("malloc()");
        exit(1);
    }
    raddr_len = sizeof(raddr);
    char ipstr_raddr[30];
    char ipstr_server_addr[30];
    while (1) {
        len = recvfrom(sd, msg_channel, MSG_CHANNEL_MAX, 0, (void *)&raddr, &raddr_len);
        fprintf(stderr, "raddr:%d\n", raddr.sin_addr.s_addr);
        // 防止有人恶意发送不相关的包
        if (raddr.sin_addr.s_addr != serveraddr.sin_addr.s_addr) {
            inet_ntop(AF_INET, &raddr.sin_addr.s_addr, ipstr_raddr, 30);
            inet_ntop(AF_INET, &serveraddr.sin_addr.s_addr, ipstr_server_addr, 30);
            fprintf(stderr, "Ignore:address not match. raddr:%s serveraddr:%s\n", ipstr_raddr, ipstr_server_addr);
            continue;
        }
        if (raddr.sin_port != serveraddr.sin_port) {
            fprintf(stderr, "Ignore:port not match.\n");
            continue;
        }
        if (len < sizeof(struct msg_channel_st)) {
            fprintf(stderr, "Ignore:message too small.\n");
            continue;
        }
        // 做一个缓冲机制
        if (msg_channel->chnid == chosenid) {
            fprintf(stdout, "Accepted msg:%d recieved.\n", msg_channel->chnid);
            if (writen(pd[1], msg_channel->data, len - sizeof(chnid_t)) < 0)
                exit(1);
        }
    }
    free(msg_channel);
    close(sd);
    exit(0);
}