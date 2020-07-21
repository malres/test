package com.example.nativecodec;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;

import android.app.Activity;
import android.content.res.AssetManager;
import android.graphics.SurfaceTexture;
import android.os.Bundle;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CompoundButton;
import android.widget.CompoundButton.OnCheckedChangeListener;
import android.widget.RadioButton;
import android.widget.Spinner;
import android.widget.TextView;

import java.io.IOException;

public class MainActivity extends Activity {
    static final String TAG = "NativeCodec";

    String mSourceString = null;

    SurfaceView mSurfaceView1;          //surface的view
    SurfaceHolder mSurfaceHolder1;      //一个接口，surface的监听器，提供访问和控制surfaceview背后的surface相关的方法

    VideoSink mSelectedVideoSink;
    VideoSink mNativeCodecPlayerVideoSink;

    SurfaceHolderVideoSink mSurfaceHolder1VideoSink;
    GLViewVideoSink mGLView1VideoSink;
    //GLSurfaceView继承自SurfaceView,对SurfaceView再次封装，方便在安卓中使用OpenGL

    boolean mCreated = false;
    boolean mIsPlaying = false;



    //Activity生命周期的开始
    //savedInstanceState作用：保存Activity状态数据
    //与icicle的区别：简单来说就是icicle是小map，savedInstanceState是大map
    @Override
    protected void onCreate(Bundle icicle) {
        super.onCreate(icicle);
        setContentView(R.layout.activity_main);

        //第二块幕布
        mGLView1 = (MyGLSurfaceView) findViewById(R.id.glsurfaceview1);

        //设置surface1接收器
        //使用Android的findViewById查找到第一块幕布的所在地，并赋值给mSurfaceView1
        mSurfaceView1 = (SurfaceView) findViewById(R.id.surfaceview1);
        mSurfaceHolder1 = mSurfaceView1.getHolder();
        //通过getHolder获取到相关SurfaceHolder类


        //Callback回调函数
        mSurfaceHolder1.addCallback(new SurfaceHolder.Callback() {

            //启动
            @Override
            public void surfaceCreated(SurfaceHolder holder) {
                Log.v(TAG,"surfaceCreated");
                if (mRadio1.isChecked()){
                    //跳转到setSurface
                    setSurface(holder.getSurface());
                }
            }

            //加载    format格式
            @Override
            public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                Log.v(TAG, "surfaceChanged format=" + format + ", width=" + width + ", height="
                 + height);
            }
            //释放
            @Override
            public void surfaceDestroyed(SurfaceHolder holder) {
                Log.v(TAG,"surfaceDestroyed");
            }
        });

        //初始化内容源微调器
        //source_spinner在activity_main.xml文件中，起到选择文件源的作用
        //Spinner 安卓中的下拉框控件
        //通过findViewById找到source_spinner并赋值给sourceSpinner
        Spinner sourceSpinner = (Spinner) findViewById(R.id.source_spinner);

        //ArrayAdapter是ListView和GridView的一个适配器，会将传入的View强制转换为TextView
        //其中ListView以列表形式显示数据，GridView以网格形式展示数据
        //android.R.layout是Android SDK自带的布局文件,R.array则是自己在res目录下面写的布局
        ArrayAdapter<CharSequence> sourceAdapter = ArrayAdapter.createFromResource(
                this, R.array.source_array, android.R.layout.simple_spinner_item);


        //source_array指向res下values下的strings.xml文件，simple_spinner_item下拉框选择播放文件
        //setDropDownViewResource 设置数据适配器的显示样式
        //setOnItemSelectedListener给listview的item设置监听器
        sourceAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        sourceSpinner.setAdapter(sourceAdapter);
        sourceSpinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override

