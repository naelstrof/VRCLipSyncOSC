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
// For ipv6 support
#include <Ws2tcpip.h>
#include <tchar.h>

#pragma comment (lib, "SRanipal.lib")
#pragma comment (lib, "Ws2_32.lib")

using namespace ViveSR;

std::string ConvertLipCode(ViveSR::anipal::Lip::Version2::LipShape_v2 lipCode);
std::string CovertErrorCode(int error);
void lua_pushlips(lua_State* lua_state, ViveSR::anipal::Lip::LipData_v2& lip_data_v2);
void lua_pusheye(lua_State* lua_state, ViveSR::anipal::Eye::SingleEyeData& targetEye, ViveSR::anipal::Eye::SingleEyeExpression& targetExpression);
void lua_pusheyes(lua_State* lua_state, ViveSR::anipal::Eye::EyeData_v2& eye_data_v2);

// Stuff I'd rather keep on the heap instead of the stack.
char networkBuffer[1024];
char lip_image[800 * 400];
static ViveSR::anipal::Eye::EyeData_v2 cachedEyeData;
// Things shared between functions.
std::map<const char*, float> VRCData;
volatile bool running = true;
bool workingEyes = false;
bool workingLips = false;

// We hook this into lua so the user can call it from config.lua. We specifically just store it so we can send a meaningul OSC packet later with SendData()
static int SetVRCData(lua_State* lua_state) {
    const char* name = lua_tostring(lua_state, -2);
    float num = (float)lua_tonumber(lua_state, -1);
    VRCData[name] = num;
    return 0;
}

// This function sends everything in the dictonary out to VRChat, then clears the dictionary.
// FIXME: it has a limit of 1024 bytes so that it doesn't have to fiddle with memory allocs. This is probably fine but it should probably double in size automatically if it hits the limit.
void SendData(std::map<const char*, float>& data, SOCKET socket) {
    // Nothing to send? quit out
    if (data.size() == 0) {
        return;
    }
    OSCPP::Client::Packet packet(networkBuffer, 1024);
    // FIXME: Using a fake timestamp 1234, I believe this is ignored by VRChat, though it would normally be used to sort the incoming packets.
    // If this app ever implements a delta time we could use the high resolution timer to send some good microsecond timestamps instead.
    packet.openBundle(1234ULL);
    // Foreach element in the dictionary, construct a message.
    for (const auto& kv : data) {
        packet.openMessage(kv.first, 1).float32(kv.second).closeMessage();
    }
    packet.closeBundle();
    // Send it!
    int result = send(socket, networkBuffer, (int)packet.size(), 0);
    if (result == SOCKET_ERROR) {
        std::cerr << "Failed to send packet to VRChat with error code " << WSAGetLastError() << "\n" << std::flush;
        throw std::exception("Aborting...");
    }
    data.clear();
}
// We catch specifically SIGINT (ctrl+c), so we can clean up and properly relinquish control of the lipsync api.
void signal_calllback_handler(int signum) {
    std::cerr << "Caught signal " << signum << ", cleanly shutting down...\n" << std::flush;
    running = false;
}
// Windows version of SIGINT, apparently :vomit:
BOOL WINAPI consoleHandler(_In_ DWORD signal) {
    // Pass signal to another signal handler
    if (signal != CTRL_C_EVENT) {
        return FALSE;
    }
    std::cerr << "Caught signal " << signal << ", cleanly shutting down...\n" << std::flush;
    running = false;
    return TRUE;
}

// Eyecallback
void eyeDataReceivedCallback(ViveSR::anipal::Eye::EyeData_v2 const& eye_data) {
    //memcpy(&cachedEyeData, eye_data, sizeof(ViveSR::anipal::Eye::EyeData));
    cachedEyeData = eye_data;
    bool needCalibration = false;
    int error = ViveSR::anipal::Eye::IsUserNeedCalibration(&needCalibration);
    //if (needCalibration) {
        //std::cerr << "SRanipal reports that you need to do eye calibration.\n" << std::flush;
    //}
}

