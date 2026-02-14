#pragma once
#include <cstddef>
#include <array>

enum platform{
    eIPhoneDevice   = 0,
    eIPadDevice     = 1,
    eAndroidPhone   = 2,
    eAndroidTablet  = 3,
};

enum operationId{
    HANDSHAKE           = 0,
    SUBSCRIBE_UPDATE    = 1,
    SUBSCRIBE_SPOT      = 2,
    DISMISS             = 3,
};

struct handshake{
    platform    id;
    int         ver;
    operationId opId;
};

struct handshakeResponse{
    char carName[50];
    char driverName[50];
    int identifier;
    int version;
    char trackName[50];
    char trackConfig[50];
};

struct RTLap{
    int carIdentifierNumber;
    int lap;
    char driverName[50];
    char carName[50];
    int time;
};

struct RTCarInfo{
    char identifier;
    int size;

    float speed_Kmh;
    float speed_Mph;
    float speed_Ms;

    bool isAbsEnabled;
    bool isAbsInAction;
    bool isTcInAction;
    bool isTcEnabled;
    bool isInPit;
    bool isEngineLimiterOn;

    float accG_vertical;
    float accG_horizontal;
    float accG_frontal;

    int lapTime;
    int lastLap;
    int bestLap;
    int lapCount;

    float gas;
    float brake;
    float clutch;
    float engineRPM;
    float steer;
    int gear;
    float cgHeight;

    float wheelAngularSpeed[4];
    float slipAngle[4];
    float slipAngle_ContactPatch[4];
    float slipRatio[4];
    float tyreSlip[4];
    float ndSlip[4];
    float load[4];
    float Dy[4];
    float Mz[4];
    float tyreDirtyLevel[4];

    float camberRAD[4];
    float tyreRadius[4];
    float tyreLoadedRadius[4];

    float suspensionHeight[4];
    float carPositionNormalized;
    float carSlope;
    float carCoordinates[3];
};

struct FieldInfo {
    const char* name;
    std::size_t offset;
    std::size_t size;
};

#define FIELD(struct_type, member) \
    FieldInfo{ \
        #member, \
        offsetof(struct_type, member), \
        sizeof(((struct_type*)0)->member) \
    }

#define FIELD_PART(struct_type, member, byte_offset, byte_size) \
    FieldInfo{ \
        #member, \
        offsetof(struct_type, member) + static_cast<std::size_t>(byte_offset), \
        static_cast<std::size_t>(byte_size) \
    }

constexpr std::array<FieldInfo, 58> RTCarInfo_Fields = {
    FIELD(RTCarInfo, identifier),
    FIELD(RTCarInfo, size),
    FIELD(RTCarInfo, speed_Kmh),
    FIELD(RTCarInfo, speed_Mph),
    FIELD(RTCarInfo, speed_Ms),
    FIELD(RTCarInfo, isAbsEnabled),
    FIELD(RTCarInfo, isAbsInAction),
    FIELD(RTCarInfo, isTcInAction),
    FIELD(RTCarInfo, isTcEnabled),
    FIELD(RTCarInfo, isInPit),
    FIELD(RTCarInfo, isEngineLimiterOn),
    FIELD(RTCarInfo, accG_vertical),
    FIELD(RTCarInfo, accG_horizontal),
    FIELD(RTCarInfo, accG_frontal),
    FIELD(RTCarInfo, lapTime),
    FIELD(RTCarInfo, lastLap),
    FIELD(RTCarInfo, bestLap),
    FIELD(RTCarInfo, lapCount),
    FIELD(RTCarInfo, gas),
    FIELD(RTCarInfo, brake),
    FIELD(RTCarInfo, clutch),
    FIELD(RTCarInfo, engineRPM),
    FIELD(RTCarInfo, steer),
    FIELD(RTCarInfo, gear),
    FIELD(RTCarInfo, cgHeight),
    FIELD_PART(RTCarInfo, wheelAngularSpeed, 0, 8),
    FIELD_PART(RTCarInfo, wheelAngularSpeed, 8, 8),
    FIELD_PART(RTCarInfo, slipAngle, 0, 8),
    FIELD_PART(RTCarInfo, slipAngle, 8, 8),
    FIELD_PART(RTCarInfo, slipAngle_ContactPatch, 0, 8),
    FIELD_PART(RTCarInfo, slipAngle_ContactPatch, 8, 8),
    FIELD_PART(RTCarInfo, slipRatio, 0, 8),
    FIELD_PART(RTCarInfo, slipRatio, 8, 8),
    FIELD_PART(RTCarInfo, tyreSlip, 0, 8),
    FIELD_PART(RTCarInfo, tyreSlip, 8, 8),
    FIELD_PART(RTCarInfo, ndSlip, 0, 8),
    FIELD_PART(RTCarInfo, ndSlip, 8, 8),
    FIELD_PART(RTCarInfo, load, 0, 8),
    FIELD_PART(RTCarInfo, load, 8, 8),
    FIELD_PART(RTCarInfo, Dy, 0, 8),
    FIELD_PART(RTCarInfo, Dy, 8, 8),
    FIELD_PART(RTCarInfo, Mz, 0, 8),
    FIELD_PART(RTCarInfo, Mz, 8, 8),
    FIELD_PART(RTCarInfo, tyreDirtyLevel, 0, 8),
    FIELD_PART(RTCarInfo, tyreDirtyLevel, 8, 8),
    FIELD_PART(RTCarInfo, camberRAD, 0, 8),
    FIELD_PART(RTCarInfo, camberRAD, 8, 8),
    FIELD_PART(RTCarInfo, tyreRadius, 0, 8),
    FIELD_PART(RTCarInfo, tyreRadius, 8, 8),
    FIELD_PART(RTCarInfo, tyreLoadedRadius, 0, 8),
    FIELD_PART(RTCarInfo, tyreLoadedRadius, 8, 8),
    FIELD_PART(RTCarInfo, suspensionHeight, 0, 8),
    FIELD_PART(RTCarInfo, suspensionHeight, 8, 8),
    FIELD(RTCarInfo, carPositionNormalized),
    FIELD(RTCarInfo, carSlope),
    FIELD_PART(RTCarInfo, carCoordinates, 0, 4),
    FIELD_PART(RTCarInfo, carCoordinates, 4, 4),
    FIELD_PART(RTCarInfo, carCoordinates, 8, 4)
};