            //在onItemSelected中parent是你当前所操作的Spinner，当某一个Activity中有多个Spinner时，可以根据
            // parent.getId()与R.id.currentSpinner是否相等，来判断是否你当前操作的Spinner，通过
            // switch...case...语句来解决多个Spinner的情况
            public void onItemSelected(AdapterView<?> parent, View view, int pos, long id) {
                mSourceString = parent.getItemAtPosition(pos).toString();
                Log.v(TAG, "onItemSelected " + mSourceString);
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
                Log.v(TAG, "onNothingSelected");
                mSourceString = null;
            }
        });

        mRadio1 = (RadioButton) findViewById(R.id.radio1);
        mRadio2 = (RadioButton) findViewById(R.id.radio2);

        //此处OnCheckedChangeListener报红的，到头部手动导入下面这个包：
        //import android.widget.CompoundButton.OnCheckedChangeListener;
        OnCheckedChangeListener checklistener = new CompoundButton.OnCheckedChangeListener(){

            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                Log.i("@@@", "onCheckedChanged");
                if (buttonView == mRadio1 && isChecked){
                    mRadio2.setChecked(false);
                }
                if (buttonView == mRadio2 && isChecked){
                    mRadio1.setChecked(false);
                }
                if (isChecked){
                    if (mRadio1.isChecked()){
                        if (mSurfaceHolder1VideoSink == null){
                            mSurfaceHolder1VideoSink = new SurfaceHolderVideoSink(mSurfaceHolder1);
                        }
                        mSelectedVideoSink = mSurfaceHolder1VideoSink;
                        mGLView1.onPause();
                        Log.i("@@@", "glview pause");
                    }else{
                        mGLView1.onResume();
                        if (mGLView1VideoSink == null){
                            mGLView1VideoSink = new GLViewVideoSink(mGLView1);
                        }
                        mSelectedVideoSink = mGLView1VideoSink;
                    }
                    switchSurface();
                }
            }
        };

        mRadio1.setOnCheckedChangeListener(checklistener);
        mRadio2.setOnCheckedChangeListener(checklistener);
        mRadio2.toggle();

        //与单选按钮相比，suefaces更容易成为目标
        mSurfaceView1.setOnClickListener(new View.OnClickListener(){

            @Override
            public void onClick(View v) {
                mRadio1.toggle();
            }
        });
        mGLView1.setOnClickListener(new View.OnClickListener(){

            @Override
            public void onClick(View view) {
                mRadio2.toggle();
            }
        });

        //初始化按钮单击处理
        //native MediaPlayer start/pause
        ((Button) findViewById(R.id.start_native)).setOnClickListener(new View.OnClickListener(){

            @Override
            public void onClick(View view) {
                if (!mCreated){
                    if (mNativeCodecPlayerVideoSink == null){
                        if (mSelectedVideoSink == null){
                            return;
                        }
                        mSelectedVideoSink.useAsSinkForNative();
                        mNativeCodecPlayerVideoSink = mSelectedVideoSink;
                    }
                    if (mSourceString != null){
                        mCreated = createStreamingMediaPlayer(getResources().getAssets(),mSourceString);
                    }
                }
                if (mCreated){
                    mIsPlaying = !mIsPlaying;
                    setPlayingStreamingMediaPlayer(mIsPlaying);
                }
            }
        });

        //native MediaPlayer rewind
        ((Button) findViewById(R.id.rewind_native)).setOnClickListener(new View.OnClickListener(){

            @Override
            public void onClick(View view) {
                if (mNativeCodecPlayerVideoSink != null){
                    rewindStreamingMediaPlayer();
                }
            }

        });
    }

    void switchSurface(){
        if (mCreated && mNativeCodecPlayerVideoSink != mSelectedVideoSink){

            //关闭并在其他surface上重新创建
            Log.i("@@@", "shutting down player");
            shutdown();
            mCreated = false;
            mSelectedVideoSink.useAsSinkForNative();
            mNativeCodecPlayerVideoSink = mSelectedVideoSink;
            if (mSourceString != null){
                Log.i("@@@", "recreating player");
                mCreated = createStreamingMediaPlayer(getResources().getAssets(), mSourceString);
                mIsPlaying = false;
            }
        }
    }

    /** 在活动即将暂停时调用 */
    @Override
    protected void onPause(){
        mIsPlaying = false;
        setPlayingStreamingMediaPlayer(false);
        mGLView1.onPause();
        super.onPause();
    }

    @Override
    protected void onResume(){
        super.onResume();
        if (mRadio2.isChecked()){
            mGLView1.onResume();
        }
    }

    /** 当活动即将销毁时调用 */
    @Override
    protected void onDestroy(){
        shutdown();
        mCreated = false;
        super.onDestroy();
    }

    private MyGLSurfaceView mGLView1;
    private RadioButton mRadio1;
    private RadioButton mRadio2;

    public static native void createEngine();
    //create 创建 Streaming 流媒体 Media  Player播放
    //createStreamingMediaPlayer创建流媒体播放
    //以下为native方法

    public static native boolean  createStreamingMediaPlayer(AssetManager asstMgr, String filename);
    public static native void setPlayingStreamingMediaPlayer(boolean isPlaying);
    public static native void shutdown();
    public static native void setSurface(Surface surface);
    public static native void rewindStreamingMediaPlayer();

    //程序启动时加载native-lib库
    static {
        System.loadLibrary("native-lib");
    }


    //videosink提取了surface和SurfaceTexure之间的区别
    //或者说是surfaveholder和GLSurfaceView
    static abstract class VideoSink{

        //abstract抽象类
        abstract void setFixedSize(int width, int height);
        abstract void useAsSinkForNative();
    }

    //SurfaceHolderVideoSink在此处继承是为了调用setFixedSize的宽高
    static class SurfaceHolderVideoSink extends VideoSink{

        private final SurfaceHolder mSurfaceHolder;

        SurfaceHolderVideoSink(SurfaceHolder surfaceHolder){
            mSurfaceHolder = surfaceHolder;
        }

        @Override
        void setFixedSize(int width, int height){
            mSurfaceHolder.setFixedSize(width, height);
        }

        @Override
        void useAsSinkForNative(){
            Surface s = mSurfaceHolder.getSurface();
            Log.i("@@@", "setting surface" + s);
            setSurface(s);
        }
    }

    static class GLViewVideoSink extends VideoSink{

        private final MyGLSurfaceView mMyGLSurfaceView;

        GLViewVideoSink(MyGLSurfaceView myGLSurfaceView){
            mMyGLSurfaceView = myGLSurfaceView;
        }

        @Override
        void setFixedSize(int width, int height) {

        }

        @Override
        void useAsSinkForNative() {
            SurfaceTexture st = mMyGLSurfaceView.getSurfaceTexture();
            Surface s = new Surface(st);
            setSurface(s);
            s.release();
        }
    }




}
