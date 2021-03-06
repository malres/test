//
// Created by malres on 2020/7/16.
//

#include "looper.h"

#include <assert.h>
#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <semaphore.h>

#include <android/log.h>
#define TAG "NativeCodec-looper"
#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, TAG, __VA_ARGS__)

struct loopermessage;
typedef struct loopermessage loopermessage;

struct loopermessage{
    int what;
    void *obj;
    loopermessage *next;
    bool quit;
};

void* looper::trampoline(void *p) {
    ((looper*)p)->loop();
    return NULL;
}

looper::looper(){
    sem_init(&headdataavailable, 0, 0);
    sem_init(&headwriteprotect, 0, 1);
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    pthread_create(&worker, &attr, trampoline, this);
    running = true;
}

looper::~looper(){
    if (running){
        LOGV("Looper deleted while still running.Some messages will not be processed");
        quit();
    }
}

void looper::post(int what, void *data, bool flush) {
    loopermessage *msg = new loopermessage();
    msg->what = what;
    msg->obj = data;
    msg->next = NULL;
    msg->quit = false;
    addmsg(msg, flush);
}

void looper::addmsg(loopermessage *msg, bool flush) {
    sem_wait(&headwriteprotect);
    loopermessage *h = head;

    if (flush){
        while (h){
            loopermessage *next = h->next;
            delete h;
            h = next;
        }
        h = NULL;
    }
    if (h){
        while (h->next){
            h = h->next;
        }
        h->next = msg;
    } else{
        head = msg;
    }
    LOGV("post msg %d", msg->what);
    sem_post(&headwriteprotect);
    sem_post(&headdataavailable);
}

void looper::loop() {
    while (true){
        //等待可用消息
        sem_wait(&headdataavailable);

        //获取下一条可用消息
        sem_wait(&headwriteprotect);
        loopermessage *msg = head;
        if (msg == NULL){
            LOGV("no msg");
            sem_post(&headwriteprotect);
            continue;
        }
        head = msg->next;
        sem_post(&headwriteprotect);

        if (msg->quit){
            LOGV("quitting");
            delete msg;
            return;
        }
        LOGV("processing msg %d", msg->what);
        handle(msg->what, msg->obj);
        delete msg;
    }
}

void looper::quit() {
    LOGV("quit");
    loopermessage *msg = new loopermessage();
    msg->what = 0;
    msg->obj = NULL;
    msg->next = NULL;
    msg->quit = true;
    addmsg(msg, false);
    void *retval;
    pthread_join(worker, &retval);
    sem_destroy(&headdataavailable);
    sem_destroy(&headwriteprotect);
    running = false;
}
void looper::handle(int what, void* obj) {
    LOGV("dropping msg %d %p", what, obj);
}