// Main's responsibility right now is to initialize/uninitialize our three apis: An outgoing UDP packet socket, a lua state, and the lipsync api.
int main(int argc, char** argv) {
    int exitCode = EXIT_SUCCESS;
    // connect to vrchat, most of this code was appropriated from https://docs.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-send
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
    // inet_addr only supports ipv4, I know it's overkill to support ipv6 when I only plan to send to localhost, but that warning message (C4996) kept bothering me.
    //clientService.sin_addr.s_addr = inet_addr("127.0.0.1");
    InetPton(AF_INET, _T("127.0.0.1"), &clientService.sin_addr.s_addr);
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

    // create new Lua state -----------
    lua_State* lua_state;
    lua_state = luaL_newstate();
    luaL_openlibs(lua_state);

    try {
        // Give lua our "SendData" command, so the user can call it.
        lua_register(lua_state, "SendData", SetVRCData);
        // run the Lua script config.lua.
        if (luaL_dofile(lua_state, "config.lua") != LUA_OK) {
            throw std::exception(lua_tostring(lua_state,-1));
        } else {
            std::cout << "Successfully loaded config.lua.\n" << std::flush;
        }
        // Quickly check to make sure the update functions are available by pushing them on the lua stack
        lua_getglobal(lua_state, "update");
        if (!lua_isfunction(lua_state, -1)) {
            throw std::exception("ERROR: update function not found in config.lua");
        }
        lua_pop(lua_state, 1);
        
        // We initialize liptracking really late, because it's very slow and also usually launches SteamVR.
        int error = ViveSR::anipal::Initial(ViveSR::anipal::Lip::ANIPAL_TYPE_LIP_V2, NULL);
        if (error == ViveSR::Error::WORK) {
            std::cout << "Successfully initialize version2 Lip engine.\n" << std::flush;
            workingLips = true;
        } else {
            std::cerr << "[Non-fatal] Failed to initialize version2 Lip engine. Please refer to the code " << error << " " << CovertErrorCode(error) << "\n" << std::flush;
        }

        error = ViveSR::anipal::Initial(ViveSR::anipal::Eye::ANIPAL_TYPE_EYE_V2, NULL);
        if (error == ViveSR::Error::WORK) {
            std::cout << "Successfully initialize version2 Eye engine.\n" << std::flush;
            ViveSR::anipal::Eye::RegisterEyeDataCallback_v2(eyeDataReceivedCallback);
            workingEyes = true;
        } else {
            std::cerr << "[Non-fatal] Failed to initialize version2 Eye engine. Please refer to the code " << error << " " << CovertErrorCode(error) << "\n" << std::flush;
        }

        // No data!
        if (!workingLips && !workingEyes) {
            throw std::exception("Aborting due to having no data to send!");
        }
        
        ViveSR::anipal::Lip::LipData_v2 lip_data_v2;
        // we use some data on the heap (lip_image) to store the depth data. This is much better than using the stack.
        lip_data_v2.image = lip_image;

        // Right before we start our loop, we finally subscribe to the SIGINT interrupt. This is so we can cleanly shut down if the user hits ctrl+c.
        typedef void (*SignalHandlerPointer)(int);
        SignalHandlerPointer previousHandler;
        previousHandler = signal(SIGINT, signal_calllback_handler);
        if (previousHandler == SIG_ERR) {
            throw std::exception("Could not set signal handler!");
        }
        if (!SetConsoleCtrlHandler(consoleHandler, TRUE)) {
            throw std::exception("Could not set control handler!");
        }

        std::cout << "Running! Hit Ctrl+C to exit.\n" << std::flush;
        while (running) {
            // We know the update function exists at this point because it was checked earlier.
            lua_getglobal(lua_state, "update");
            // Finally call the function, checking for errors!
            lua_pushlips(lua_state, lip_data_v2);
            lua_pusheyes(lua_state, cachedEyeData);
            if (lua_pcall(lua_state, 2, 0, 0) != LUA_OK) {
                throw std::exception(lua_tostring(lua_state,-1));
            }
            // Lua should've pushed a bunch of stuff into our dictionary. Here we wrap it all up and send it to VRChat.
            SendData(VRCData, ConnectSocket);
            // We sleep for 13 milliseconds so that we can relinquish control back to the OS. Without this we'll eat an entire core!
            std::this_thread::sleep_for(std::chrono::milliseconds(13));
        }
        // Reset the signal handlers
        signal(SIGINT, previousHandler);
        SetConsoleCtrlHandler(consoleHandler, FALSE);
    } catch (std::exception& e) {
        // Just attempt to cleanly report the error before shutting down.
        std::cerr << e.what() << std::flush;
        exitCode = EXIT_FAILURE;
    }
    // Release the lip sync tracking.
    if (workingLips) {
        ViveSR::anipal::Release(ViveSR::anipal::Lip::ANIPAL_TYPE_LIP_V2);
    }
    if (workingEyes) {
        //ViveSR::anipal::Eye::UnregisterEyeDataCallback_v2(EyeCallback_v2);
        ViveSR::anipal::Eye::UnregisterEyeDataCallback_v2(eyeDataReceivedCallback);
        ViveSR::anipal::Release(ViveSR::anipal::Eye::ANIPAL_TYPE_EYE_V2);
    }
    // Close the Lua state
    lua_close(lua_state);
    // Close our connection
    iResult = closesocket(ConnectSocket);
    if (iResult == SOCKET_ERROR) {
        std::cerr << "Close socket failed with error: " << WSAGetLastError() << "\n" << std::flush;
        WSACleanup();
        return 1;
    }
    WSACleanup();
    return exitCode;
}

