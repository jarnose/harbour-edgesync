#include "garminconnect.h"

#include <QDateTime>
#include <QFileInfo>
#include <QTimer>

#include <algorithm>
#include <cstdlib>

namespace {

const QString kTokensSecret = QStringLiteral("garmin_tokens");
constexpr int kPageSize = 50;
constexpr int kMaxPages = 40; // 2 000 activities: far past anything a range asks for
constexpr int kPauseListMs = 300;
constexpr int kPauseDownloadMs = 500;
constexpr int kPauseUploadMs = 1000;

} // namespace

GarminConnect::GarminConnect(ActivityStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_auth(m_http.transport(), []() { return static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch() / 1000); })
    , m_api(m_http.transport(), m_auth, []() { return static_cast<int64_t>(QDateTime::currentMSecsSinceEpoch() / 1000); })
{
    connect(&m_http, &QtHttp::trace, this, [this](const QString &line) { log(line); });
    // What Garmin answers an upload with goes in the log: it is the one answer this code could
    // only guess at, and it holds no secret.
    m_api.setDebug([this](const std::string &line) { log(QString::fromStdString(line)); });
    // A refresh made while a request was under way gives new tokens: keep them.
    m_api.setTokensChanged([this](const Garmin::Tokens &t) {
        m_tokens = t;
        saveTokens(t);
        emit stateChanged();
    });
}

void GarminConnect::start()
{
    m_email = m_store->setting(QStringLiteral("garmin_email"));
    m_rangeDays = m_store->setting(QStringLiteral("garmin_range_days"), QStringLiteral("30")).toInt();
    m_lastSyncMs = m_store->setting(QStringLiteral("garmin_last_sync")).toLongLong();

    m_vault.ensureCollection([this](bool ok, const QString &error) {
        if (!ok) {
            log(QStringLiteral("the secrets vault is not available: ") + error);
            return;
        }
        m_vaultReady = true;
        m_vault.loadSecret(kTokensSecret, [this](bool found, const QByteArray &data, const QString &) {
            Garmin::Tokens t;
            if (found && Garmin::Tokens::fromJson(data.toStdString(), t)) {
                m_tokens = t;
                m_api.setTokens(t);
                log(QStringLiteral("saved sign-in loaded"));
            }
            emit stateChanged();
        });
    });
    emit stateChanged();
}

// ---- state ------------------------------------------------------------------

void GarminConnect::setStatus(const QString &status, double progress)
{
    m_status = status;
    m_progress = progress;
    emit stateChanged();
}

void GarminConnect::setError(const QString &error)
{
    m_lastError = error;
    if (!error.isEmpty())
        log(QStringLiteral("error: ") + error);
    emit stateChanged();
}

void GarminConnect::clearError()
{
    if (m_lastError.isEmpty())
        return;
    m_lastError.clear();
    emit stateChanged();
}

void GarminConnect::setRangeDays(int days)
{
    if (days < 0 || days == m_rangeDays)
        return;
    m_rangeDays = days;
    m_store->setSetting(QStringLiteral("garmin_range_days"), QString::number(days));
    emit stateChanged();
}

int GarminConnect::unsentCount() const
{
    return m_store->notOnGarmin().size();
}

qint64 GarminConnect::cutoff() const
{
    return m_rangeDays > 0 ? QDateTime::currentMSecsSinceEpoch() / 1000 - static_cast<qint64>(m_rangeDays) * 86400 : 0;
}

bool GarminConnect::begin(const QString &status)
{
    if (m_busy) {
        setError(tr("Another operation is still running."));
        return false;
    }
    if (!signedIn()) {
        setError(tr("Sign in to Garmin Connect first."));
        return false;
    }
    m_busy = true;
    ++m_generation;
    m_lastError.clear();
    setStatus(status);
    return true;
}

void GarminConnect::end(const QString &status)
{
    m_busy = false;
    setStatus(status);
}

void GarminConnect::fail(const QString &message)
{
    m_busy = false;
    m_status.clear();
    m_progress = -1;
    setError(message);
}

void GarminConnect::later(int ms, quint64 generation, const std::function<void()> &step)
{
    QTimer::singleShot(ms, this, [this, generation, step]() {
        if (current(generation))
            step();
    });
}

void GarminConnect::cancel()
{
    ++m_generation; // every step still waiting for an answer is now stale
    const bool wasBusy = m_busy || m_needsMfa;
    m_busy = false;
    m_needsMfa = false;
    if (wasBusy)
        setStatus(tr("Cancelled."));
    else
        emit stateChanged();
}

