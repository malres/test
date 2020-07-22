
#include <assert.h>
#include <jni.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

#include "looper.h"
#include "media/NdkMediaCodec.h"
#include "media/NdkMediaExtractor.h"

#include <android/log.h>
#define TAG "NativeCodec"
#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#include <android/native_window_jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

//定义一个结构体，并取名为workerdata。相当于自定义一个数据类型
typedef struct {
    int fd;
    ANativeWindow* window;
    AMediaExtractor* ex;
    AMediaCodec *codec;
    int64_t renderstart;
    bool sawInputEOS;
    bool sawOutputEOS;
    bool isPlaying;
    bool renderonce;
} workerdata;

workerdata data = {-1,NULL,NULL,NULL,0, false, false, false, false};

//enum：枚举。所谓枚举是指将变量的值一一列举出来，变量只限于列举出来的值的范围内取值。
enum {
    kMsgCodecBuffer,
    kMsgPause,
    kMsgResume,
    kMsgPauseAck,
    kMsgDecodeDone,
    kMsgSeek,
};
//mylooper继承自looper
//virtual：定义虚函数的关键字
class mylooper: public looper{
    virtual void handle(int what, void* obj);
};

static mylooper *mlooper = NULL;

__int64_t systemnanotime(){
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000000000LL + now.tv_nsec;
}

void doCodecWork(workerdata *d){

    ssize_t bufidx = -1;
    if(!d->sawInputEOS){
        bufidx = AMediaCodec_dequeueInputBuffer(d->codec, 2000);
        LOGV("input buffer %zd", bufidx);
        if(bufidx >= 0){
            size_t bufsize;
            auto buf = AMediaCodec_getInputBuffer(d->codec, bufidx, &bufsize);
            auto sampleSize = AMediaExtractor_readSampleData(d->ex, buf ,bufsize);
            if(sampleSize < 0){
                sampleSize = 0;
                d->sawInputEOS = true;
                LOGV("EOS");
            }
            auto presentationTimeUs = AMediaExtractor_getSampleTime(d->ex);

            AMediaCodec_queueInputBuffer(d->codec, bufidx, 0 ,sampleSize, presentationTimeUs,
                    d->sawInputEOS ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0);
            AMediaExtractor_advance(d->ex);
        }
    }

    if(!d->sawOutputEOS){
        AMediaCodecBufferInfo info;
        auto status = AMediaCodec_dequeueOutputBuffer(d->codec, &info, 0);
        if(status >= 0){
            if(info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM){
                LOGV("output EOS");
                d->sawOutputEOS = true;
            }
            int64_t presentationNano = info.presentationTimeUs *1000;
            if(d->renderstart < 0){
                d->renderstart = systemnanotime() - presentationNano;    //systemnanotime提供相对精确的计时，但不能用来计算当前日期
                                                                         //返回的是时间的纳秒值
            }
            int64_t delay = (d->renderstart + presentationNano) - systemnanotime();
            if(delay > 0){
                usleep(delay/1000);
            }
            AMediaCodec_releaseOutputBuffer(d->codec, status, info.size != 0);
            if(d->renderonce){
                d->renderonce = false;
                return;
            }
        } else if (status == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED){
            LOGV("output buffers changed");
        } else if (status == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED){
            auto format = AMediaCodec_getOutputFormat(d->codec);
            LOGV("format changed to: %s", AMediaFormat_toString(format));
            AMediaFormat_delete(format);
        } else if (status == AMEDIACODEC_INFO_TRY_AGAIN_LATER){
            LOGV("no output buffer right now");
        } else{
            LOGV("unexpected info code : %zd", status);
        }
    }

    if(!d->sawInputEOS || !d->sawOutputEOS){
        mlooper->post(kMsgCodecBuffer, d);
    }
}

void mylooper::handle(int what, void *obj) {
    switch (what){
        case kMsgCodecBuffer:
            doCodecWork((workerdata*)obj);
            break;

        case kMsgDecodeDone:{
            workerdata *d = ((workerdata*)obj);
            AMediaCodec_stop(d->codec);
            AMediaCodec_delete(d->codec);
            AMediaExtractor_delete(d->ex);
            d->sawInputEOS = true;
            d->sawOutputEOS = true;
        }break;

        case kMsgSeek:{
            workerdata *d = (workerdata*)obj;
            AMediaExtractor_seekTo(d->ex, 0, AMEDIAEXTRACTOR_SEEK_NEXT_SYNC);
            AMediaCodec_flush(d->codec);
            d->renderstart = -1;
            d->sawInputEOS = false;
            d->sawOutputEOS = false;
            if(!d->isPlaying){
                d->renderonce = true;
                post(kMsgCodecBuffer, d);
            }
            LOGV("seeked");
        }break;

        case kMsgPause:{
            workerdata *d = (workerdata*)obj;
            if (d->isPlaying){
                d->isPlaying = false;
                post(kMsgPauseAck, NULL, true);
            }
        }break;

        case kMsgResume:{
            workerdata *d = (workerdata*)obj;
            if (!d->isPlaying){
                d->renderstart = -1;
                d->isPlaying = true;
                post(kMsgCodecBuffer, d);
            }
        }break;
    }
}

