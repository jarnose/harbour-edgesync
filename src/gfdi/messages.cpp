#include "gfdi/messages.h"

namespace Gfdi {

namespace {

// Gadgetbridge's OUR_CAPABILITIES for its 120 known bits: all of them except
// GPS_EPHEMERIS_DOWNLOAD (28), LIVETRACK (22), LIVETRACK_AUTO_START (23) and the
// unknown bits 104-111 and 114-119. Computed from the enum order in
// GarminCapability.java; tests/test_messages.cpp pins the bytes.
const uint8_t kOurCapabilities[15] = {
    0xFF, 0xFF, 0x3F, 0xEF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x03,
};

} // namespace

bool parseDeviceInformation(const Bytes &payload, DeviceInfo &out)
{
    Reader r(payload);
    out.protocolVersion = static_cast<uint16_t>(r.u16());
    out.productNumber = static_cast<uint16_t>(r.u16());
    out.unitNumber = r.u32();
    out.softwareVersion = static_cast<uint16_t>(r.u16());
    out.maxPacketSize = static_cast<uint16_t>(r.u16());
    out.bluetoothName = r.str();
    out.deviceName = r.str();
    out.deviceModel = r.str();
    out.classicAddress.clear();
    const bool core = r.ok();
    // What follows is a flag, and with it set the BLE and Classic MAC addresses
    // (little-endian), then one more flag.
    if (core && r.remaining() > 0 && r.u8() == 1) {
        const Bytes ble = r.raw(6);
        const Bytes classic = r.raw(6);
        if (r.ok()) {
            static const char *digits = "0123456789ABCDEF";
            for (int i = 5; i >= 0; --i) {
                out.classicAddress += digits[classic[i] >> 4];
                out.classicAddress += digits[classic[i] & 15];
                if (i)
                    out.classicAddress += ':';
            }
        }
        (void)ble;
    }
    return core;
}

Bytes buildDeviceInformationReply(const DeviceInfo &theirs, const HostInfo &host)
{
    // protocol 1xx devices get a flag set; the rest do not.
    const uint32_t protocolFlags = theirs.protocolVersion / 100 == 1 ? 1 : 0;
    Writer w;
    w.u16(150)       // our protocol version
            .u16(0xFFFF)     // product number: none
            .u32(0xFFFFFFFF) // unit number: none
            .u16(7791)       // software version
            .u16(0xFFFF)     // max packet size: no preference
            .str(host.bluetoothName)
            .str(host.manufacturer)
            .str(host.model)
            .u8(protocolFlags);
    return buildResponse(MsgDeviceInformation, StatusAck, w.bytes());
}

Bytes ourCapabilities()
{
    return Bytes(kOurCapabilities, kOurCapabilities + sizeof(kOurCapabilities));
}

Bytes minimalCapabilities()
{
    // Bit numbers from GarminCapability.java's enum order.
    static const unsigned bits[] = {
        0,  // CONNECT_MOBILE_FIT_LINK
        3,  // SYNC
        4,  // DEVICE_INITIATES_SYNC
        5,  // HOST_INITIATED_SYNC_REQUESTS
        29, // EXPLICIT_ARCHIVE
        42, 43, 44, 45, 46, 47, // SPORT_GENERIC, RUNNING, CYCLING, TRANSITION, FITNESS_EQUIPMENT, SWIMMING
        55, // GARMIN_DEVICE_INFO_FILE_TYPE
        58, 59, // SPORT_STRENGTH, SPORT_CARDIO
        71, // CURRENT_TIME_REQUEST_SUPPORT
        76, // MULTI_LINK_SERVICE
    };
    Bytes out(15, 0);
    for (unsigned b : bits)
        out[b / 8] |= static_cast<uint8_t>(1u << (b % 8));
    return out;
}

bool parseConfiguration(const Bytes &payload, Bytes &capabilities)
{
    Reader r(payload);
    const size_t n = r.u8();
    capabilities = r.raw(n);
    return r.ok();
}

Bytes buildConfiguration(const Bytes &capabilities)
{
    Writer w;
    w.u8(static_cast<uint32_t>(capabilities.size())).raw(capabilities);
    return buildFrame(MsgConfiguration, w.bytes());
}

bool hasCapability(const Bytes &capabilities, unsigned bit)
{
    const size_t byte = bit / 8;
    return byte < capabilities.size() && (capabilities[byte] & (1u << (bit % 8))) != 0;
}

Bytes buildCurrentTimeReply(uint32_t referenceId, uint32_t garminNow, int32_t utcOffsetSeconds, uint32_t nextDstEnd,
                            uint32_t nextDstStart)
{
    Writer w;
    w.u32(referenceId).u32(garminNow).u32(static_cast<uint32_t>(utcOffsetSeconds)).u32(nextDstEnd).u32(nextDstStart);
    return buildResponse(MsgCurrentTimeRequest, StatusAck, w.bytes());
}

Bytes buildAuthNegotiationReply(const Bytes &payload)
{
    // Status "ok" (0), then the unknown byte and the flags as they were asked.
    Writer w;
    w.u8(0);
    Reader r(payload);
    const uint32_t unknown = r.u8();
    const uint32_t flags = r.u32();
    w.u8(r.ok() ? unknown : 0).u32(r.ok() ? flags : 0);
    return buildResponse(MsgAuthNegotiation, StatusAck, w.bytes());
}

Bytes buildSystemEvent(SystemEvent event, uint8_t value)
{
    Writer w;
    w.u8(event).u8(value);
    return buildFrame(MsgSystemEvent, w.bytes());
}

Bytes buildDeviceSettings(bool autoUpload, bool weatherConditions, bool weatherAlerts)
{
    // Setting ids 6, 7 and 8; a boolean is a length of 1 and then 0 or 1.
    Writer w;
    w.u8(3);
    w.u8(6).u8(1).u8(autoUpload ? 1 : 0);
    w.u8(7).u8(1).u8(weatherConditions ? 1 : 0);
    w.u8(8).u8(1).u8(weatherAlerts ? 1 : 0);
    return buildFrame(MsgDeviceSettings, w.bytes());
}

Bytes buildSupportedFileTypesRequest()
{
    return buildFrame(MsgSupportedFileTypes);
}

bool parseSupportedFileTypes(const Bytes &extra, std::vector<SupportedFileType> &out)
{
    Reader r(extra);
    const size_t count = r.u8();
    out.clear();
    for (size_t i = 0; i < count && r.ok(); ++i) {
        SupportedFileType t;
        t.dataType = static_cast<uint8_t>(r.u8());
        t.subType = static_cast<uint8_t>(r.u8());
        t.name = r.str();
        if (r.ok())
            out.push_back(t);
    }
    return r.ok();
}

static bool readEntry(Reader &r, DirectoryEntry &e)
{
    e.index = static_cast<uint16_t>(r.u16());
    e.dataType = static_cast<uint8_t>(r.u8());
    e.subType = static_cast<uint8_t>(r.u8());
    e.number = static_cast<uint16_t>(r.u16());
    e.specificFlags = static_cast<uint8_t>(r.u8());
    e.fileFlags = static_cast<uint8_t>(r.u8());
    e.size = r.u32();
    const uint32_t stamp = r.u32();
    // 0 is the Edge's "no date", not 31 Dec 1989.
    e.timestamp = stamp == 0 ? 0 : garminToUnix(stamp);
    return r.ok();
}

bool parseDirectory(const Bytes &file, std::vector<DirectoryEntry> &out)
{
    out.clear();
    if (file.size() % 16 != 0)
        return false;
    Reader r(file);
    while (r.remaining() >= 16) {
        DirectoryEntry e;
        if (!readEntry(r, e))
            return false;
        out.push_back(e);
    }
    return true;
}

bool parseFileAvailable(const Bytes &payload, DirectoryEntry &out)
{
    Reader r(payload);
    return readEntry(r, out); // a trailing flag byte, 0 or 1, is ignored
}

Bytes buildFilter(FilterType type)
{
    Writer w;
    w.u8(type);
    return buildFrame(MsgFilter, w.bytes());
}

Bytes buildDownloadRequest(uint16_t fileIndex, uint32_t dataOffset, uint32_t dataSize, uint16_t crcSeed,
                           RequestType type)
{
    Writer w;
    w.u16(fileIndex).u32(dataOffset).u8(type).u16(crcSeed).u32(dataSize).u8(0);
    return buildFrame(MsgDownloadRequest, w.bytes());
}

bool parseResponse(const Bytes &payload, ResponseView &out)
{
    Reader r(payload);
    out.originalType = static_cast<uint16_t>(r.u16());
    const uint32_t status = r.u8();
    if (!r.ok())
        return false;
    out.status = status <= StatusLengthError ? static_cast<Status>(status) : StatusNak;
    out.extra = r.rest();
    return true;
}

bool parseDownloadResponse(const ResponseView &response, DownloadResponse &out)
{
    if (response.status != StatusAck)
        return false;
    Reader r(response.extra);
    const uint32_t status = r.u8();
    out.fileSize = r.u32();
    if (!r.ok())
        return false;
    out.download = status <= DownloadCrcIncorrect ? static_cast<DownloadStatus>(status) : DownloadInvalid;
    return true;
}

bool parseFileTransferData(const Bytes &payload, FileChunk &out)
{
    Reader r(payload);
    out.flags = static_cast<uint8_t>(r.u8());
    out.crc = static_cast<uint16_t>(r.u16());
    out.offset = r.u32();
    out.data = r.rest();
    return r.ok();
}

Bytes buildFileTransferReply(Status status, TransferStatus transfer, uint32_t nextOffset)
{
    Writer w;
    w.u8(transfer).u32(nextOffset);
    return buildResponse(MsgFileTransferData, status, w.bytes());
}

Bytes buildSetFileFlags(uint16_t fileIndex, uint8_t flags)
{
    Writer w;
    w.u16(fileIndex).u8(flags);
    return buildFrame(MsgSetFileFlag, w.bytes());
}

bool parseSynchronization(const Bytes &payload, Synchronization &out)
{
    Reader r(payload);
    out.type = static_cast<uint8_t>(r.u8());
    const size_t width = r.u8();
    if (width == 8)
        out.fileTypeMask = r.u64();
    else if (width == 4)
        out.fileTypeMask = r.u32();
    else
        return false;
    return r.ok();
}

} // namespace Gfdi
