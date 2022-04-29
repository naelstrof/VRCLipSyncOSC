#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <thread>
#include <string>
#include "SRanipal.h"
#include "SRanipal_Eye.h"
#include "SRanipal_Lip.h"
#include "SRanipal_Enums.h"
#include "SRanipal_NotRelease.h"
#include "oscpp/client.hpp"
#include <lua.hpp>
#include <iostream>
#include <map>

#include <winsock2.h>

#pragma comment (lib, "SRanipal.lib")
using namespace ViveSR;

#define EnableEyeTracking 0
#define DisableEyeTracking 1

std::string ConvertLipCode(ViveSR::anipal::Lip::Version2::LipShape_v2 lipCode);
std::string CovertErrorCode(int error);

std::map<const char*, float> VRCData;
static int SetVRCData(lua_State* lua_state) {
    const char* name = lua_tostring(lua_state, -2);
    float num = lua_tonumber(lua_state, -1);
    VRCData[name] = num;
    return 0;
}
char networkBuffer[1024];
void SendData(std::map<const char*, float> data, SOCKET socket) {
    OSCPP::Client::Packet packet(networkBuffer, 1024);
    packet.openBundle(1234ULL);
    for (const auto& kv : data) {
        packet.openMessage(kv.first, 1).float32(kv.second).closeMessage();
    }
    packet.closeBundle();
    int result = send(socket, networkBuffer, packet.size(), 0);
    if (result == SOCKET_ERROR) {
        std::cerr << "Failed to send packet to VRChat with error code " << WSAGetLastError() << "\n" << std::flush;
        throw "Aborting...\n";
    }
}
char lip_image[800 * 400];
bool running = true;
int main(int argc, char** argv) {
    // connect to vrchat -------------
    int iResult;
    WSADATA wsaData;
    SOCKET ConnectSocket = INVALID_SOCKET;
    struct sockaddr_in clientService;

    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != NO_ERROR) {
        wprintf(L"WSAStartup failed with error: %d\n", iResult);
        return 1;
    }

    // Create a SOCKET for connecting to server
    ConnectSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (ConnectSocket == INVALID_SOCKET) {
        wprintf(L"socket failed with error: %ld\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // The sockaddr_in structure specifies the address family,
    // IP address, and port of the server to be connected to.
    clientService.sin_family = AF_INET;
    clientService.sin_addr.s_addr = inet_addr("127.0.0.1");
    clientService.sin_port = htons(9000);

    //----------------------
    // Connect to server.
    iResult = connect(ConnectSocket, (SOCKADDR*)&clientService, sizeof(clientService));
    if (iResult == SOCKET_ERROR) {
        wprintf(L"connect failed with error: %d\n", WSAGetLastError());
        closesocket(ConnectSocket);
        WSACleanup();
        return 1;
    }

    // initialize liptracking ---------
    int error = ViveSR::anipal::Initial(ViveSR::anipal::Lip::ANIPAL_TYPE_LIP_V2, NULL);
    if (error == ViveSR::Error::WORK) {
        std::cout << "Successfully initialize version2 Lip engine.\n" << std::flush;
    } else {
        std::cout << "Failed to initialize version2 Lip engine. Please refer to the code " << error << " " << CovertErrorCode(error).c_str() << "\n" << std::flush;
        return 1;
    }

    // create new Lua state -----------
    lua_State* lua_state;
    lua_state = luaL_newstate();
    luaL_openlibs(lua_state);

    try {
        lua_register(lua_state, "SendData", SetVRCData);
        // run the Lua script
        if (luaL_dofile(lua_state, "config.lua") != LUA_OK) {
            throw lua_tostring(lua_state,-1);
        } else {
            std::cout << "Successfully loaded config.lua.\n" << std::flush;
        }

        ViveSR::anipal::Lip::LipData_v2 lip_data_v2;
        lip_data_v2.image = lip_image;
        int result = ViveSR::Error::WORK;
        while (running) {
            result = ViveSR::anipal::Lip::GetLipData_v2(&lip_data_v2);
            if (result == ViveSR::Error::WORK) {
                float* weightings = lip_data_v2.prediction_data.blend_shape_weight;
                lua_getglobal(lua_state, "update");
                if (!lua_isfunction(lua_state, -1)) {
                    lua_pop(lua_state, 1);
                    throw "ERROR: update function not found in config.lua\n";
                }
                lua_newtable(lua_state);
                for (int i = ViveSR::anipal::Lip::Version2::Jaw_Right; i < ViveSR::anipal::Lip::Version2::Max; i++) {
                    lua_pushstring(lua_state, ConvertLipCode((ViveSR::anipal::Lip::Version2::LipShape_v2)i).c_str());
                    lua_pushnumber(lua_state, weightings[i]);
                    lua_settable(lua_state, -3);
                }
                if (lua_pcall(lua_state, 1, 0, 0) != LUA_OK) {
                    throw lua_tostring(lua_state,-1);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
            SendData(VRCData, ConnectSocket);
        }
    } catch (std::exception& e) {
        std::cerr << e.what() << std::flush;
    } catch (const char* msg) {
        std::cerr << msg << std::flush;
    }
    // close the Lua state
    lua_close(lua_state);
    // release the lip sync tracking.
    ViveSR::anipal::Release(ViveSR::anipal::Lip::ANIPAL_TYPE_LIP_V2);
}

std::string ConvertLipCode(ViveSR::anipal::Lip::Version2::LipShape_v2 lipShape) {
    std::string result = "";
    switch (lipShape) {
        case ViveSR::anipal::Lip::Version2::None: result = "None"; break;
        case ViveSR::anipal::Lip::Version2::Jaw_Right: result = "Jaw_Right"; break;
        case ViveSR::anipal::Lip::Version2::Jaw_Left: result = "Jaw_Left"; break;
        case ViveSR::anipal::Lip::Version2::Jaw_Forward: result = "Jaw_Forward"; break;
        case ViveSR::anipal::Lip::Version2::Jaw_Open: result = "Jaw_Open"; break;
        case ViveSR::anipal::Lip::Version2::Mouth_Ape_Shape: result = "Mouth_Ape_Shape"; break;
        case ViveSR::anipal::Lip::Version2::Mouth_Upper_Right: result = "Mouth_Upper_Right"; break;
        case ViveSR::anipal::Lip::Version2::Mouth_Upper_Left: result = "Mouth_Upper_Left"; break;
        case ViveSR::anipal::Lip::Version2::Mouth_Lower_Right: result = "Mouth_Lower_Right"; break;
        case ViveSR::anipal::Lip::Version2::Mouth_Lower_Left: result = "Mouth_Lower_Left"; break;
        case ViveSR::anipal::Lip::Version2::Mouth_Upper_Overturn: result = "Mouth_Upper_Overturn"; break;
        case ViveSR::anipal::Lip::Version2::Mouth_Lower_Overturn: result = "Mouth_Lower_Overturn"; break;
        case ViveSR::anipal::Lip::Version2::Mouth_Pout: result = "Mouth_Pout"; break;
        case ViveSR::anipal::Lip::Version2::Mouth_Smile_Right: result = "Mouth_Smile_Right"; break;
        case ViveSR::anipal::Lip::Version2::Mouth_Smile_Left: result = "Mouth_Smile_Left"; break;
        case ViveSR::anipal::Lip::Version2::Mouth_Sad_Right: result = "Mouth_Sad_Right"; break;
        case ViveSR::anipal::Lip::Version2::Mouth_Sad_Left: result = "Mouth_Sad_Left"; break;
        case ViveSR::anipal::Lip::Version2::Cheek_Puff_Right: result = "Cheek_Puff_Right"; break;
        case ViveSR::anipal::Lip::Version2::Cheek_Puff_Left: result = "Cheek_Puff_Left"; break;
        case ViveSR::anipal::Lip::Version2::Cheek_Suck: result = "Cheek_Suck"; break;
        case ViveSR::anipal::Lip::Version2::Mouth_Upper_UpRight: result = "Mouth_Upper_UpRight"; break;
        case ViveSR::anipal::Lip::Version2::Mouth_Upper_UpLeft: result = "Mouth_Upper_UpLeft"; break;
        case ViveSR::anipal::Lip::Version2::Mouth_Lower_DownRight: result = "Mouth_Lower_DownRight"; break;
        case ViveSR::anipal::Lip::Version2::Mouth_Lower_DownLeft: result = "Mouth_Lower_DownLeft"; break;
        case ViveSR::anipal::Lip::Version2::Mouth_Upper_Inside: result = "Mouth_Upper_Inside"; break;
        case ViveSR::anipal::Lip::Version2::Mouth_Lower_Inside: result = "Mouth_Lower_Inside"; break;
        case ViveSR::anipal::Lip::Version2::Mouth_Lower_Overlay: result = "Mouth_Lower_Overlay"; break;
        case ViveSR::anipal::Lip::Version2::Tongue_LongStep1: result = "Tongue_LongStep1"; break;
        case ViveSR::anipal::Lip::Version2::Tongue_LongStep2: result = "Tongue_LongStep2"; break;
        case ViveSR::anipal::Lip::Version2::Tongue_Down: result = "Tongue_Down"; break;
        case ViveSR::anipal::Lip::Version2::Tongue_Up: result = "Tongue_Up"; break;
        case ViveSR::anipal::Lip::Version2::Tongue_Right: result = "Tongue_Right"; break;
        case ViveSR::anipal::Lip::Version2::Tongue_Left: result = "Tongue_Left"; break;
        case ViveSR::anipal::Lip::Version2::Tongue_Roll: result = "Tongue_Roll"; break;
        case ViveSR::anipal::Lip::Version2::Tongue_UpLeft_Morph: result = "Tongue_UpLeft_Morph"; break;
        case ViveSR::anipal::Lip::Version2::Tongue_UpRight_Morph: result = "Tongue_UpRight_Morph"; break;
        case ViveSR::anipal::Lip::Version2::Tongue_DownLeft_Morph: result = "Tongue_DownLeft_Morph"; break;
        case ViveSR::anipal::Lip::Version2::Tongue_DownRight_Morph: result = "Tongue_DownRight_Morph"; break;
        case ViveSR::anipal::Lip::Version2::Max: result = "Max"; break;
        default: result = "INVALID"; break;
    }
    return result;
}

std::string CovertErrorCode(int error) {
    std::string result = "";
    switch (error) {
    case(RUNTIME_NOT_FOUND):     result = "RUNTIME_NOT_FOUND"; break;
    case(NOT_INITIAL):           result = "NOT_INITIAL"; break;
    case(FAILED):                result = "FAILED"; break;
    case(WORK):                  result = "WORK"; break;
    case(INVALID_INPUT):         result = "INVALID_INPUT"; break;
    case(FILE_NOT_FOUND):        result = "FILE_NOT_FOUND"; break;
    case(DATA_NOT_FOUND):        result = "DATA_NOT_FOUND"; break;
    case(UNDEFINED):             result = "UNDEFINED"; break;
    case(INITIAL_FAILED):        result = "INITIAL_FAILED"; break;
    case(NOT_IMPLEMENTED):       result = "NOT_IMPLEMENTED"; break;
    case(NULL_POINTER):          result = "NULL_POINTER"; break;
    case(OVER_MAX_LENGTH):       result = "OVER_MAX_LENGTH"; break;
    case(FILE_INVALID):          result = "FILE_INVALID"; break;
    case(UNINSTALL_STEAM):       result = "UNINSTALL_STEAM"; break;
    case(MEMCPY_FAIL):           result = "MEMCPY_FAIL"; break;
    case(NOT_MATCH):             result = "NOT_MATCH"; break;
    case(NODE_NOT_EXIST):        result = "NODE_NOT_EXIST"; break;
    case(UNKONW_MODULE):         result = "UNKONW_MODULE"; break;
    case(MODULE_FULL):           result = "MODULE_FULL"; break;
    case(UNKNOW_TYPE):           result = "UNKNOW_TYPE"; break;
    case(INVALID_MODULE):        result = "INVALID_MODULE"; break;
    case(INVALID_TYPE):          result = "INVALID_TYPE"; break;
    case(MEMORY_NOT_ENOUGH):     result = "MEMORY_NOT_ENOUGH"; break;
    case(BUSY):                  result = "BUSY"; break;
    case(NOT_SUPPORTED):         result = "NOT_SUPPORTED"; break;
    case(INVALID_VALUE):         result = "INVALID_VALUE"; break;
    case(COMING_SOON):           result = "COMING_SOON"; break;
    case(INVALID_CHANGE):        result = "INVALID_CHANGE"; break;
    case(TIMEOUT):               result = "TIMEOUT"; break;
    case(DEVICE_NOT_FOUND):      result = "DEVICE_NOT_FOUND"; break;
    case(INVALID_DEVICE):        result = "INVALID_DEVICE"; break;
    case(NOT_AUTHORIZED):        result = "NOT_AUTHORIZED"; break;
    case(ALREADY):               result = "ALREADY"; break;
    case(INTERNAL):              result = "INTERNAL"; break;
    case(CONNECTION_FAILED):     result = "CONNECTION_FAILED"; break;
    case(ALLOCATION_FAILED):     result = "ALLOCATION_FAILED"; break;
    case(OPERATION_FAILED):      result = "OPERATION_FAILED"; break;
    case(NOT_AVAILABLE):         result = "NOT_AVAILABLE"; break;
    case(CALLBACK_IN_PROGRESS):  result = "CALLBACK_IN_PROGRESS"; break;
    case(SERVICE_NOT_FOUND):     result = "SERVICE_NOT_FOUND"; break;
    case(DISABLED_BY_USER):      result = "DISABLED_BY_USER"; break;
    case(EULA_NOT_ACCEPT):       result = "EULA_NOT_ACCEPT"; break;
    case(RUNTIME_NO_RESPONSE):   result = "RUNTIME_NO_RESPONSE"; break;
    case(OPENCL_NOT_SUPPORT):    result = "OPENCL_NOT_SUPPORT"; break;
    case(NOT_SUPPORT_EYE_TRACKING): result = "NOT_SUPPORT_EYE_TRACKING"; break;
    case(LIP_NOT_SUPPORT):       result = "LIP_NOT_SUPPORT"; break;
    default:
        result = "No such error code";
    }
    return result;
}