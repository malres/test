
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
//static在此的作用是静态变量，*是表明mlooper是指针类型
static mylooper *mlooper = NULL;

//timespec：纳秒，来源于time.h
//CLOCK_MONOTONIC：从系统启动这一刻起开始计时,不受系统时间被用户改变的影响
__int64_t systemnanotime(){
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000000000LL + now.tv_nsec;
}

void doCodecWork(workerdata *d){

    /**
     * ssize_t：有符号整形，在32位机器上等同于int,z 64位机器上等同于long int.
     * queueInputBuffer和dequeueInputBuffer是一对方法，
     * 应用场景是用于对输入的数据流进行编码或者解码处理的时候，通过各种方法获得一个ByteBuffer的数组,
     * 然后调用dequeueInputBuffer方法提取出要处理的部分。处理完毕之后通过dequeueInputBuffer把ByteBuffer
     * 放回队列中，就能释放内存。
     * dequeueInputBuffer表示等待的时间（毫秒）
     * size_t：有符号整形，表示操作数据块的大小
     * */
    ssize_t bufidx = -1;
    if(!d->sawInputEOS){
        //获取缓冲区，设置超时为2000毫秒
        bufidx = AMediaCodec_dequeueInputBuffer(d->codec, 2000);
        LOGV("input buffer %zd", bufidx);
        if(bufidx >= 0){
            size_t bufsize;
            //取到缓冲区输入流
            auto buf = AMediaCodec_getInputBuffer(d->codec, bufidx, &bufsize);
            //开始读取样本,d->ex已经设置了选中的轨道
            auto sampleSize = AMediaExtractor_readSampleData(d->ex, buf ,bufsize);
            if(sampleSize < 0){
                sampleSize = 0;
                d->sawInputEOS = true;
                LOGV("EOS");
            }
            //以微秒为单位返回当前样本的呈现时间
            auto presentationTimeUs = AMediaExtractor_getSampleTime(d->ex);

            //将缓冲区传递至解码器
            AMediaCodec_queueInputBuffer(d->codec, bufidx, 0 ,sampleSize, presentationTimeUs,
                    d->sawInputEOS ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0);
            //前进到下一个样本
            AMediaExtractor_advance(d->ex);
        }
    }

    if(!d->sawOutputEOS){
        AMediaCodecBufferInfo info;
        //缓冲区第一步
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
                //延时操作，如果缓冲区里的可展示时间>当前视频播放的进度，就休眠一下
                usleep(delay/1000);
            }
            //渲染，如果info.size != 0等于true，就会渲染到surface上第二步
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
            //解码当前帧超时
            LOGV("no output buffer right now");
        } else{
            LOGV("unexpected info code : %zd", status);
        }
    }

    if(!d->sawInputEOS || !d->sawOutputEOS){
        //如果输入或者输出没有结束，就回调自己
        mlooper->post(kMsgCodecBuffer, d);
    }
}

//此处重写了消息的处理方法
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
                post(kMsgPauseAck, NULL, true);//清空队列
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
                                           &outStart, &outLen);//打开视频文件

        env->ReleaseStringUTFChars(filename, utf8);
        if (fd < 0){
            LOGE("failed to open file: %s %d (%s)", utf8, fd, strerror(errno));
            return JNI_FALSE;
        }

        data.fd = fd;

        //用于保存当前播放用到的一些标志位
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

        //获取轨道数
        int numtracks = AMediaExtractor_getTrackCount(ex);

        //负责媒体文件的编码和解码工作
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
                AMediaCodec_configure(codec, format, d->window, NULL, 0);//d->window 与解码器绑定
                d->ex = ex;//视频轨道
                d->codec = codec;//解码器
                d->renderstart = -1;//开始渲染时间
                d->sawInputEOS = false;
                d->sawOutputEOS = false;
                d->isPlaying = false;
                d->renderonce = true;
                AMediaCodec_start(codec);//开始解码
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