//extern "C" 表示以下方法按C语言的方式进行编译
extern "C"{
    //jboolean JNI中的变量类型        JNIEnv* 引用JNIenv指针
    jboolean Java_com_example_nativecodec_MainActivity_createStreamingMediaPlayer(JNIEnv* env,
            jclass clazz, jobject assetMgr, jstring filename){
        LOGV("@@@ create");

        //string转UTF-8
        const char *utf8 = env->GetStringUTFChars(filename, NULL);
        LOGV("opening %s", utf8);
        //%s 格式化为字符串

        off_t outStart, outLen;
        int fd = AAsset_openFileDescriptor(AAssetManager_open(AAssetManager_fromJava(env, assetMgr), utf8, 0),
                                           &outStart, &outLen);

        env->ReleaseStringUTFChars(filename, utf8);
        if (fd < 0){
            LOGE("failed to open file: %s %d (%s)", utf8, fd, strerror(errno));
            return JNI_FALSE;
        }

        data.fd = fd;

        workerdata *d = &data;

        AMediaExtractor *ex = AMediaExtractor_new();
        media_status_t err = AMediaExtractor_setDataSourceFd(ex, d->fd,
                static_cast<off64_t >(outStart),
                static_cast<off64_t>(outLen));

        close(d->fd);
        if (err != AMEDIA_OK){
            LOGV("setDataSource error: %d", err);
            return JNI_FALSE;
        }

        int numtracks = AMediaExtractor_getTrackCount(ex);

        AMediaCodec *codec = NULL;

        LOGV("input has %d tracks", numtracks);
        for (int i = 0; i < numtracks; ++i) {
            AMediaFormat *format = AMediaExtractor_getTrackFormat(ex, i);
            const char *s = AMediaFormat_toString(format);
            LOGV("track %d format: %s", i, s);
            const char *mime;
            if (!AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime)){
                LOGV("no mime type");
                return JNI_FALSE;
            } else if (!strncmp(mime, "video/", 6)){

                //生产代码应检查错误
                AMediaExtractor_selectTrack(ex, i);
                codec = AMediaCodec_createDecoderByType(mime);
                AMediaCodec_configure(codec, format, d->window, NULL, 0);
                d->ex = ex;
                d->codec = codec;
                d->renderstart = -1;
                d->sawInputEOS = false;
                d->sawOutputEOS = false;
                d->isPlaying = false;
                d->renderonce = true;
                AMediaCodec_start(codec);
            }
            //MediaFormat 最小的音频格式
            AMediaFormat_delete(format);
        }

        mlooper = new mylooper();
        mlooper->post(kMsgCodecBuffer, d);

        return JNI_TRUE;
    }

    //设置流媒体播放器的播放状态
    void Java_com_example_nativecodec_MainActivity_setPlayingStreamingMediaPlayer(JNIEnv* env,
            jclass clazz, jboolean isPlaying){
        LOGV("@@@ playpause: %d", isPlaying);
        if (mlooper){
            mlooper->post(kMsgResume, &data);
//            if (isPlaying){
//
//            } else{
//
//            }
        }
    }

    //关闭本地媒体系统
    void Java_com_example_nativecodec_MainActivity_shutdown(JNIEnv* env, jclass clazz){
        LOGV("@@@ shutdown");
        if (mlooper){
            mlooper->post(kMsgDecodeDone, &data, true);
            mlooper->quit();
            delete mlooper;
            mlooper = NULL;
        }
        if (data.window){
            ANativeWindow_release(data.window);
            data.window = NULL;
        }
    }

    //设置surface
    void Java_com_example_nativecodec_MainActivity_setSurface(JNIEnv *env,jclass clazz,
            jobject surface){

        //从Java surface获取native window
        if (data.window){
            ANativeWindow_release(data.window);
            data.window = NULL;
        }
        data.window = ANativeWindow_fromSurface(env, surface);
        LOGV("@@@ setsurface %p", data.window);
    }

    //转回流媒体播放器
    void Java_com_example_nativecodec_MainActivity_rewindStreamingMediaPlayer(JNIEnv *env, jclass clazz)
    {
        LOGV("@@@ rewind");
        if (mlooper){
            mlooper->post(kMsgSeek, &data);
        }
    }

    //暂停播放
    void Java_com_example_nativecodec_MainActivity_pauseStreamingMediaPlayer(JNIEnv *env, jclass clazz){
        LOGV("@@@ pause");
        if (mlooper){
            mlooper->post(kMsgPause, &data);
        }
    }

}