void lua_pushcombined(lua_State* lua_state, ViveSR::anipal::Eye::CombinedEyeData& combinedEye) {
    lua_newtable(lua_state);
        lua_pushstring(lua_state, "convergenceDistance");
        lua_pushnumber(lua_state, combinedEye.convergence_distance_mm);
        lua_settable(lua_state, -3);

        lua_pushstring(lua_state, "convergenceDistanceValid");
        lua_pushboolean(lua_state, combinedEye.convergence_distance_validity);
        lua_settable(lua_state, -3);

        lua_pushstring(lua_state, "openness");
        lua_pushnumber(lua_state, combinedEye.eye_data.eye_openness);
        lua_settable(lua_state, -3);

        lua_pushstring(lua_state, "gazeDirectionNormalized");
        lua_newtable(lua_state);
            lua_pushstring(lua_state, "x");
            lua_pushnumber(lua_state, combinedEye.eye_data.gaze_direction_normalized.x);
            lua_settable(lua_state, -3);
            lua_pushstring(lua_state, "y");
            lua_pushnumber(lua_state, combinedEye.eye_data.gaze_direction_normalized.y);
            lua_settable(lua_state, -3);
            lua_pushstring(lua_state, "z");
            lua_pushnumber(lua_state, combinedEye.eye_data.gaze_direction_normalized.z);
            lua_settable(lua_state, -3);
        lua_settable(lua_state, -3);

        lua_pushstring(lua_state, "gazeOrigin");
        lua_newtable(lua_state);
            lua_pushstring(lua_state, "x");
            lua_pushnumber(lua_state, combinedEye.eye_data.gaze_origin_mm.x);
            lua_settable(lua_state, -3);
            lua_pushstring(lua_state, "y");
            lua_pushnumber(lua_state, combinedEye.eye_data.gaze_origin_mm.y);
            lua_settable(lua_state, -3);
            lua_pushstring(lua_state, "z");
            lua_pushnumber(lua_state, combinedEye.eye_data.gaze_origin_mm.z);
            lua_settable(lua_state, -3);
        lua_settable(lua_state, -3);

        lua_pushstring(lua_state, "pupilDiameter");
        lua_pushnumber(lua_state, combinedEye.eye_data.pupil_diameter_mm);
        lua_settable(lua_state, -3);

        lua_pushstring(lua_state, "pupilPosition");
        lua_newtable(lua_state);
            lua_pushstring(lua_state, "x");
            lua_pushnumber(lua_state, combinedEye.eye_data.pupil_position_in_sensor_area.x);
            lua_settable(lua_state, -3);
            lua_pushstring(lua_state, "y");
            lua_pushnumber(lua_state, combinedEye.eye_data.pupil_position_in_sensor_area.y);
            lua_settable(lua_state, -3);
        lua_settable(lua_state, -3);

        uint64_t validBitMask = combinedEye.eye_data.eye_data_validata_bit_mask;
        lua_pushstring(lua_state, "gazeOriginValid");
        lua_pushboolean(lua_state, ViveSR::anipal::Eye::DecodeBitMask(validBitMask, ViveSR::anipal::Eye::SingleEyeDataValidity::SINGLE_EYE_DATA_GAZE_ORIGIN_VALIDITY));
        lua_settable(lua_state, -3);
        lua_pushstring(lua_state, "gazeValid");
        lua_pushboolean(lua_state, ViveSR::anipal::Eye::DecodeBitMask(validBitMask, ViveSR::anipal::Eye::SingleEyeDataValidity::SINGLE_EYE_DATA_GAZE_DIRECTION_VALIDITY));
        lua_settable(lua_state, -3);
        lua_pushstring(lua_state, "pupilDiameterValid");
        lua_pushboolean(lua_state, ViveSR::anipal::Eye::DecodeBitMask(validBitMask, ViveSR::anipal::Eye::SingleEyeDataValidity::SINGLE_EYE_DATA_PUPIL_DIAMETER_VALIDITY));
        lua_settable(lua_state, -3);
        lua_pushstring(lua_state, "opennessValid");
        lua_pushboolean(lua_state, ViveSR::anipal::Eye::DecodeBitMask(validBitMask, ViveSR::anipal::Eye::SingleEyeDataValidity::SINGLE_EYE_DATA_EYE_OPENNESS_VALIDITY));
        lua_settable(lua_state, -3);
        lua_pushstring(lua_state, "pupilPositionInSensorAreaValid");
        lua_pushboolean(lua_state, ViveSR::anipal::Eye::DecodeBitMask(validBitMask, ViveSR::anipal::Eye::SingleEyeDataValidity::SINGLE_EYE_DATA_PUPIL_POSITION_IN_SENSOR_AREA_VALIDITY));
        lua_settable(lua_state, -3);
}