// ---- sign-in ----------------------------------------------------------------

void GarminConnect::saveTokens(const Garmin::Tokens &tokens)
{
    m_vault.storeSecret(kTokensSecret, QByteArray::fromStdString(tokens.toJson()), [this](bool ok, const QString &error) {
        if (!ok)
            log(QStringLiteral("could not save the sign-in in the vault: ") + error);
    });
}

void GarminConnect::signIn(const QString &email, const QString &password)
{
    if (m_busy) {
        setError(tr("Another operation is still running."));
        return;
    }
    const QString address = email.trimmed();
    if (address.isEmpty() || password.isEmpty()) {
        setError(tr("Give the email address and the password of your Garmin account."));
        return;
    }
    m_busy = true;
    m_needsMfa = false;
    ++m_generation;
    const quint64 generation = m_generation;
    m_lastError.clear();
    m_email = address;
    m_store->setSetting(QStringLiteral("garmin_email"), address);
    setStatus(tr("Signing in to Garmin Connect…"));
    log(QStringLiteral("signing in"));

    // The password goes to Garmin and nowhere else: not saved, not logged.
    m_auth.login(address.toStdString(), password.toStdString(), [this, generation](const Garmin::AuthResult &r) {
        if (current(generation))
            onAuthResult(r);
    });
}

void GarminConnect::submitMfa(const QString &code)
{
    if (!m_needsMfa || m_busy)
        return;
    m_busy = true;
    ++m_generation;
    const quint64 generation = m_generation;
    m_lastError.clear();
    setStatus(tr("Checking the code…"));
    m_auth.submitMfa(code.trimmed().toStdString(), [this, generation](const Garmin::AuthResult &r) {
        if (current(generation))
            onAuthResult(r);
    });
}

void GarminConnect::onAuthResult(const Garmin::AuthResult &r)
{
    m_busy = false;
    const QString message = QString::fromStdString(r.message);
    switch (r.status) {
    case Garmin::AuthStatus::Ok:
        m_needsMfa = false;
        m_tokens = r.tokens;
        m_api.setTokens(r.tokens);
        saveTokens(r.tokens);
        log(QStringLiteral("signed in"));
        setStatus(tr("Signed in to Garmin Connect"));
        return;
    case Garmin::AuthStatus::NeedMfa:
        m_needsMfa = true;
        setStatus(tr("Garmin sent a verification code (%1). Enter it below.").arg(QString::fromStdString(r.mfaMethod)));
        return;
    case Garmin::AuthStatus::BadMfaCode:
        // The sign-in is still open: another code may be tried.
        m_status.clear();
        setError(message);
        return;
    default:
        m_needsMfa = false;
        m_status.clear();
        m_progress = -1;
        setError(message);
        return;
    }
}

void GarminConnect::signOut()
{
    cancel();
    m_tokens = Garmin::Tokens();
    m_api.setTokens(m_tokens);
    m_vault.deleteSecret(kTokensSecret, [](bool, const QString &) {});
    log(QStringLiteral("signed out"));
    setStatus(QString());
}

void GarminConnect::handleError(const Garmin::ApiError &error)
{
    log(QStringLiteral("api error %1 (HTTP %2): %3")
                .arg(static_cast<int>(error.status)).arg(error.http).arg(QString::fromStdString(error.message)));
    if (error.status == Garmin::ApiStatus::SignedOut) {
        m_tokens = Garmin::Tokens();
        m_api.setTokens(m_tokens);
        m_vault.deleteSecret(kTokensSecret, [](bool, const QString &) {});
    }
    fail(QString::fromStdString(error.message));
}

// ---- listing and matching ---------------------------------------------------

void GarminConnect::listAndMatch(quint64 generation,
                                 const std::function<void(std::vector<Garmin::RemoteActivity>)> &then)
{
    listPage(generation, 0, std::make_shared<std::vector<Garmin::RemoteActivity>>(), then);
}

