#pragma once

#include "gfdi/message.h"

#include <cstdint>
#include <string>
#include <vector>

// The typed messages of the file-sync conversation: how each one is laid out on
// the wire, in both directions. The layouts come from Gadgetbridge's Garmin
// support (AGPL-3.0), which reverse-engineered them; docs/protocol.md names
// the file each came from.
namespace Gfdi {

// Seconds from the Unix epoch to Garmin's (31 Dec 1989).
constexpr int64_t kGarminEpoch = 631065600;
inline int64_t garminToUnix(uint32_t garmin) { return static_cast<int64_t>(garmin) + kGarminEpoch; }
inline uint32_t unixToGarmin(int64_t unixTime) { return static_cast<uint32_t>(unixTime - kGarminEpoch); }

// ---- the handshake ----------------------------------------------------------

struct DeviceInfo
{
    uint16_t protocolVersion = 0;
    uint16_t productNumber = 0;
    uint32_t unitNumber = 0;
    uint16_t softwareVersion = 0; // 1234 is 12.34
    uint16_t maxPacketSize = 0;
    std::string bluetoothName;
    std::string deviceName;
    std::string deviceModel;
    // The Edge's Classic Bluetooth address ("AA:BB:CC:DD:EE:FF"), when it says
    // one. Some models serve their files only over a Classic RFCOMM link, which
    // is opened to this address; it is only logged here.
    std::string classicAddress;
};

bool parseDeviceInformation(const Bytes &payload, DeviceInfo &out);

struct HostInfo
{
    std::string bluetoothName = "Edge Sync";
    std::string manufacturer = "Sailfish";
    std::string model = "Edge Sync";
};

// The Response that tells the Edge who we are ("Connected with X" on its screen).
Bytes buildDeviceInformationReply(const DeviceInfo &theirs, const HostInfo &host);

// The capability bitmask the Edge sent, and the one we answer with: the set
// Gadgetbridge declares (everything it knows minus what it does not implement),
// because that is the one set known to make Garmin firmware offer its files.
Bytes ourCapabilities();
bool parseConfiguration(const Bytes &payload, Bytes &capabilities);
Bytes buildConfiguration(const Bytes &capabilities);
bool hasCapability(const Bytes &capabilities, unsigned bit);

// Capability bit numbers that this app looks at.
enum Capability : unsigned {
    CapSync = 3,
    CapDeviceInitiatesSync = 4,
    CapHostInitiatedSyncRequests = 5,
    CapExplicitArchive = 29,
    CapMultiLinkService = 76,
};

// The answer to CURRENT_TIME_REQUEST. `utcOffsetSeconds` includes any DST in
// force; the two transition stamps are Garmin time, or 0 when unknown.
Bytes buildCurrentTimeReply(uint32_t referenceId, uint32_t garminNow, int32_t utcOffsetSeconds,
                            uint32_t nextDstEnd = 0, uint32_t nextDstStart = 0);

// AUTH_NEGOTIATION is acknowledged by echoing what was asked, with status "ok".
Bytes buildAuthNegotiationReply(const Bytes &payload);

// ---- system events ----------------------------------------------------------

enum SystemEvent : uint8_t {
    EventSyncComplete = 0,
    EventSyncFail = 1,
    EventPairStart = 3,
    EventPairComplete = 4,
    EventHostForeground = 6,
    EventHostBackground = 7,
    EventSyncReady = 8,
    EventTimeUpdated = 16,
};

Bytes buildSystemEvent(SystemEvent event, uint8_t value = 0);

// DEVICE_SETTINGS with the three booleans Gadgetbridge sends right after the
// handshake: auto-upload on, weather conditions and alerts as asked.
Bytes buildDeviceSettings(bool autoUpload, bool weatherConditions, bool weatherAlerts);
Bytes buildSupportedFileTypesRequest();

struct SupportedFileType
{
    uint8_t dataType = 0;
    uint8_t subType = 0;
    std::string name;
};
// The Response payload (after the original type) to SUPPORTED_FILE_TYPES.
bool parseSupportedFileTypes(const Bytes &responseExtra, std::vector<SupportedFileType> &out);

// ---- the file directory -----------------------------------------------------

struct DirectoryEntry
{
    uint16_t index = 0;
    uint8_t dataType = 0; // 128 is a FIT file
    uint8_t subType = 0;  // with dataType 128: 4 is an activity
    uint16_t number = 0;
    uint8_t specificFlags = 0;
    uint8_t fileFlags = 0;
    uint32_t size = 0;
    int64_t timestamp = 0; // Unix seconds; 0 when the Edge gave none

    bool isFit() const { return dataType == 128; }
    bool isActivity() const { return dataType == 128 && subType == 4; }
};

constexpr uint8_t kFlagArchive = 0x10;

// The directory file is a bare run of 16-byte entries. False when its length is
// not a multiple of 16.
bool parseDirectory(const Bytes &file, std::vector<DirectoryEntry> &out);

// FILE_AVAILABLE announces one entry the Edge has just closed.
bool parseFileAvailable(const Bytes &payload, DirectoryEntry &out);

// ---- file transfer ----------------------------------------------------------

enum FilterType : uint8_t { FilterAll = 0, FilterOnlyNew = 3 };
Bytes buildFilter(FilterType type);

enum RequestType : uint8_t { RequestContinue = 0, RequestNew = 1 };
Bytes buildDownloadRequest(uint16_t fileIndex, uint32_t dataOffset, uint32_t dataSize, uint16_t crcSeed,
                           RequestType type);

enum DownloadStatus : uint8_t {
    DownloadOk = 0,
    DownloadIndexUnknown = 1,
    DownloadIndexNotReadable = 2,
    DownloadNoSpaceLeft = 3,
    DownloadInvalid = 4,
    DownloadNotReady = 5,
    DownloadCrcIncorrect = 6,
};

struct DownloadResponse
{
    DownloadStatus download = DownloadOk;
    uint32_t fileSize = 0;
};

struct FileChunk
{
    uint8_t flags = 0;
    uint16_t crc = 0; // running CRC of the file up to and including this chunk
    uint32_t offset = 0;
    Bytes data;
};
bool parseFileTransferData(const Bytes &payload, FileChunk &out);

enum TransferStatus : uint8_t {
    TransferOk = 0,
    TransferResend = 1,
    TransferAbort = 2,
    TransferCrcMismatch = 3,
    TransferOffsetMismatch = 4,
};
// The Response to a FILE_TRANSFER_DATA chunk: how far the file has come.
Bytes buildFileTransferReply(Status status, TransferStatus transfer, uint32_t nextOffset);

Bytes buildSetFileFlags(uint16_t fileIndex, uint8_t flags);

// SYNCHRONIZATION is the Edge saying it has something to sync.
struct Synchronization
{
    uint8_t type = 0; // 0 manual, 1 automatic
    uint64_t fileTypeMask = 0;
};
bool parseSynchronization(const Bytes &payload, Synchronization &out);

// A Response's header: the type it answers and its status; `extra` is the rest.
struct ResponseView
{
    uint16_t originalType = 0;
    Status status = StatusAck;
    Bytes extra;
};
bool parseResponse(const Bytes &payload, ResponseView &out);

// The body of the Response to a DOWNLOAD_REQUEST. Only an ACKed response has one.
bool parseDownloadResponse(const ResponseView &response, DownloadResponse &out);

} // namespace Gfdi