void lua_pusheye(lua_State* lua_state, ViveSR::anipal::Eye::SingleEyeData& targetEye, ViveSR::anipal::Eye::SingleEyeExpression& targetExpression) {
    lua_newtable(lua_state);
        // Gaze direction
        lua_pushstring(lua_state, "gazeDirectionNormalized");
        lua_newtable(lua_state);
            lua_pushstring(lua_state, "x");
            lua_pushnumber(lua_state, targetEye.gaze_direction_normalized.x);
            lua_settable(lua_state, -3);
            lua_pushstring(lua_state, "y");
            lua_pushnumber(lua_state, targetEye.gaze_direction_normalized.y);
            lua_settable(lua_state, -3);
            lua_pushstring(lua_state, "z");
            lua_pushnumber(lua_state, targetEye.gaze_direction_normalized.z);
            lua_settable(lua_state, -3);
        lua_settable(lua_state, -3);

        lua_pushstring(lua_state, "openness");
        lua_pushnumber(lua_state, targetEye.eye_openness);
        lua_settable(lua_state, -3);

        // Gaze origin
        lua_pushstring(lua_state, "gazeOrigin");
        lua_newtable(lua_state);
            lua_pushstring(lua_state, "x");
            lua_pushnumber(lua_state, targetEye.gaze_origin_mm.x);
            lua_settable(lua_state, -3);
            lua_pushstring(lua_state, "y");
            lua_pushnumber(lua_state, targetEye.gaze_origin_mm.y);
            lua_settable(lua_state, -3);
            lua_pushstring(lua_state, "z");
            lua_pushnumber(lua_state, targetEye.gaze_origin_mm.z);
            lua_settable(lua_state, -3);
        lua_settable(lua_state, -3);

        lua_pushstring(lua_state, "pupilDiameter");
        lua_pushnumber(lua_state, targetEye.pupil_diameter_mm);
        lua_settable(lua_state, -3);

        lua_pushstring(lua_state, "pupilPosition");
        lua_newtable(lua_state);
            lua_pushstring(lua_state, "x");
            lua_pushnumber(lua_state, targetEye.pupil_position_in_sensor_area.x);
            lua_settable(lua_state, -3);
            lua_pushstring(lua_state, "y");
            lua_pushnumber(lua_state, targetEye.pupil_position_in_sensor_area.y);
            lua_settable(lua_state, -3);
        lua_settable(lua_state, -3);

        // Valid masks
        uint64_t validBitMask = targetEye.eye_data_validata_bit_mask;
        lua_pushstring(lua_state, "gazeOriginValid");
        lua_pushboolean(lua_state, ViveSR::anipal::Eye::DecodeBitMask(validBitMask, ViveSR::anipal::Eye::SingleEyeDataValidity::SINGLE_EYE_DATA_GAZE_ORIGIN_VALIDITY));
        lua_settable(lua_state, -3);
        lua_pushstring(lua_state, "gazeValid");
        lua_pushboolean(lua_state, ViveSR::anipal::Eye::DecodeBitMask(validBitMask, ViveSR::anipal::Eye::SingleEyeDataValidity::SINGLE_EYE_DATA_GAZE_DIRECTION_VALIDITY));
        lua_settable(lua_state, -3);
        lua_pushstring(lua_state, "pupilDiameterValid");
        lua_pushboolean(lua_state, ViveSR::anipal::Eye::DecodeBitMask(validBitMask, ViveSR::anipal::Eye::SingleEyeDataValidity::SINGLE_EYE_DATA_PUPIL_DIAMETER_VALIDITY));
        lua_settable(lua_state, -3);
        lua_pushstring(lua_state, "opennessValid");
        lua_pushboolean(lua_state, ViveSR::anipal::Eye::DecodeBitMask(validBitMask, ViveSR::anipal::Eye::SingleEyeDataValidity::SINGLE_EYE_DATA_EYE_OPENNESS_VALIDITY));
        lua_settable(lua_state, -3);
        lua_pushstring(lua_state, "pupilPositionInSensorAreaValid");
        lua_pushboolean(lua_state, ViveSR::anipal::Eye::DecodeBitMask(validBitMask, ViveSR::anipal::Eye::SingleEyeDataValidity::SINGLE_EYE_DATA_PUPIL_POSITION_IN_SENSOR_AREA_VALIDITY));
        lua_settable(lua_state, -3);

        lua_pushstring(lua_state, "expression");
        lua_newtable(lua_state);
            lua_pushstring(lua_state, "frown");
            lua_pushnumber(lua_state, targetExpression.eye_frown);
            lua_settable(lua_state, -3);

            lua_pushstring(lua_state, "squeeze");
            lua_pushnumber(lua_state, targetExpression.eye_squeeze);
            lua_settable(lua_state, -3);

            lua_pushstring(lua_state, "wide");
            lua_pushnumber(lua_state, targetExpression.eye_wide);
            lua_settable(lua_state, -3);
        lua_settable(lua_state, -3);
}