void GarminConnect::listPage(quint64 generation, int start, std::shared_ptr<std::vector<Garmin::RemoteActivity>> all,
                             const std::function<void(std::vector<Garmin::RemoteActivity>)> &then)
{
    setStatus(tr("Reading the activity list from Garmin Connect…"));
    m_api.listActivities(start, kPageSize, [this, generation, start, all, then](const Garmin::ApiError &error,
                                                                              const std::vector<Garmin::RemoteActivity> &page) {
        if (!current(generation))
            return;
        if (!error.ok()) {
            handleError(error);
            return;
        }
        all->insert(all->end(), page.begin(), page.end());

        int64_t oldest = INT64_MAX;
        for (const Garmin::RemoteActivity &a : page)
            oldest = std::min(oldest, a.start);
        const bool full = static_cast<int>(page.size()) >= kPageSize;
        const bool withinRange = cutoff() == 0 || oldest >= cutoff();
        const bool more = full && withinRange && start / kPageSize + 1 < kMaxPages;
        if (more) {
            later(kPauseListMs, generation, [this, generation, start, all, then]() {
                listPage(generation, start + kPageSize, all, then);
            });
            return;
        }

        // The whole range is in. Match it against what is here: a ride Garmin has and
        // this phone has too is linked (so it is not sent again), the rest is missing.
        std::vector<Garmin::RemoteActivity> missing;
        int linked = 0;
        for (const Garmin::RemoteActivity &a : *all) {
            if (cutoff() != 0 && a.start < cutoff())
                continue;
            const qint64 id = m_store->idForStart(a.start, 2);
            if (id > 0) {
                ActivityStore::Row row;
                // 0: not known to be on Garmin; -1: on Garmin, its id not known. Either way the
                // list has the id now.
                if (m_store->row(id, row) && row.garminId <= 0) {
                    m_store->setGarminId(id, a.id);
                    ++linked;
                }
            } else {
                missing.push_back(a);
            }
        }
        log(QStringLiteral("garmin lists %1 activities in range, %2 linked to rides here, %3 not here")
                    .arg(all->size()).arg(linked).arg(missing.size()));
        if (linked > 0)
            emit activitiesChanged();
        then(missing);
    });
}

// ---- download ---------------------------------------------------------------

void GarminConnect::downloadNew()
{
    if (!begin(tr("Reading the activity list from Garmin Connect…")))
        return;
    const quint64 generation = m_generation;
    listAndMatch(generation, [this, generation](std::vector<Garmin::RemoteActivity> missing) {
        if (!current(generation))
            return;
        if (missing.empty()) {
            m_lastSyncMs = QDateTime::currentMSecsSinceEpoch();
            m_store->setSetting(QStringLiteral("garmin_last_sync"), QString::number(m_lastSyncMs));
            end(tr("Nothing new on Garmin Connect"));
            return;
        }
        std::sort(missing.begin(), missing.end(),
                  [](const Garmin::RemoteActivity &a, const Garmin::RemoteActivity &b) { return a.start < b.start; });
        downloadStep(generation, std::make_shared<std::vector<Garmin::RemoteActivity>>(missing), 0, 0);
    });
}

void GarminConnect::downloadStep(quint64 generation, std::shared_ptr<std::vector<Garmin::RemoteActivity>> todo,
                                 size_t index, int stored)
{
    if (index >= todo->size()) {
        m_lastSyncMs = QDateTime::currentMSecsSinceEpoch();
        m_store->setSetting(QStringLiteral("garmin_last_sync"), QString::number(m_lastSyncMs));
        end(tr("%n new activities from Garmin Connect", "", stored));
        emit activitiesChanged();
        return;
    }
    const Garmin::RemoteActivity remote = (*todo)[index];
    setStatus(tr("Downloading activity %1 of %2…").arg(index + 1).arg(todo->size()),
              static_cast<double>(index) / static_cast<double>(todo->size()));

    m_api.downloadFit(remote.id, [this, generation, todo, index, stored, remote](const Garmin::ApiError &error,
                                                                               const std::string &fit) {
        if (!current(generation))
            return;
        int newStored = stored;
        if (!error.ok()) {
            // One ride with no original file (a manual entry, say) does not stop the rest.
            if (error.status == Garmin::ApiStatus::NotFound || error.status == Garmin::ApiStatus::Protocol) {
                log(QStringLiteral("skipping activity %1: %2").arg(remote.id).arg(QString::fromStdString(error.message)));
            } else {
                handleError(error);
                return;
            }
        } else {
            Gfdi::DirectoryEntry entry;
            entry.dataType = 128;
            entry.subType = 4;
            entry.timestamp = remote.start;
            entry.size = static_cast<uint32_t>(fit.size());
            QString storeError;
            const qint64 id = m_store->add(QStringLiteral("garmin"), entry, QByteArray(fit.data(), static_cast<int>(fit.size())),
                                           &storeError, QStringLiteral("garmin"), remote.id);
            if (id > 0) {
                ++newStored;
                log(QStringLiteral("stored garmin activity %1 as %2 (%3 bytes)").arg(remote.id).arg(id).arg(fit.size()));
                emit activitiesChanged();
            } else {
                log(QStringLiteral("could not store activity %1: %2").arg(remote.id).arg(storeError));
            }
        }
        later(kPauseDownloadMs, generation, [this, generation, todo, index, newStored]() {
            downloadStep(generation, todo, index + 1, newStored);
        });
    });
}

