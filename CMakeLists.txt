cmake_minimum_required(VERSION 3.10)
project(qq_ai_reply)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSYS2 MinGW64 路径
set(MSYS2_PREFIX "C:/Users/admin/scoop/apps/msys2/current/mingw64")

# 静态链接 gcc 运行时
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")

# libcurl
set(CURL_INCLUDE_DIR "${MSYS2_PREFIX}/include")
set(CURL_LIBRARY "${MSYS2_PREFIX}/lib/libcurl.dll.a")

add_executable(qq_ai_reply
    src/main.cpp
    src/config.cpp
    src/http_server.cpp
    src/onebot_api.cpp
    src/ai_client.cpp
    src/note.cpp
)

target_include_directories(qq_ai_reply PRIVATE ${CURL_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/thirdparty)
target_link_libraries(qq_ai_reply ${CURL_LIBRARY} ws2_32 wldap32 crypt32)

# AI 接口独立测试程序
add_executable(test_ai
    src/test_ai.cpp
    src/ai_client.cpp
    src/config.cpp
)
target_include_directories(test_ai PRIVATE ${CURL_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/thirdparty)
target_link_libraries(test_ai ${CURL_LIBRARY} ws2_32 wldap32 crypt32)