void lua_pusheyes(lua_State* lua_state, ViveSR::anipal::Eye::EyeData_v2& eye_data_v2) {
    if (!workingEyes) {
        lua_pushnil(lua_state);
        return;
    }

    //int result = ViveSR::anipal::Eye::GetEyeData_v2(&eye_data_v2);
    //if (result != ViveSR::Error::WORK) {
        //lua_pushnil(lua_state);
        //return;
    //}

    // Construct a new table
    lua_newtable(lua_state);
        lua_pushstring(lua_state, "left");
        lua_pusheye(lua_state, eye_data_v2.verbose_data.left, eye_data_v2.expression_data.left);
        lua_settable(lua_state, -3);
        lua_pushstring(lua_state, "right");
        lua_pusheye(lua_state, eye_data_v2.verbose_data.right, eye_data_v2.expression_data.right);
        lua_settable(lua_state, -3);
        lua_pushstring(lua_state, "combined");
        lua_pushcombined(lua_state, eye_data_v2.verbose_data.combined);
        lua_settable(lua_state, -3);
}

void lua_pushlips(lua_State* lua_state, ViveSR::anipal::Lip::LipData_v2& lip_data_v2) {
    if (!workingLips) {
        lua_pushnil(lua_state);
        return;
    }
    int result = ViveSR::anipal::Lip::GetLipData_v2(&lip_data_v2);
    if (result != ViveSR::Error::WORK) {
        lua_pushnil(lua_state);
        return;
    }
    float* weightings = lip_data_v2.prediction_data.blend_shape_weight;
    // Construct a new table
    lua_newtable(lua_state);
    // Loop through all the Version2 enums to store the new data into the table.
    for (int i = ViveSR::anipal::Lip::Version2::Jaw_Right; i < ViveSR::anipal::Lip::Version2::Max; i++) {
        lua_pushstring(lua_state, ConvertLipCode((ViveSR::anipal::Lip::Version2::LipShape_v2)i).c_str());
        lua_pushnumber(lua_state, weightings[i]);
        lua_settable(lua_state, -3);
    }
}

// In C++, enums are truely just raw bytes. Gotta convert them somehow! :clown:
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
        // This should never happen, if it does then there's a critical error that should probably be addressed.
        default: throw std::exception("Unknown lip shape code"); break;
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