// ---- upload -----------------------------------------------------------------

void GarminConnect::uploadUnsent()
{
    if (!begin(tr("Checking what Garmin Connect has…")))
        return;
    m_acceptedRows.clear();
    const quint64 generation = m_generation;
    listAndMatch(generation, [this, generation](std::vector<Garmin::RemoteActivity>) {
        if (!current(generation))
            return;
        auto todo = std::make_shared<QList<ActivityStore::Row>>(m_store->notOnGarmin());
        if (todo->isEmpty()) {
            end(tr("Everything here is already on Garmin Connect"));
            return;
        }
        uploadStep(generation, todo, 0, 0, 0, 0);
    });
}

void GarminConnect::uploadStep(quint64 generation, std::shared_ptr<QList<ActivityStore::Row>> todo, size_t index,
                               int sent, int already, int refused)
{
    if (static_cast<int>(index) >= todo->size()) {
        finishUploads(generation, sent, already, refused);
        return;
    }
    const ActivityStore::Row row = todo->at(static_cast<int>(index));
    setStatus(tr("Sending activity %1 of %2…").arg(index + 1).arg(todo->size()),
              static_cast<double>(index) / static_cast<double>(todo->size()));

    const QByteArray bytes = m_store->readFit(row);
    if (bytes.isEmpty()) {
        log(QStringLiteral("activity %1 has no file, skipped").arg(row.id));
        uploadStep(generation, todo, index + 1, sent, already, refused);
        return;
    }
    const std::string fit(bytes.constData(), static_cast<size_t>(bytes.size()));
    const std::string name = QFileInfo(row.fitPath).fileName().toStdString();
    // A file from another device goes up as an import, so it is not passed on to services
    // linked to the account: the device that recorded it has done that already.
    const bool asImport = row.origin == QLatin1String("import");

    m_api.upload(fit, name, asImport, [this, generation, todo, index, sent, already, refused, row](
                                              const Garmin::ApiError &error, const Garmin::UploadResult &result) {
        if (!current(generation))
            return;
        int s = sent, a = already, r = refused;
        if (!error.ok()) {
            if (error.status == Garmin::ApiStatus::Protocol) {
                // Garmin took the request and refused the file: note it and go on.
                ++r;
                log(QStringLiteral("garmin refused activity %1: %2").arg(row.id).arg(QString::fromStdString(error.message)));
            } else {
                handleError(error);
                return;
            }
        } else {
            if (result.accepted) {
                // Not linked yet: that waits for the list to show it.
                m_acceptedRows.append(row);
            } else {
                m_store->setGarminId(row.id, result.activityId > 0 ? result.activityId : -1);
                if (result.uploaded)
                    ++s;
                else
                    ++a;
            }
            log(QStringLiteral("activity %1: %2").arg(row.id).arg(QString::fromStdString(result.message)));
            emit activitiesChanged();
        }
        later(kPauseUploadMs, generation, [this, generation, todo, index, s, a, r]() {
            uploadStep(generation, todo, index + 1, s, a, r);
        });
    });
}

struct GarminConnect::Confirm
{
    QList<ActivityStore::Row> pending;
    std::vector<bool> found;
    int attempt = 0;
};

