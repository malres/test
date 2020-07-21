package com.example.nativecodec;

import android.content.Context;
import android.graphics.SurfaceTexture;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.opengl.Matrix;
import android.os.SystemClock;
import android.util.AttributeSet;
import android.util.Log;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class MyGLSurfaceView extends GLSurfaceView {

    MyRenderer mRenderer;

    public MyGLSurfaceView(Context context){
        this(context, null);
    }

    public MyGLSurfaceView(Context context, AttributeSet attributeSet) {
        super(context,attributeSet);
        init();
    }

    private void init(){
        setEGLContextClientVersion(2);
        mRenderer = new MyRenderer();
        setRenderer(mRenderer);
        Log.i("@@@", "setrenderer");
    }

    @Override
    public void onPause() {
        mRenderer.onPause();
        super.onPause();
    }

    @Override
    public void onResume(){
        super.onResume();
        mRenderer.onResume();
    }

    public SurfaceTexture getSurfaceTexture(){
        return mRenderer.getSurfaceTexturn();
    }
}

class MyRenderer implements GLSurfaceView.Renderer, SurfaceTexture.OnFrameAvailableListener{

    public MyRenderer(){
        mVertices = ByteBuffer.allocateDirect(mVerticesData.length
                * FLOAT_SIZE_BYTES).order(ByteOrder.nativeOrder()).asFloatBuffer();
        mVertices.put(mVerticesData).position(0);

        Matrix.setIdentityM(mSTMatrix, 0);
    }
    public void onPause(){
    }

    public void onResume(){
        mLastTime = SystemClock.elapsedRealtimeNanos();
    }


