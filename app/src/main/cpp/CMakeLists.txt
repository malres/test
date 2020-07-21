#声明要求的cmake最低版本
cmake_minimum_required(VERSION 3.4.1)

add_library( # Sets the name of the library.库名称
             native-lib

             # Sets the library as a shared library.设置为共享库
             SHARED

             looper.cpp

             # Provides a relative path to your source file(s).源文件的相对路径
             native-lib.cpp )

find_library( # 设置路径变量名称
              log-lib

              # 指定NDK名称
              log )

target_link_libraries( native-lib
                       android
                       log
                       mediandk
                       OpenMAXAL)