void GarminConnect::finishUploads(quint64 generation, int sent, int already, int refused)
{
    auto summarise = [this, sent, already, refused](int confirmed, int unlisted) {
        m_lastSyncMs = QDateTime::currentMSecsSinceEpoch();
        m_store->setSetting(QStringLiteral("garmin_last_sync"), QString::number(m_lastSyncMs));
        QString summary = tr("%n activities sent to Garmin Connect", "", sent + confirmed);
        if (already > 0)
            summary += QStringLiteral(" · ") + tr("%n already there", "", already);
        if (refused > 0)
            summary += QStringLiteral(" · ") + tr("%n refused", "", refused);
        if (unlisted > 0)
            summary += QStringLiteral(" · ") + tr("%n not listed yet", "", unlisted);
        end(summary);
        emit activitiesChanged();
    };
    if (m_acceptedRows.isEmpty()) {
        summarise(0, 0);
        return;
    }
    const int total = m_acceptedRows.size();
    setStatus(tr("Waiting for Garmin Connect to process %n files…", "", total));
    auto state = std::make_shared<Confirm>();
    state->pending = m_acceptedRows;
    state->found.assign(static_cast<size_t>(total), false);
    m_acceptedRows.clear();
    confirmAccepted(generation, state, [this, summarise, total](int confirmed) { summarise(confirmed, total - confirmed); });
}

void GarminConnect::confirmAccepted(quint64 generation, std::shared_ptr<Confirm> state,
                                    const std::function<void(int)> &then)
{
    // Garmin takes a few seconds to process a file; look, and look again twice.
    later(6000, generation, [this, generation, state, then]() { confirmNext(generation, state, 0, then); });
}

void GarminConnect::confirmNext(quint64 generation, std::shared_ptr<Confirm> state, int index,
                                const std::function<void(int)> &then)
{
    if (index >= state->pending.size()) {
        int confirmed = 0;
        for (bool f : state->found)
            confirmed += f ? 1 : 0;
        if (confirmed < state->pending.size() && ++state->attempt < 3) {
            confirmAccepted(generation, state, then);
            return;
        }
        then(confirmed);
        return;
    }
    if (state->found[static_cast<size_t>(index)]) {
        confirmNext(generation, state, index + 1, then);
        return;
    }
    const ActivityStore::Row row = state->pending.at(index);
    // Garmin's day is not necessarily this phone's: ask for the days around the ride.
    m_api.listActivities(0, 20, [this, generation, state, index, then, row](const Garmin::ApiError &error,
                                                                           const std::vector<Garmin::RemoteActivity> &list) {
        if (!current(generation))
            return;
        if (!error.ok()) {
            handleError(error);
            return;
        }
        for (const Garmin::RemoteActivity &a : list) {
            if (std::llabs(a.start - row.startTime) <= 2) {
                m_store->setGarminId(row.id, a.id);
                state->found[static_cast<size_t>(index)] = true;
                log(QStringLiteral("activity %1 is on Garmin Connect as %2").arg(row.id).arg(a.id));
                break;
            }
        }
        later(kPauseListMs, generation, [this, generation, state, index, then]() { confirmNext(generation, state, index + 1, then); });
    }, Garmin::garminDate(row.startTime - 86400), Garmin::garminDate(row.startTime + 86400));
}

void GarminConnect::uploadActivity(qint64 id)
{
    ActivityStore::Row row;
    if (!m_store->row(id, row))
        return;
    if (!begin(tr("Sending to Garmin Connect…")))
        return;
    const quint64 generation = m_generation;
    const QByteArray bytes = m_store->readFit(row);
    if (bytes.isEmpty()) {
        fail(tr("The activity's file is missing."));
        return;
    }
    const std::string fit(bytes.constData(), static_cast<size_t>(bytes.size()));
    const std::string name = QFileInfo(row.fitPath).fileName().toStdString();
    const bool asImport = row.origin == QLatin1String("import");
    m_acceptedRows.clear();
    m_api.upload(fit, name, asImport, [this, generation, id, row](const Garmin::ApiError &error, const Garmin::UploadResult &result) {
        if (!current(generation))
            return;
        if (!error.ok()) {
            handleError(error);
            return;
        }
        if (result.accepted) {
            // Look for it in the list before saying it is there.
            setStatus(tr("Waiting for Garmin Connect to process the file…"));
            auto state = std::make_shared<Confirm>();
            state->pending.append(row);
            state->found.assign(1, false);
            confirmAccepted(generation, state, [this](int confirmed) {
                emit activitiesChanged();
                end(confirmed > 0 ? tr("Sent to Garmin Connect")
                                  : tr("Garmin Connect accepted the file but does not list it yet. Check again in a moment."));
            });
            return;
        }
        m_store->setGarminId(id, result.activityId > 0 ? result.activityId : -1);
        emit activitiesChanged();
        end(result.uploaded ? tr("Sent to Garmin Connect") : tr("Garmin Connect already has this activity"));
    });
}
