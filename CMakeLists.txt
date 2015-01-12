cmake_minimum_required(VERSION 2.8.4)
project(PathSync)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mwindows -DWINVER=0x502 -D_WIN32_IE=0x600 -D_WIN32_WINNT=0x502")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w -std=gnu++0x")
#reduce size
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s -fpermissive -flto -fwhole-program")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections")
set(CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_CXX_STANDARD_LIBRARIES} C:/MinGW/lib/libcomctl32.a")
set(SRC PathSync)
set(WDL WDL)
set(SOURCE_FILES ${SOURCE_FILES} ${SRC}/pathsync.cpp)
set(SOURCE_FILES ${SOURCE_FILES} ${SRC}/res.rc)
set(SOURCE_FILES ${SOURCE_FILES} ${SRC}/fnmatch.cpp)
set(SOURCE_FILES ${SOURCE_FILES} ${WDL}/win32_utf8.c)
set(SOURCE_FILES ${SOURCE_FILES} ${WDL}/WinGUI/wndsize.cpp)
add_executable(PathSync ${SOURCE_FILES})

#upx pack
#add_custom_command(
#    TARGET PathSync POST_BUILD
#    COMMAND upx -9 PathSync.exe)
    add_custom_command(
    TARGET PathSync POST_BUILD
    COMMAND upx --best --lzma PathSync.exe)