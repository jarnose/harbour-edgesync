#include "gfdi/session.h"

#include "gfdi/crc.h"

#include <algorithm>

namespace Gfdi {

namespace {

constexpr int kNotReadyRetries = 10;
constexpr int kNotReadyDelayMs = 1000;
constexpr int kMaxResends = 5;
constexpr int kHandshakeTimeoutMs = 25000;

// Bits of SYNCHRONIZATION's file-type mask that mean "there are activities".
constexpr unsigned kSyncWorkouts = 3;
constexpr unsigned kSyncActivities = 5;
constexpr unsigned kSyncActivitySummary = 21;
constexpr unsigned kSyncSleep = 26;

bool syncMeansActivities(const Synchronization &s)
{
    if (s.type == 0) // a manual sync asks for everything
        return true;
    const uint64_t any = (1ull << kSyncWorkouts) | (1ull << kSyncActivities) | (1ull << kSyncActivitySummary)
            | (1ull << kSyncSleep);
    return (s.fileTypeMask & any) != 0;
}

} // namespace

Session::Session(SessionOptions options, SessionEvents events)
    : m_opt(std::move(options)), m_ev(std::move(events))
{
}

void Session::log(const std::string &s) const
{
    if (m_ev.log)
        m_ev.log(s);
}

void Session::sendRaw(const Bytes &frame)
{
    if (m_ev.send)
        m_ev.send(frame);
}

void Session::ack(uint16_t originalType, Status status)
{
    sendRaw(buildResponse(originalType, status));
}

void Session::start(int64_t nowMs)
{
    m_state = State::Handshake;
    m_startedAt = nowMs;
    m_stateSince = nowMs;
    reportProgress(Progress::Handshake);
}

void Session::tick(int64_t now)
{
    if (m_state == State::Finished || m_state == State::Idle)
        return;

    if (m_state == State::Handshake) {
        if (now - m_startedAt > kHandshakeTimeoutMs)
            fail("The Edge did not start the conversation. Is it paired with this phone, "
                 "and not connected to another one?");
        return;
    }
    if (m_state == State::Initialised) {
        if (!m_syncStarted && m_selfStartAt != 0 && now >= m_selfStartAt)
            beginSync(now);
        return;
    }

    if (m_retryAt != 0 && now >= m_retryAt) {
        m_retryAt = 0;
        sendDownloadRequest(now);
        return;
    }
    if (m_retryAt == 0 && now - m_stateSince > m_opt.stepTimeoutMs)
        fail("The Edge stopped answering during the transfer.");
}

void Session::abort(const std::string &reason)
{
    fail(reason);
}

void Session::onFrame(const Bytes &raw, int64_t now)
{
    if (m_state == State::Finished || m_state == State::Idle)
        return;

    Frame f;
    std::string error;
    if (!parseFrame(raw, f, error)) {
        // Nothing to acknowledge: a frame that cannot be read has no type.
        log("dropped a frame: " + error);
        return;
    }
    m_stateSince = now;

    switch (f.type) {
    case MsgDeviceInformation:
        handleDeviceInformation(f);
        break;
    case MsgConfiguration:
        handleConfiguration(f);
        if (m_state == State::Initialised && m_syncRequestedEarly) {
            m_syncRequestedEarly = false;
            beginSync(now);
        }
        break;
    case MsgResponse:
        handleResponse(f, now);
        break;
    case MsgFileTransferData:
        handleFileTransferData(f, now);
        break;
    case MsgSynchronization:
        handleSynchronization(f, now);
        break;
    case MsgFileAvailable: {
        DirectoryEntry e;
        if (parseFileAvailable(f.payload, e))
            log("the Edge reports a new file, index " + std::to_string(e.index));
        ack(f.type);
        break;
    }
    case MsgCurrentTimeRequest: {
        Reader r(f.payload);
        const uint32_t ref = r.u32();
        const int64_t unixNow = m_opt.unixNow + (now - m_startedAt) / 1000;
        sendRaw(buildCurrentTimeReply(ref, unixToGarmin(unixNow), m_opt.utcOffsetSeconds));
        break;
    }
    case MsgAuthNegotiation:
        sendRaw(buildAuthNegotiationReply(f.payload));
        break;
    case MsgBatteryStatus:
        ack(f.type);
        break;
    default:
        // Weather, notifications, protobuf services, music: none are ours.
        log("message type " + std::to_string(f.type) + " answered as unsupported");
        ack(f.type, StatusUnsupported);
        break;
    }
}

void Session::handleDeviceInformation(const Frame &f)
{
    if (!parseDeviceInformation(f.payload, m_info)) {
        log("DEVICE_INFORMATION could not be read");
        ack(f.type, StatusDecodeError);
        return;
    }
    m_gotInfo = true;
    // Two answers, as Gadgetbridge sends: the plain acknowledgement, then the
    // one that carries who we are.
    ack(f.type);
    sendRaw(buildDeviceInformationReply(m_info, m_opt.host));
    if (m_ev.deviceInfo)
        m_ev.deviceInfo(m_info);
}

void Session::handleConfiguration(const Frame &f)
{
    if (!parseConfiguration(f.payload, m_caps)) {
        log("CONFIGURATION could not be read");
        ack(f.type, StatusDecodeError);
        return;
    }
    ack(f.type);
    sendRaw(buildConfiguration(m_opt.minimalCapabilities ? Gfdi::minimalCapabilities() : ourCapabilities()));

    if (m_gotConfig)
        return; // a repeat: answered, but the initialisation below runs once
    m_gotConfig = true;

    sendRaw(buildSupportedFileTypesRequest());
    sendRaw(buildDeviceSettings(true, false, false));
    if (m_opt.syncClock)
        sendRaw(buildSystemEvent(EventTimeUpdated));
    // The Edge answers SYNC_READY with a SYNCHRONIZATION of its own, if it is
    // going to; selfStartDelayMs is how long to wait before asking ourselves.
    sendRaw(buildSystemEvent(EventSyncReady));

    m_state = State::Initialised;
    m_selfStartAt = m_stateSince + m_opt.selfStartDelayMs;
}

void Session::handleSynchronization(const Frame &f, int64_t now)
{
    ack(f.type);
    Synchronization s;
    if (!parseSynchronization(f.payload, s)) {
        log("SYNCHRONIZATION could not be read");
        return;
    }
    if (!syncMeansActivities(s)) {
        log("the Edge wants to sync something other than activities; not starting");
        return;
    }
    if (m_syncStarted)
        return;
    if (m_state == State::Initialised) {
        m_edgeInitiated = true;
        beginSync(now);
    } else if (m_state == State::Handshake) {
        m_edgeInitiated = true;
        m_syncRequestedEarly = true;
    }
}

void Session::beginSync(int64_t now)
{
    if (m_syncStarted)
        return;
    m_syncStarted = true;
    m_selfStartAt = 0;

    // The device-initiated path is the one Gadgetbridge answers with a FILTER;
    // the one it starts itself asks for the directory straight away.
    if (m_opt.filterOnlyNew || m_edgeInitiated) {
        sendRaw(buildFilter(FilterOnlyNew));
        m_state = State::Filtering;
        m_stateSince = now;
        return;
    }
    requestDirectory(now);
}

void Session::requestDirectory(int64_t now)
{
    m_state = State::Directory;
    reportProgress(Progress::Directory);
    DirectoryEntry root;
    root.index = 0;
    startTransfer(root, true, now);
}

void Session::startTransfer(const DirectoryEntry &entry, bool isDirectory, int64_t now)
{
    m_xfer = Transfer();
    m_xfer.entry = entry;
    m_xfer.isDirectory = isDirectory;
    m_xferActive = true;
    m_notReadyRetries = 0;
    sendDownloadRequest(now);
}

void Session::sendDownloadRequest(int64_t now)
{
    m_stateSince = now;
    sendRaw(buildDownloadRequest(m_xfer.entry.index, 0, 0, 0, RequestNew));
}

void Session::handleResponse(const Frame &f, int64_t now)
{
    ResponseView r;
    if (!parseResponse(f.payload, r)) {
        log("a Response could not be read");
        return;
    }

    switch (r.originalType) {
    case MsgDownloadRequest: {
        if (!m_xferActive) {
            log("a download answer arrived with no download pending");
            return;
        }
        DownloadResponse d;
        if (!parseDownloadResponse(r, d)) {
            const std::string what = "the Edge refused the download (status " + std::to_string(r.status) + ")";
            if (m_xfer.isDirectory)
                fail(what);
            else {
                log(what + ", skipping file " + std::to_string(m_xfer.entry.index));
                m_xferActive = false;
                nextFile(now);
            }
            return;
        }
        if (d.download == DownloadNotReady) {
            if (++m_notReadyRetries > kNotReadyRetries) {
                fail("The Edge kept saying it was not ready to send files.");
                return;
            }
            m_retryAt = now + kNotReadyDelayMs;
            return;
        }
        if (d.download != DownloadOk) {
            const std::string what = "the Edge cannot send file " + std::to_string(m_xfer.entry.index)
                    + " (status " + std::to_string(d.download) + ")";
            if (m_xfer.isDirectory)
                fail(what);
            else {
                log(what + ", skipping it");
                m_xferActive = false;
                nextFile(now);
            }
            return;
        }
        m_retryAt = 0;
        m_xfer.sizeKnown = true;
        m_xfer.size = d.fileSize;
        m_xfer.data.reserve(d.fileSize);
        reportProgress(m_xfer.isDirectory ? Progress::Directory : Progress::Files);
        if (d.fileSize == 0)
            completeTransfer(now);
        break;
    }
    case MsgFilter:
        if (m_state == State::Filtering)
            requestDirectory(now);
        break;
    case MsgSupportedFileTypes: {
        std::vector<SupportedFileType> types;
        if (r.status == StatusAck && parseSupportedFileTypes(r.extra, types))
            log("the Edge lists " + std::to_string(types.size()) + " file types");
        break;
    }
    default:
        if (r.status != StatusAck)
            log("the Edge answered message " + std::to_string(r.originalType) + " with status "
                + std::to_string(r.status));
        break;
    }
}

void Session::handleFileTransferData(const Frame &f, int64_t now)
{
    FileChunk c;
    if (!parseFileTransferData(f.payload, c)) {
        log("a file chunk could not be read");
        return;
    }
    if (!m_xferActive || !m_xfer.sizeKnown) {
        log("a file chunk arrived with no download pending");
        return;
    }

    const uint32_t have = static_cast<uint32_t>(m_xfer.data.size());
    if (c.offset != have) {
        if (c.offset < have) {
            // A repeat of a chunk already taken: say where we are.
            sendRaw(buildFileTransferReply(StatusAck, TransferOk, have));
            return;
        }
        if (++m_xfer.resends > kMaxResends) {
            fail("File chunks kept arriving out of order.");
            return;
        }
        sendRaw(buildFileTransferReply(StatusAck, TransferResend, have));
        return;
    }

    const uint16_t crc = crc16(c.data.data(), c.data.size(), m_xfer.crc);
    if (crc != c.crc) {
        fail("A file chunk failed its checksum at byte " + std::to_string(have) + ". Try again.");
        return;
    }
    if (have + c.data.size() > m_xfer.size) {
        fail("The Edge sent more data than the file is long.");
        return;
    }

    m_xfer.crc = crc;
    m_xfer.data.insert(m_xfer.data.end(), c.data.begin(), c.data.end());
    sendRaw(buildFileTransferReply(StatusAck, TransferOk, static_cast<uint32_t>(m_xfer.data.size())));
    reportProgress(m_xfer.isDirectory ? Progress::Directory : Progress::Files);

    if (m_xfer.data.size() == m_xfer.size)
        completeTransfer(now);
}

void Session::completeTransfer(int64_t now)
{
    m_xferActive = false;

    if (m_xfer.isDirectory) {
        std::vector<DirectoryEntry> entries;
        if (!parseDirectory(m_xfer.data, entries)) {
            fail("The Edge's file list had an unexpected length.");
            return;
        }
        if (m_ev.directory)
            m_ev.directory(entries);

        m_queue.clear();
        for (const DirectoryEntry &e : entries) {
            if (m_ev.wantFile && m_ev.wantFile(e))
                m_queue.push_back(e);
        }
        // Oldest first, so an interrupted sync leaves a contiguous history.
        std::stable_sort(m_queue.begin(), m_queue.end(), [](const DirectoryEntry &a, const DirectoryEntry &b) {
            return a.timestamp < b.timestamp;
        });
        m_queuePos = 0;
        m_state = State::Files;
        log(std::to_string(entries.size()) + " files on the Edge, " + std::to_string(m_queue.size())
            + " to fetch");
        nextFile(now);
        return;
    }

    const bool stored = m_ev.fileReceived ? m_ev.fileReceived(m_xfer.entry, m_xfer.data) : false;
    if (stored) {
        ++m_stored;
        if (m_opt.archiveAfterDownload)
            sendRaw(buildSetFileFlags(m_xfer.entry.index, kFlagArchive));
    }
    nextFile(now);
}

void Session::nextFile(int64_t now)
{
    if (m_queuePos >= m_queue.size()) {
        finishOk();
        return;
    }
    startTransfer(m_queue[m_queuePos++], false, now);
    reportProgress(Progress::Files);
}

void Session::finishOk()
{
    reportProgress(Progress::Finishing);
    sendRaw(buildSystemEvent(EventSyncComplete));
    m_state = State::Finished;
    if (m_ev.finished)
        m_ev.finished(true, std::string());
}

void Session::fail(const std::string &message)
{
    if (m_state == State::Finished)
        return;
    m_state = State::Finished;
    m_xferActive = false;
    if (m_ev.finished)
        m_ev.finished(false, message);
}

void Session::reportProgress(Progress::Phase phase)
{
    if (!m_ev.progress)
        return;
    Progress p;
    p.phase = phase;
    if (m_state == State::Files || phase == Progress::Files) {
        p.fileCount = static_cast<int>(m_queue.size());
        p.fileNumber = static_cast<int>(m_queuePos);
    }
    if (m_xferActive) {
        p.fileBytes = static_cast<uint32_t>(m_xfer.data.size());
        p.fileSize = m_xfer.size;
    }
    m_ev.progress(p);
}

} // namespace Gfdi
