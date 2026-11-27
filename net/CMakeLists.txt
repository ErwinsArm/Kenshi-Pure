# KenshiPure network code: protocol codec, relay server, test bot and their tests.
# Built by scripts/build-net.ps1 with the toolchain from scripts/setup-modern.ps1.
cmake_minimum_required(VERSION 3.25)
project(KenshiPureNet LANGUAGES C CXX)

set(ENET_DIR "" CACHE PATH "ENet 1.3.18 source folder (tools/modern/enet-1.3.18)")
if(NOT EXISTS "${ENET_DIR}/include/enet/enet.h")
  message(FATAL_ERROR "ENET_DIR does not hold ENet sources: '${ENET_DIR}'. Run scripts/setup-modern.ps1.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static CRT: the programs run on PCs without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(enet STATIC
  ${ENET_DIR}/callbacks.c ${ENET_DIR}/compress.c ${ENET_DIR}/host.c ${ENET_DIR}/list.c
  ${ENET_DIR}/packet.c ${ENET_DIR}/peer.c ${ENET_DIR}/protocol.c ${ENET_DIR}/win32.c)
target_include_directories(enet PUBLIC ${ENET_DIR}/include)
target_compile_definitions(enet PRIVATE _WINSOCK_DEPRECATED_NO_WARNINGS)
target_compile_options(enet PRIVATE /W1 /wd5287)   # third-party code: keep its warnings out of our output
target_compile_definitions(enet PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(enet PUBLIC ws2_32 winmm)

# The codec has no network or game dependency (it goes into the client core at M3).
add_library(kp_proto STATIC proto/Protocol.cpp)
target_include_directories(kp_proto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(kp_proto PRIVATE /W4 /WX)

add_library(kp_net STATIC common/Enet.cpp server/RelayServer.cpp client/NetClient.cpp)
target_link_libraries(kp_net PUBLIC kp_proto enet)
target_compile_options(kp_net PRIVATE /W4 /WX)

add_executable(kp-server server/main.cpp)
target_link_libraries(kp-server PRIVATE kp_net)
add_executable(kp-bot bot/main.cpp)
target_link_libraries(kp-bot PRIVATE kp_net)
foreach(t kp-server kp-bot)
  target_compile_options(${t} PRIVATE /W4 /WX)
endforeach()

enable_testing()
add_executable(kp-proto-test test/ProtoTest.cpp)
target_link_libraries(kp-proto-test PRIVATE kp_proto)
add_executable(kp-loopback-test test/LoopbackTest.cpp)
target_link_libraries(kp-loopback-test PRIVATE kp_net)
foreach(t kp-proto-test kp-loopback-test)
  target_compile_options(${t} PRIVATE /W4 /WX)
  add_test(NAME ${t} COMMAND ${t})
endforeach()
set_tests_properties(kp-loopback-test PROPERTIES TIMEOUT 60)
