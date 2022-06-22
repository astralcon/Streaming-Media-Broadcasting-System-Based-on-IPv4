#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#include "mytbf.h"

struct mytbf_st {
    int cps;
    int burst;
    int token;
    int pos;
    pthread_mutex_t mut;
    pthread_cond_t cond;
};

static struct mytbf_st *job[MYTBF_MAX];
static pthread_mutex_t mut_job = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t init_once = PTHREAD_ONCE_INIT;
static pthread_t tid;

int min(int a, int b) {
    if (a <= b)
        return a;
    else 
        return b;
}

static void *thr_alrm(void *p) {
    int i;
    while (1) {
        pthread_mutex_lock(&mut_job);
        for (i = 0; i < MYTBF_MAX; ++i) {
            if (job[i] != NULL) {
                pthread_mutex_lock(&job[i]->mut);
                job[i]->token += job[i]->cps;
                if (job[i]->token > job[i]->burst)
                    job[i]->token = job[i]->burst;
                pthread_cond_broadcast(&job[i]->cond);
                pthread_mutex_unlock(&job[i]->mut);
            }
        }
        pthread_mutex_unlock(&mut_job);
        sleep(1);
    }
}

static void module_unload() {
    pthread_cancel(tid);
    pthread_join(tid, NULL);

    int i = 0;
    while (i < MYTBF_MAX) {
        free(job[i]);
        ++i;
    }
}

static void module_load() {
    pthread_t tid;
    int err = pthread_create(&tid, NULL, thr_alrm, NULL);
    if (err) {
        fprintf(stderr, "pthread_create():%s\n", strerror(errno));
        exit(1);
    }
    atexit(module_unload);
}

static int get_free_pos_unlocked() {
    int i = 0;
    while (i < MYTBF_MAX) {
        if (job[i] == NULL)
            return i;
        ++i;
    }
    return -1;
}

mytbf_t *mytbf_init(int cps, int burst) {
    struct mytbf_st *me;
    pthread_once(&init_once, module_load);
    module_load();
    me = malloc(sizeof(me));
    if (me == NULL) {
        return NULL;
    }
    me->cps = cps;
    me->burst = burst;
    me->token = 0;
    pthread_mutex_init(&me->mut, NULL);
    pthread_cond_init(&me->cond, NULL);
    pthread_mutex_lock(&mut_job);

    int pos = get_free_pos_unlocked();
    if (pos < 0) {
        pthread_mutex_unlock(&mut_job);
        free(me);
        return me;
    }
    me->pos = pos;
    job[me->pos] = me;
    pthread_mutex_unlock(&mut_job);

}

int mytbf_fetchtoken(mytbf_t *ptr, int size) {
    struct mytbf_st *me = ptr;
    pthread_mutex_lock(&me->mut);
    while (me->token <= 0) {
        pthread_cond_wait(&me->cond, &me->mut);
    }

    int n = min(me->token, size);
    me->token -= n;
    pthread_mutex_unlock(&me->mut);
    return n;
}

int mytbf_returntoken(mytbf_t *ptr, int size) {
    struct mytbf_st *me = ptr;
    pthread_mutex_lock(&me->mut);
    me->token += size;
    if (me->token > me->burst)
        me->token = me->burst;
    pthread_cond_broadcast(&me->cond);
    pthread_mutex_unlock(&me->mut);
    return 0;
}

int mytbf_destroy(mytbf_t *ptr) {
    struct mytbf_st *me = ptr;
    pthread_mutex_lock(&mut_job);
    job[me->pos] = NULL;
    pthread_mutex_unlock(&mut_job);
    pthread_mutex_destroy(&me->mut);
    pthread_cond_destroy(&me->cond);
    free(ptr);
    return 0;
}

int mytbf_checktoken(mytbf_t *ptr) {
    int token_left = 0;
    struct mytbf_st *me = ptr;
    pthread_mutex_lock(&me->mut);
    token_left = me->token;
    pthread_mutex_unlock(&me->mut);
    return token_left;
}

