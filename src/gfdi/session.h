#pragma once

#include "gfdi/messages.h"

#include <functional>
#include <string>
#include <vector>

namespace Gfdi {

struct SessionOptions
{
    HostInfo host;

    // Local offset from UTC in seconds, for the Edge's CURRENT_TIME_REQUEST.
    int32_t utcOffsetSeconds = 0;
    int64_t unixNow = 0;

    // Ask the Edge to list only files not yet archived, instead of everything.
    // Off by default: whether a file is archived is also changed by the Garmin
    // app, so "new" can hide files this app has never seen.
    bool filterOnlyNew = false;

    // Set the archive flag on each file once it is safely stored here. This
    // changes what the Edge itself shows as synced, so it is opt-in.
    bool archiveAfterDownload = false;

    // Answer CONFIGURATION with a short capability list instead of Gadgetbridge's.
    bool minimalCapabilities = false;

    // Tell the Edge the phone's clock changed, which makes it ask for the time.
    bool syncClock = false;

    // How long one step may go without a sign of life before the sync fails.
    int stepTimeoutMs = 30000;
    // How long to wait for the Edge to start the sync itself after SYNC_READY
    // before this side asks for the directory.
    int selfStartDelayMs = 2500;
};

struct Progress
{
    enum Phase { Handshake, Directory, Files, Finishing };
    Phase phase = Handshake;
    int fileNumber = 0;  // 1-based, of fileCount
    int fileCount = 0;
    uint32_t fileBytes = 0;
    uint32_t fileSize = 0;
};

struct SessionEvents
{
    // Send one GFDI frame to the Edge.
    std::function<void(const Bytes &)> send;

    // Whether to download this directory entry. Called once per entry, after
    // the directory arrives.
    std::function<bool(const DirectoryEntry &)> wantFile;

    // A file arrived whole and its CRC checked. True if it was stored.
    std::function<bool(const DirectoryEntry &, const Bytes &)> fileReceived;

    std::function<void(const DeviceInfo &)> deviceInfo;
    std::function<void(const std::vector<DirectoryEntry> &)> directory;
    std::function<void(const Progress &)> progress;
    std::function<void(const std::string &)> log;

    // Exactly once. `ok` false carries a message a person can read.
    std::function<void(bool ok, const std::string &message)> finished;
};

// One synchronisation with an Edge, as a state machine with no I/O: it is fed
// the GFDI frames that arrive and the passage of time, and it answers through
// SessionEvents. Everything that can go wrong with the protocol is testable
// against a scripted Edge (tests/test_session.cpp) without a phone.
//
// The conversation, as Gadgetbridge has it for Edge devices:
//   Edge -> DEVICE_INFORMATION          we acknowledge, and answer with who we are
//   Edge -> CONFIGURATION (capabilities) we acknowledge, and answer with ours
//   we  -> SUPPORTED_FILE_TYPES, DEVICE_SETTINGS, SYNC_READY
//   Edge -> SYNCHRONIZATION             (or, if it stays quiet, we just ask)
//   we  -> [FILTER] and DOWNLOAD_REQUEST for file index 0, the directory
//   Edge -> response with the size, then FILE_TRANSFER_DATA chunks; we acknowledge each
//   we  -> DOWNLOAD_REQUEST for every wanted file in turn, the same way
//   we  -> SYSTEM_EVENT SYNC_COMPLETE
class Session
{
public:
    Session(SessionOptions options, SessionEvents events);

    // The transport is ready: the Edge is expected to open with DEVICE_INFORMATION.
    void start(int64_t nowMs);

    void onFrame(const Bytes &frame, int64_t nowMs);

    // Call regularly (a few times a second). Drives the timeouts and the
    // retry of a download the Edge was not ready for.
    void tick(int64_t nowMs);

    // Gives up. `finished` is called with the reason unless it already was.
    void abort(const std::string &reason);

    bool isFinished() const { return m_state == State::Finished; }
    const DeviceInfo &deviceInfo() const { return m_info; }
    const Bytes &capabilities() const { return m_caps; }

private:
    enum class State {
        Idle,
        Handshake,    // waiting for DEVICE_INFORMATION and CONFIGURATION
        Initialised,  // our side of the handshake is out; waiting to start
        Filtering,    // FILTER sent, waiting for its answer
        Directory,    // the directory file is being downloaded
        Files,        // activity files are being downloaded
        Finished,
    };

    struct Transfer
    {
        DirectoryEntry entry;
        bool isDirectory = false;
        bool sizeKnown = false;
        uint32_t size = 0;
        uint16_t crc = 0;
        Bytes data;
        int resends = 0;
    };

    void handleDeviceInformation(const Frame &f);
    void handleConfiguration(const Frame &f);
    void handleResponse(const Frame &f, int64_t now);
    void handleFileTransferData(const Frame &f, int64_t now);
    void handleSynchronization(const Frame &f, int64_t now);

    void ack(uint16_t originalType, Status status = StatusAck);
    void sendRaw(const Bytes &frame);
    void log(const std::string &s) const;

    void beginSync(int64_t now);
    void requestDirectory(int64_t now);
    void startTransfer(const DirectoryEntry &entry, bool isDirectory, int64_t now);
    void sendDownloadRequest(int64_t now);
    void completeTransfer(int64_t now);
    void nextFile(int64_t now);
    void fail(const std::string &message);
    void finishOk();
    void reportProgress(Progress::Phase phase);

    SessionOptions m_opt;
    SessionEvents m_ev;
    State m_state = State::Idle;
    int64_t m_stateSince = 0;   // last sign of life, for the timeout
    int64_t m_startedAt = 0;
    int64_t m_selfStartAt = 0;  // 0: not scheduled
    int64_t m_retryAt = 0;      // 0: no download request waiting to be repeated
    int m_notReadyRetries = 0;

    DeviceInfo m_info;
    Bytes m_caps;
    bool m_gotInfo = false;
    bool m_gotConfig = false;
    bool m_syncStarted = false;
    bool m_edgeInitiated = false;       // the sync began with the Edge's SYNCHRONIZATION
    bool m_syncRequestedEarly = false;  // ... which came before we finished the handshake

    Transfer m_xfer;
    bool m_xferActive = false;
    std::vector<DirectoryEntry> m_queue;
    size_t m_queuePos = 0;
    int m_stored = 0;
};

} // namespace Gfdi
