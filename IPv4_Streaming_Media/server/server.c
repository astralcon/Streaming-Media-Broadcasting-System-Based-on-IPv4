#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <proto.h>
#include <syslog.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <fcntl.h>

#include "server_conf.h"
#include "medialib.h"
#include "thr_channel.h"
#include "thr_list.h"

/* 
 * -M   指定多播组
 * -P   指定接收端口
 * -F   前台运行
 * -D   指定媒体库位置
 * -I   指定网络设备
 * -H   显示帮助
 * 
 * */

struct server_conf_st server_conf = {
    .rcvport = DEFAULT_RCVPORT,
    .mgroup = DEFAULT_MGROUP,
    .media_dir = DEFAULT_MEDIADIR,
    .runmode = RUN_DAEMON,
    .ifname = DEFAULT_IF
};

int serversd;
struct sockaddr_in sndaddr;
static struct mlib_listentry_st *list;

static void printf_help() {
    printf("-M   指定多播组\n"
           "-P   指定接收端口\n"
           "-F   前台运行\n"
           "-D   指定媒体库位置\n"
           "-I   指定网络设备\n"
           "-H   显示帮助\n");
}

static void daemon_exit(int s) {
    thr_list_destroy();
    thr_channel_destroyall();
    mlib_freechnlist(list);
    syslog(LOG_WARNING, "signal %d caught, exit now.", s);
    closelog();
    exit(0);
}

static int daemonize() {
    pid_t pid;
    pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "fork() failed:%s", strerror(errno));
        return -1;
    }
    if (pid > 0)
        exit(0);

    int fd;
    fd = open("/dev/null", O_RDWR);
    if (fd < 0) {
        syslog(LOG_ERR, "open() failed:%s", strerror(errno));
        return -2;
    } else {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        if (fd > 2)
            close(fd);
    }
    chdir("/");
    umask(0);
    setsid();
    return 0;
}

static int socket_init() {
    int serversd = socket(AF_INET, SOCK_DGRAM, 0);
    struct ip_mreqn mreq;
    if (serversd < 0) {
        syslog(LOG_ERR, "socket():%s\n", strerror(errno));
        exit(1);
    }
    inet_pton(AF_INET, server_conf.mgroup, &mreq.imr_multiaddr);
    inet_pton(AF_INET, "0.0.0.0", &mreq.imr_address);
    mreq.imr_ifindex = if_nametoindex(server_conf.ifname);
    if (setsockopt(serversd, IPPROTO_IP, IP_MULTICAST_IF, &mreq, sizeof(mreq)) < 0) {
        syslog(LOG_ERR, "setsockopt(IP_MULTICAST_IF):%s", strerror(errno));
        exit(1);
    }
    // bind
    sndaddr.sin_family = AF_INET;
    sndaddr.sin_port = htons(atoi(server_conf.rcvport));
    inet_pton(AF_INET, server_conf.mgroup, &sndaddr.sin_addr);
    return 0;
}

int main(int argc, char **argv) {
    struct sigaction sa;
    sa.sa_handler = daemon_exit;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGINT);
    sigaddset(&sa.sa_mask, SIGQUIT);
    sigaddset(&sa.sa_mask, SIGTERM);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    openlog("netradio", LOG_PID | LOG_PERROR, LOG_DAEMON);
    #ifdef DEBUG
        fprintf(stdout, "here1!\n");
    #endif

    // 命令行分析
    int c;
    while (1) {
        c = getopt(argc, argv, "M:P:FD:I:H");
        #ifdef DEBUG
            fprintf(stdout, "here2!\n");
        #endif
        printf("get command c:%c\n", c);
        if (c < 0) 
            break;
        switch (c) {
            case 'M':
                server_conf.mgroup = optarg;
                break;
            case 'P':
                server_conf.rcvport = optarg;
                break;
            case 'F':
                server_conf.runmode = RUN_FOREGROUND;
                break;
            case 'D':
                server_conf.media_dir = optarg;
                break;
            case 'I':
                server_conf.ifname = optarg;
                break;
            case 'H':
                printf_help();
                exit(0);
                break;
            default:
                abort();
                break;
        }
        break;
    }
    #ifdef DEBUG
        printf("server_conf.runmode:%d", server_conf.runmode);
    #endif
    // 守护进程的实现
    if (server_conf.runmode == RUN_DAEMON) {
        if(daemonize() != 0)
            perror("daemonize()");
    } else if (server_conf.runmode == RUN_FOREGROUND) {
        // do nothing
    } else {
        syslog(LOG_ERR, "EINVAL server_conf.runmode.");
        exit(1);
    }

    // SCOKET初始化
    socket_init();
    // 获取频道信息
    int list_size, err;
    err = mlib_getchnlist(&list, &list_size);
    // 创建节目单线程
    thr_list_create(list, list_size);
    // if error
    if (err) {
        syslog(LOG_ERR, "mlib_getchnlist():%s", strerror(errno));
        exit(1);
    }
    thr_list_create(list, list_size);
    // 创建频道线程
    int i = 0;
    while (i < list_size) {
        err = thr_channel_create(list + i);
        // if error
        if (err) {
            fprintf(stderr, "thr_channel_create():%s\n", strerror(errno));
            exit(1);
        }
        ++i;
    }
    syslog(LOG_DEBUG, "%d channel threads created.", i);
    while (1)
        pause();
}