    @Override
    public void onDrawFrame(GL10 glUnused) {
        synchronized (this){   //synchronized Java中的一种同步锁
            if (updateSurface){
                mSurface.updateTexImage();

                mSurface.getTransformMatrix(mSTMatrix);
                updateSurface = false;
            }
        }

        //忽略传入的GL10接口，并使用GLES20代替类的静态方法
        GLES20.glClear(GLES20.GL_DEPTH_BUFFER_BIT | GLES20.GL_COLOR_BUFFER_BIT);
        GLES20.glUseProgram(mProgram);
        checkGlError("glUseProgram");

        //GL_TEXTURE0 某些常量值
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
        GLES20.glBindTexture(GL_TEXTURE_EXTERNAL_OES, mTextureID);

        mVertices.position(VERTICES_DATA_POS_OFFSET);
        GLES20.glVertexAttribPointer(maPositionHandle, 3,GLES20.GL_FLOAT, false,
                 VERTICES_DATA_STRIDE_BYTES,mVertices);
        checkGlError("glVertexAttribPointer maPosition");
        GLES20.glEnableVertexAttribArray(maPositionHandle);
        checkGlError("glEnableVertexAttribArray maPositionHandle");

        mVertices.position(VERTICES_DATA_UV_OFFSET);
        GLES20.glVertexAttribPointer(maTextureHandle, 3, GLES20.GL_FLOAT, false,
                VERTICES_DATA_STRIDE_BYTES, mVertices);
        checkGlError("glVertexAttribPointer maTextureHandle");

        //如果少了下面两语句，则第二个幕布播放失败
        GLES20.glEnableVertexAttribArray(maTextureHandle);
        checkGlError("glEnableVertexAttribArray maTextureHandle");


        //SystemClock获取系统时间、运行时间的工具类
        //elapsedRealtimeNanos设备开机到现在的时间，单位是纳秒,含系统深度睡眠时间
        long now = SystemClock.elapsedRealtimeNanos();
        mRunTime += (now - mLastTime);
        mLastTime = now;
        double d = ((double)mRunTime) / 1000000000;
        Matrix.setIdentityM(mMMatrix, 0);       //Matrix 用于矩阵计算
        //三角函数的计算
        Matrix.rotateM(mMMatrix, 0, 30, (float)Math.sin(d), (float)Math.cos(d), 0);
        Matrix.multiplyMM(mMVPMatrix, 0, mVmatrix, 0, mMMatrix, 0);
        Matrix.multiplyMM(mMVPMatrix, 0, mProjMatrix, 0, mMVPMatrix, 0);

        GLES20.glUniformMatrix4fv(muMVPMatrixHandle, 1, false,mMVPMatrix, 0);
        GLES20.glUniformMatrix4fv(muSTMatrixHandle, 1, false, mSTMatrix, 0);

        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP,0, 4);
        checkGlError("glDrawArrays");
    }

    @Override
    public void onSurfaceChanged(GL10 glUnused, int width, int height){

        //忽略传入的GL10接口，并使用GLES20接口代替类的静态方法
        GLES20.glViewport(0, 0,width, height);
        mRatio = (float) width / height;
        Matrix.frustumM(mProjMatrix, 0, -mRatio, mRatio, -1, 1, 3, 7);
    }

    @Override
    public void onSurfaceCreated(GL10 glUnused, EGLConfig config) {

        //设置Alpha混合和Android背景颜色
        GLES20.glEnable(GLES20.GL_BLEND);
        GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, GLES20.GL_ONE_MINUS_SRC_ALPHA);
        GLES20.glClearColor(0.643f, 0.776f, 0.223f, 1.0f);

        //设置着色器并处理其变量
        mProgram = createProgram(mVertexShader, mFargmentShader);
        if (mProgram == 0){
            return;
        }
        maPositionHandle = GLES20.glGetAttribLocation(mProgram, "aPosition");
        checkGlError("glGetAttribLocation aPosition");
        if (maPositionHandle == -1){
            throw new RuntimeException("Could not get attrib location for aPosition");
        }
        maTextureHandle = GLES20.glGetAttribLocation(mProgram, "aTextureCoord");
        checkGlError("glGetAttribLocation aTextureCoord");
        if (maTextureHandle == -1){
            throw new RuntimeException("Could not get attrib location for aTextureCoord");
        }

        muMVPMatrixHandle = GLES20.glGetUniformLocation(mProgram,"uMVPMatrix");
        checkGlError("glGetUniformLocation uMVPMatrix");
        if (muMVPMatrixHandle == -1){
            throw new RuntimeException("Could not get attrib location for uMVPMatrix");
        }

        muSTMatrixHandle = GLES20.glGetUniformLocation(mProgram,"uSTMatrix");
        checkGlError("glGetUniformLocation uMVPMatrix");
        if (muMVPMatrixHandle == -1){
            throw new RuntimeException("Could not get attrib location for uMVPMatrix");
        }

        muSTMatrixHandle = GLES20.glGetUniformLocation(mProgram,"uSTMatrix");
        checkGlError("glGetUniformLocation uSTMatrix");
        if (muSTMatrixHandle == -1){
            throw new RuntimeException("Could not get attrib location for uSTMatrix");
        }

        //每次创建曲面时都必须创建纹理
        int[] textures = new int[1];
        GLES20.glGenTextures(1, textures, 0);

        mTextureID = textures[0];
        GLES20.glBindTexture(GL_TEXTURE_EXTERNAL_OES, mTextureID);
        checkGlError("glBindTexture mTextureID");

        //无法使用相机源进行映射
        //GL_REPEAT:坐标的整数部分被忽略，重复纹理，这是OpenGL纹理默认的处理方式.
        //GL_MIRRORED_REPEAT: 纹理也会被重复，但是当纹理坐标的整数部分是奇数时会使用镜像重复.
        //GL_CLAMP_TO_EDGE: 坐标会被截断到[0,1]之间。结果是坐标值大的被截断到纹理的边缘部分，
        // 形成了一个拉伸的边缘(stretched edge pattern).
        //GL_CLAMP_TO_BORDER: 不在[0,1]范围内的纹理坐标会使用用户指定的边缘颜色.
        //glTexParameterf 函数参数的含义
        //GL_TEXTURE_MIN_FILTER 设置最小过滤，第三个参数决定用什么过滤,MAG则是最大值
        //GL_TEXTURE_WRAP_S；纹理坐标一般用str表示，分别对应xyz，2d纹理用st表示
        GLES20.glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MIN_FILTER,
                GLES20.GL_NEAREST);
        GLES20.glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MAG_FILTER,
                GLES20.GL_LINEAR);
        GLES20.glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_S,
                GLES20.GL_CLAMP_TO_EDGE);
        GLES20.glTexParameterf(GL_TEXTURE_EXTERNAL_OES,GLES20.GL_TEXTURE_WRAP_T,
                GLES20.GL_CLAMP_TO_EDGE);
        checkGlError("glTexParameteri mTextureID");

        //创建提供textureID的SurfaceTexture,并将其传递给相机
        mSurface = new SurfaceTexture(mTextureID);
        mSurface.setOnFrameAvailableListener(this);

        Matrix.setLookAtM(mVmatrix, 0, 0, 0, 4f,
                0f, 0f, 0f,0f,1.0f, 0.0f);

        //synchronized，可以确保线程互斥的访问同步代码
        synchronized (this){
            updateSurface = false;
        }
    }



    @Override
    public void onFrameAvailable(SurfaceTexture surface) {
        //为简单起见，SurfaceTexture在有新数据可用时在此处调用。
        //调用可能来自某个随机线程，因此请放心使用同步。
        // 此处无法进行OpenGL调用。
        updateSurface = true;
    }

    private int loadShader(int shaderType, String source){
        int shader = GLES20.glCreateShader(shaderType);
        if (shader != 0){
            GLES20.glShaderSource(shader,source);
            GLES20.glCompileShader(shader);
            int[] compiled = new int[1];
            GLES20.glGetShaderiv(shader,GLES20.GL_COMPILE_STATUS,compiled,0);
            if (compiled[0] == 0){
                Log.e(TAG,"Could not compile shader " + shaderType + ":");
                Log.e(TAG, GLES20.glGetShaderInfoLog(shader));
                shader = 0;
            }
        }
        return shader;
    }

    private int createProgram(String vertexSource, String fragmentSource){
        int vertexShader = loadShader(GLES20.GL_VERTEX_SHADER,vertexSource);
        if (vertexShader == 0){
            return 0;
        }
        int pixelShader = loadShader(GLES20.GL_FRAGMENT_SHADER, fragmentSource);
        if (pixelShader == 0){
            return 0;
        }

        int program = GLES20.glCreateProgram();
        if (program != 0){
            GLES20.glAttachShader(program, vertexShader);
            checkGlError("glAttachShader");
            GLES20.glAttachShader(program, pixelShader);
            checkGlError("glAttachShader");
            GLES20.glLinkProgram(program);
            int[] linkStatus = new int[1];
            GLES20.glGetProgramiv(program, GLES20.GL_LINK_STATUS, linkStatus, 0);
            if (linkStatus[0] != GLES20.GL_TRUE){
                Log.e(TAG,"Could not link program:");
                Log.e(TAG, GLES20.glGetProgramInfoLog(program));
                GLES20.glDeleteProgram(program);
                program = 0;
            }
        }
        return  program;
    }





    private void checkGlError(String op){
        int error;
        while ((error = GLES20.glGetError()) != GLES20.GL_NO_ERROR){
            Log.e(TAG, op + ": glError " + error);
            throw new RuntimeException(op + ": glError " + error);
        }
    }

    private static final int FLOAT_SIZE_BYTES = 4;
    private static final int VERTICES_DATA_STRIDE_BYTES = 5 * FLOAT_SIZE_BYTES;
    private static final int VERTICES_DATA_POS_OFFSET = 0;
    private static final int VERTICES_DATA_UV_OFFSET = 3;
    private final float[] mVerticesData = {
            //X, Y, Z, U, V
            -1.25f, -1.0f, 0, 0.f, 0.f,
            1.25f, -1.0f, 0, 1.f, 0.f,
            -1.25f,  1.0f, 0, 0.f, 1.f,
            1.25f,  1.0f, 0, 1.f, 1.f,
    };

    private FloatBuffer mVertices;

    private final String mVertexShader =
            "uniform mat4 uMVPMatrix;\n" +
            "uniform mat4 uSTMatrix;\n" +
            "attribute vec4 aPosition;\n" +
            "attribute vec4 aTextureCoord;\n" +
            "varying vec2 vTextureCoord;\n" +
            "void main() {\n" +
            "  gl_Position = uMVPMatrix * aPosition;\n" +
            "  vTextureCoord = (uSTMatrix * aTextureCoord).xy;\n" +
            "}\n";

    private final String mFargmentShader =
            "#extension GL_OES_EGL_image_external : require\n" +
            "precision mediump float;\n" +
            "varying vec2 vTextureCoord;\n" +
            "uniform samplerExternalOES sTexture;\n" +
            "void main() {\n" +
            "  gl_FragColor = texture2D(sTexture, vTextureCoord);\n" +
            "}\n";

    private float[] mMVPMatrix = new float[16];
    private float[] mProjMatrix = new float[16];
    private float[] mMMatrix = new float[16];
    private float[] mVmatrix = new float[16];
    private float[] mSTMatrix = new float[16];

    private int mProgram;
    private int mTextureID;
    private int muMVPMatrixHandle;
    private int muSTMatrixHandle;
    private int maPositionHandle;
    private int maTextureHandle;

    private float mRatio = 1.0f;
    private SurfaceTexture mSurface;
    private boolean updateSurface = false;
    private long mLastTime = -1;
    private long mRunTime = 0;

    private static final String TAG = "MyRenderer";

    //Magic key
    private static final int GL_TEXTURE_EXTERNAL_OES = 0x8D65;

    public SurfaceTexture getSurfaceTexturn(){
        return mSurface;
    }

}
