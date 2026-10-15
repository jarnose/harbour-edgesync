#pragma once

#include "gfdi/bytes.h"

#include <string>

namespace Gfdi {

// GFDI message types (the "5xxx" numbers). Only the ones this app speaks or has
// to answer are named; everything else is acknowledged as unsupported.
enum MessageType : uint16_t {
    MsgResponse = 5000,
    MsgDownloadRequest = 5002,
    MsgUploadRequest = 5003,
    MsgFileTransferData = 5004,
    MsgCreateFile = 5005,
    MsgFilter = 5007,
    MsgSetFileFlag = 5008,
    MsgFileAvailable = 5009,
    MsgFitDefinition = 5011,
    MsgFitData = 5012,
    MsgWeatherRequest = 5014,
    MsgBatteryStatus = 5023,
    MsgDeviceInformation = 5024,
    MsgDeviceSettings = 5026,
    MsgSystemEvent = 5030,
    MsgSupportedFileTypes = 5031,
    MsgNotificationSubscription = 5036,
    MsgSynchronization = 5037,
    MsgFindMyPhoneRequest = 5039,
    MsgProtobufRequest = 5043,
    MsgProtobufResponse = 5044,
    MsgConfiguration = 5050,
    MsgCurrentTimeRequest = 5052,
    MsgAuthNegotiation = 5101,
};

// The status byte of a Response.
enum Status : uint8_t {
    StatusAck = 0,
    StatusNak = 1,
    StatusUnsupported = 2,
    StatusDecodeError = 3,
    StatusCrcError = 4,
    StatusLengthError = 5,
};

// One GFDI message: [size:2][type:2][payload][crc:2], little-endian, where size
// counts all of it and the CRC covers everything before itself.
struct Frame
{
    uint16_t type = 0;
    Bytes payload;
};

// Checks length and CRC and splits the frame. A type with the top bit set
// carries a sequence number in its high byte and the real type, minus 5000, in
// its low byte; that is undone here. On failure `error` says which check failed.
bool parseFrame(const Bytes &raw, Frame &out, std::string &error);

Bytes buildFrame(uint16_t type, const Bytes &payload = Bytes());

// A Response (5000) to a message of `originalType`: original type, status, then
// whatever `extra` the answer carries.
Bytes buildResponse(uint16_t originalType, Status status, const Bytes &extra = Bytes());

} // namespace Gfdi
