// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon_service.h"

#include "command_contract.h"
#include "config_store.h"
#include "core/qprocess_runner.h"
#include "core/rule_conditions.h"
#include "dbus_error_contract.h"
#include "rokudeck1_adaptor.h"

#include <QDBusError>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDateTime>
#include <QEventLoop>
#include <QRandomGenerator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPointer>
#include <QRegularExpression>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace RokuDeck {
namespace {

constexpr auto serviceName = "io.github.josephur.RokuDeck1";
constexpr auto objectPath = "/io/github/josephur/RokuDeck1";

[[nodiscard]] QString powerName(PowerState power) {
    switch (power) {
    case PowerState::On:
        return QStringLiteral("on");
    case PowerState::Standby:
        return QStringLiteral("standby");
    case PowerState::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

[[nodiscard]] QString triStateName(TriState state) {
    switch (state) {
    case TriState::Active:
        return QStringLiteral("active");
    case TriState::Inactive:
        return QStringLiteral("inactive");
    case TriState::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

[[nodiscard]] QString mediaName(MediaPlaybackState state) {
    switch (state) {
    case MediaPlaybackState::Playing:
        return QStringLiteral("playing");
    case MediaPlaybackState::Paused:
        return QStringLiteral("paused");
    case MediaPlaybackState::Stopped:
        return QStringLiteral("stopped");
    case MediaPlaybackState::Idle:
        return QStringLiteral("idle");
    case MediaPlaybackState::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

[[nodiscard]] QString healthName(DeviceHealth health) {
    switch (health) {
    case DeviceHealth::Online:
        return QStringLiteral("online");
    case DeviceHealth::Standby:
        return QStringLiteral("standby");
    case DeviceHealth::Degraded:
        return QStringLiteral("degraded");
    case DeviceHealth::Offline:
        return QStringLiteral("offline");
    case DeviceHealth::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

constexpr qsizetype maximumDeviceJsonBytes = 4096;
constexpr qsizetype maximumDisplayNameLength = 128;
constexpr qsizetype maximumSerialLength = 64;

// The reserved serial of the machine the daemon runs on (spec §1): exactly one
// such device is bootstrapped at startup, undeletable and always consenting.
constexpr auto localPcSerial = "LOCAL-PC";

// wpctl get-volume prints "Volume: 0.58", optionally suffixed " [MUTED]".
// Returns the 0..100 volume (rounded) plus the mute flag; nullopt when the
// output cannot be trusted (treated as a failed poll).
[[nodiscard]] std::optional<std::pair<int, bool>> parseWpctlVolume(const QByteArray& output) {
    static const QRegularExpression pattern(
        QStringLiteral("^Volume: ([0-9]+(?:\\.[0-9]+)?)( \\[MUTED\\])?$"));
    const QString line = QString::fromUtf8(output).trimmed();
    const auto match = pattern.match(line);
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    return std::make_pair(qRound(match.captured(1).toDouble() * 100.0),
                          !match.captured(2).isEmpty());
}

// Deliberately parsed by hand rather than with QHostAddress: the daemon must
// accept exactly one notation, and QHostAddress also resolves shorthand forms
// such as "10.1" that would silently reach a different host than the user typed.
[[nodiscard]] bool isRoutableIpv4(const QString& address) {
    const QStringList octets = address.split(QChar(u'.'));
    if (octets.size() != 4) {
        return false;
    }
    quint32 raw = 0;
    for (const QString& octet : octets) {
        if (octet.isEmpty() || octet.size() > 3) {
            return false;
        }
        if (octet.size() > 1 && octet.startsWith(QChar(u'0'))) {
            return false;
        }
        for (const QChar character : octet) {
            if (character < u'0' || character > u'9') {
                return false;
            }
        }
        const uint value = octet.toUInt();
        if (value > 255) {
            return false;
        }
        raw = (raw << 8) | value;
    }
    if (raw == 0 || raw == 0xFFFFFFFFu) {
        return false;
    }
    const quint32 highOctet = raw >> 24;
    // Loopback, multicast and reserved ranges can never name a Roku on the LAN.
    return highOctet != 127 && highOctet < 224;
}

[[nodiscard]] bool isPrintableSingleLine(const QString& text) {
    return std::ranges::none_of(text, [](QChar character) {
        return character.category() == QChar::Other_Control ||
               character.category() == QChar::Other_Format;
    });
}

// The refresh policy reshapes what a widget asked for rather than replacing it,
// so the default keeps today's behaviour while the other two are real choices:
// "responsive" guarantees at least one update a second even for a widget that
// asked for less, and "battery conscious" refuses to poll faster than every
// thirty seconds no matter how eager the widget is.
[[nodiscard]] std::chrono::milliseconds applyRefreshPolicy(const QString& policy,
                                                           std::chrono::milliseconds requested) {
    if (policy == QStringLiteral("responsive")) {
        return std::min(requested, std::chrono::milliseconds(std::chrono::seconds(1)));
    }
    if (policy == QStringLiteral("battery")) {
        return std::max(requested, std::chrono::milliseconds(std::chrono::seconds(30)));
    }
    return requested;
}

[[nodiscard]] bool isSupportedRefreshPolicy(const QString& policy) {
    static const QSet<QString> supported{QStringLiteral("responsive"), QStringLiteral("balanced"),
                                         QStringLiteral("battery")};
    return supported.contains(policy);
}

[[nodiscard]] QString compactJson(const QJsonObject& object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QString commandErrorName(const QString& errorCode) {
    if (errorCode == QStringLiteral("unknownDevice")) {
        return QString::fromLatin1(DbusError::unknownDevice);
    }
    if (errorCode == QStringLiteral("unsupportedCapability")) {
        return QString::fromLatin1(DbusError::unsupportedCapability);
    }
    if (errorCode == QStringLiteral("safetyRejected")) {
        return QString::fromLatin1(DbusError::safetyRejected);
    }
    return QString::fromLatin1(DbusError::unsupportedCommand);
}

struct DiscoveryResult {
    QList<DeviceRecord> devices;
    QString errorName;
    QString errorMessage;
};

[[nodiscard]] DiscoveryResult parseDiscoveredDevices(const ProcessResult& result) {
    if (result.timedOut) {
        return {
            {}, QString::fromLatin1(DbusError::timeout), QStringLiteral("rds discovery timed out")};
    }
    if (result.exitCode != 0) {
        return {{},
                QString::fromLatin1(DbusError::transportFailure),
                QStringLiteral("rds discovery failed")};
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(result.standardOutput, &error);
    if (error.error != QJsonParseError::NoError) {
        return {{},
                QString::fromLatin1(DbusError::transportFailure),
                QStringLiteral("rds discovery returned malformed JSON")};
    }

    if (!document.isArray() &&
        (!document.isObject() || !document.object().value(QStringLiteral("devices")).isArray())) {
        return {{},
                QString::fromLatin1(DbusError::transportFailure),
                QStringLiteral("rds discovery returned an unsupported payload")};
    }

    DiscoveryResult discovery;
    const QJsonArray values = document.isArray()
                                  ? document.array()
                                  : document.object().value(QStringLiteral("devices")).toArray();
    for (const QJsonValue& value : values) {
        if (!value.isObject()) {
            return {{},
                    QString::fromLatin1(DbusError::transportFailure),
                    QStringLiteral("rds discovery returned an invalid device entry")};
        }
        const QJsonObject object = value.toObject();
        DeviceRecord device;
        device.serial = object.value(QStringLiteral("serialNumber")).toString().trimmed();
        device.displayName = object.value(QStringLiteral("deviceName")).toString().trimmed();
        device.address = object.value(QStringLiteral("ip")).toString().trimmed();
        if (!device.serial.isEmpty() && !device.address.isEmpty() && device.serial.size() <= 256 &&
            device.displayName.size() <= 256 && device.address.size() <= 256) {
            discovery.devices.append(std::move(device));
        } else {
            return {{},
                    QString::fromLatin1(DbusError::transportFailure),
                    QStringLiteral("rds discovery returned incomplete device information")};
        }
    }
    return discovery;
}

// Ruling 2: a window spans [start, end) on the local wall clock — inclusive
// start, exclusive end — and end < start wraps midnight (22:00→08:00 spans
// overnight). parseRule guarantees start != end and HH:MM validity; an
// unparsable legacy bound fails open rather than suppressing firings.
[[nodiscard]] bool timeWithinWindow(const QTime& now, const QString& startText,
                                    const QString& endText) {
    const QTime start = QTime::fromString(startText, QStringLiteral("HH:mm"));
    const QTime end = QTime::fromString(endText, QStringLiteral("HH:mm"));
    if (!start.isValid() || !end.isValid()) {
        return true;
    }
    if (start < end) {
        return now >= start && now < end;
    }
    return now >= start || now < end;  // wraps midnight
}

} // namespace

DaemonService::DaemonService(QDBusConnection connection,
                             std::function<bool()> hasEnabledAutomations,
                             std::unique_ptr<ProcessRunner> processRunner,
                             std::unique_ptr<ConfigStore> configStore,
                             std::chrono::milliseconds idleGrace,
                             DeadlineRemaining deadlineRemaining, WallClock wallClock,
                             std::chrono::milliseconds localPcPollInterval, QObject* parent)
    : QObject(parent), m_connection(std::move(connection)),
      m_hasEnabledAutomations(hasEnabledAutomations ? std::move(hasEnabledAutomations)
                                                    : [] { return false; }),
      m_deadlineRemaining(
          deadlineRemaining
              ? std::move(deadlineRemaining)
              : [](const QDeadlineTimer& deadline) { return deadline.remainingTime(); }),
      m_idleGrace(std::max(idleGrace, std::chrono::milliseconds(1))),
      m_configStore(configStore ? std::move(configStore) : std::make_unique<ConfigStore>()),
      m_processRunner(processRunner ? std::move(processRunner)
                                    : std::make_unique<QProcessRunner>()),
      m_rdsClient(m_processRunner.get()), m_ownerWatcher(this),
      m_wallClock(wallClock ? std::move(wallClock)
                            : [] { return QDateTime::currentDateTime(); }) {
    new RokuDeck1Adaptor(this);
    m_ownerWatcher.setConnection(m_connection);
    m_ownerWatcher.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);

    // Spec §2.2: track the whole org.mpris.MediaPlayer2.* namespace. Qt's
    // service watcher only matches exact names for third-party registrations,
    // so subscribe to NameOwnerChanged directly and enumerate players that
    // registered before daemon startup.
    QDBusMessage listNames = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("ListNames"));
    auto* pending = new QDBusPendingCallWatcher(m_connection.asyncCall(listNames), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher* finished) {
                finished->deleteLater();
                if (m_shuttingDown) {
                    return;
                }
                const QDBusMessage reply = finished->reply();
                if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
                    return;
                }
                const QStringList names = reply.arguments().front().toStringList();
                for (const QString& name : names) {
                    if (name.startsWith(QLatin1String("org.mpris.MediaPlayer2."))) {
                        mprisPlayerAppeared(name);
                    }
                }
            });
    static_cast<void>(m_connection.connect(
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameOwnerChanged"), this,
        SLOT(mprisNameOwnerChanged(QString,QString,QString))));
    static_cast<void>(m_connection.connect(
        QString(), QStringLiteral("/org/mpris/MediaPlayer2"),
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"),
        this, SLOT(mprisPropertiesChanged(QString,QVariantMap,QStringList))));

    for (const DeviceRecord& device : m_configStore->loadDevices()) {
        m_deviceRegistry.upsert(device);
        m_addressGenerations.insert(device.serial, 1);
    }
    // Spec §1: the daemon always owns the machine it runs on as a reserved
    // device, so rules and actions can address This PC like any other target.
    if (!findDevice(QLatin1String(localPcSerial))) {
        DeviceRecord localDevice;
        localDevice.serial = QLatin1String(localPcSerial);
        localDevice.displayName = QStringLiteral("This PC");
        localDevice.address = QStringLiteral("127.0.0.1");
        localDevice.sortIndex = nextSortIndex();
        m_deviceRegistry.upsert(localDevice);
        m_addressGenerations.insert(localDevice.serial, 1);
        m_configStore->saveDevices(m_deviceRegistry.devices());
    }
    m_rules = m_configStore->loadRules();
    m_ruleScheduler.updateRulesAtStartup(m_rules, m_wallClock());
    m_scheduleTimer.setSingleShot(true);
    connect(&m_scheduleTimer, &QTimer::timeout, this, &DaemonService::evaluateDueRules);
    rearmScheduleTimer();
    evaluateDueRules();  // spec §4.2: fresh missed `once` rules fire once at boot

    m_idleTimer.setSingleShot(true);
    m_idleTimer.setInterval(static_cast<int>(std::min<std::chrono::milliseconds::rep>(
        m_idleGrace.count(), std::numeric_limits<int>::max())));
    connect(&m_idleTimer, &QTimer::timeout, this, &DaemonService::handleIdleTimeout);

    m_pollTimer.setInterval(50);
    connect(&m_pollTimer, &QTimer::timeout, this, &DaemonService::pollDueDevices);
    m_pollTimer.start();

    m_pcPollTimer.setInterval(static_cast<int>(std::min<std::chrono::milliseconds::rep>(
        localPcPollInterval.count(), std::numeric_limits<int>::max())));
    connect(&m_pcPollTimer, &QTimer::timeout, this, &DaemonService::pollLocalPcAudio);
    m_pcPollTimer.start();
    connect(&m_ownerWatcher, &QDBusServiceWatcher::serviceUnregistered, this,
            &DaemonService::removeClientsForOwner);
    updateIdleTimer();
}

DaemonService::~DaemonService() {
    m_shuttingDown = true;
    m_pollTimer.stop();
    m_pcPollTimer.stop();
    m_idleTimer.stop();
    m_commandQueues.clear();
    for (const QDBusMessage& request : std::as_const(m_discoveryReplies)) {
        m_connection.send(request.createErrorReply(
            QString::fromLatin1(DbusError::transportFailure),
            QStringLiteral("RokuDeck stopped before discovery completed")));
    }
    m_discoveryReplies.clear();
    if (m_registeredOnBus) {
        m_connection.unregisterService(QString::fromLatin1(serviceName));
        m_connection.unregisterObject(QString::fromLatin1(objectPath));
    }
}

bool DaemonService::registerOnBus() {
    m_lastRegistrationError.clear();
    if (m_registeredOnBus) {
        return true;
    }
    if (!m_connection.isConnected()) {
        m_lastRegistrationError = QStringLiteral("the session bus is not connected");
        return false;
    }
    if (!m_connection.registerObject(QString::fromLatin1(objectPath), this,
                                     QDBusConnection::ExportAdaptors)) {
        m_lastRegistrationError = m_connection.lastError().message();
        if (m_lastRegistrationError.isEmpty()) {
            m_lastRegistrationError = QStringLiteral("failed to register the D-Bus object");
        }
        return false;
    }
    if (!m_connection.registerService(QString::fromLatin1(serviceName))) {
        m_lastRegistrationError = m_connection.lastError().message();
        if (m_lastRegistrationError.isEmpty()) {
            m_lastRegistrationError = QStringLiteral("failed to own the D-Bus service name");
        }
        m_connection.unregisterObject(QString::fromLatin1(objectPath));
        return false;
    }
    m_registeredOnBus = true;
    return true;
}

void DaemonService::upsertDevice(const DeviceRecord& device) {
    if (device.serial.isEmpty()) {
        return;
    }
    if (mergeDevice(device)) {
        persistAndAnnounceDevices();
    }
}

bool DaemonService::mergeDevice(const DeviceRecord& device, MergeMode mode) {
    if (device.serial.isEmpty()) {
        return false;
    }
    const auto previous = findDevice(device.serial);

    // Identity merges (discovery, polling) only carry serial, name and address.
    // Copying the stored preferences forward keeps a rediscovered device from
    // silently re-enabling automations or losing the order the user chose.
    DeviceRecord merged = device;
    if (mode == MergeMode::PreserveSettings) {
        if (previous) {
            // Discovery and polling report the name the Roku advertises. Once
            // the user has named the device themselves, that name wins.
            merged.nameIsUserChosen = previous->nameIsUserChosen;
            if (previous->nameIsUserChosen) {
                merged.displayName = previous->displayName;
            }
            merged.allowAutomations = previous->allowAutomations;
            merged.wakeOnWlan = previous->wakeOnWlan;
            merged.refreshPolicy = previous->refreshPolicy;
            merged.volumeCeiling = previous->volumeCeiling;
            merged.sortIndex = previous->sortIndex;
        } else {
            merged.sortIndex = nextSortIndex();
        }
    }

    const bool changed = !previous || previous->displayName != merged.displayName ||
                         previous->address != merged.address ||
                         previous->nameIsUserChosen != merged.nameIsUserChosen ||
                         previous->allowAutomations != merged.allowAutomations ||
                         previous->wakeOnWlan != merged.wakeOnWlan ||
                         previous->refreshPolicy != merged.refreshPolicy ||
                         previous->volumeCeiling != merged.volumeCeiling ||
                         previous->sortIndex != merged.sortIndex;
    if (!changed) {
        return false;
    }

    const bool addressChanged = previous && previous->address != merged.address;
    m_deviceRegistry.upsert(merged);
    if (!previous) {
        m_addressGenerations.insert(merged.serial, 1);
    } else if (addressChanged) {
        m_addressGenerations[merged.serial] = m_addressGenerations.value(merged.serial, 1) + 1;
        m_snapshots.remove(merged.serial);
    } else {
        auto snapshotIt = m_snapshots.find(merged.serial);
        if (snapshotIt != m_snapshots.end()) {
            snapshotIt->displayName = merged.displayName;
            snapshotIt->address = merged.address;
        }
    }
    return true;
}

int DaemonService::nextSortIndex() const {
    int next = 0;
    for (const DeviceRecord& device : m_deviceRegistry.devices()) {
        next = std::max(next, device.sortIndex + 1);
    }
    return next;
}

void DaemonService::persistAndAnnounceDevices() {
    m_configStore->saveDevices(m_deviceRegistry.devices());
    Q_EMIT DeviceListChanged(ListDevices());
}

QJsonObject DaemonService::deviceObject(const DeviceRecord& device) {
    return QJsonObject{{QStringLiteral("serial"), device.serial},
                       {QStringLiteral("displayName"), device.displayName},
                       {QStringLiteral("address"), device.address},
                        {QStringLiteral("allowAutomations"), device.allowAutomations},
                        {QStringLiteral("wakeOnWlan"), device.wakeOnWlan},
                        {QStringLiteral("refreshPolicy"), device.refreshPolicy},
                        {QStringLiteral("volumeCeiling"), device.volumeCeiling}};
}

QString DaemonService::devicesJson(const QList<DeviceRecord>& devices) {
    QJsonArray entries;
    for (const DeviceRecord& device : devices) {
        entries.append(deviceObject(device));
    }
    return compactJson(
        QJsonObject{{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("devices"), entries}});
}

QString DaemonService::snapshotJson(const DeviceSnapshot& snapshot, const QString& diagnostic) {
    QJsonObject object{{QStringLiteral("schemaVersion"), 1},
                       {QStringLiteral("serial"), snapshot.serial},
                       {QStringLiteral("displayName"), snapshot.displayName},
                       {QStringLiteral("address"), snapshot.address},
                       {QStringLiteral("power"), powerName(snapshot.power)},
                       {QStringLiteral("screensaver"), triStateName(snapshot.screensaver)},
                       {QStringLiteral("media"), mediaName(snapshot.media)},
                       {QStringLiteral("health"), healthName(snapshot.health)},
                       {QStringLiteral("partialFailure"), snapshot.partialFailure}};
    if (snapshot.activeApp) {
        object.insert(QStringLiteral("activeApp"),
                      QJsonObject{{QStringLiteral("id"), snapshot.activeApp->id},
                                  {QStringLiteral("name"), snapshot.activeApp->name},
                                  {QStringLiteral("type"), snapshot.activeApp->type},
                                  {QStringLiteral("input"), snapshot.activeApp->input}});
    } else {
        object.insert(QStringLiteral("activeApp"), QJsonValue::Null);
    }
    object.insert(QStringLiteral("volume"),
                  snapshot.volume ? QJsonValue(*snapshot.volume) : QJsonValue::Null);
    object.insert(QStringLiteral("muted"),
                  snapshot.muted ? QJsonValue(*snapshot.muted) : QJsonValue::Null);
    object.insert(QStringLiteral("playing"),
                  snapshot.playing ? QJsonValue(*snapshot.playing) : QJsonValue::Null);

    QStringList capabilities(snapshot.capabilities.cbegin(), snapshot.capabilities.cend());
    capabilities.sort();
    QJsonArray capabilityArray;
    for (const QString& capability : capabilities) {
        capabilityArray.append(capability);
    }
    object.insert(QStringLiteral("capabilities"), capabilityArray);
    if (snapshot.capturedAt.isValid()) {
        object.insert(QStringLiteral("capturedAt"),
                      snapshot.capturedAt.toString(Qt::ISODateWithMs));
    }
    if (!diagnostic.isEmpty()) {
        object.insert(QStringLiteral("diagnostic"), diagnostic);
    }
    return compactJson(object);
}

QString DaemonService::ListDevices() const {
    return devicesJson(m_deviceRegistry.devices());
}

QString DaemonService::GetSnapshot(const QString& serial) const {
    const auto snapshotIt = m_snapshots.constFind(serial);
    if (snapshotIt != m_snapshots.cend()) {
        return snapshotJson(snapshotIt.value(), m_pollingDiagnostic);
    }

    const auto device = findDevice(serial);
    if (!device) {
        if (calledFromDBus()) {
            sendErrorReply(QString::fromLatin1(DbusError::unknownDevice),
                           QStringLiteral("unknown Roku device serial"));
        }
        return {};
    }
    DeviceSnapshot snapshot;
    snapshot.serial = device->serial;
    snapshot.displayName = device->displayName;
    snapshot.address = device->address;
    return snapshotJson(snapshot, m_pollingDiagnostic);
}

QString DaemonService::callerOwner() const {
    if (calledFromDBus() && !message().service().isEmpty()) {
        return message().service();
    }
    return m_connection.baseService();
}

void DaemonService::removePollInterests(const ClientRegistration& registration) {
    const QString schedulerClientId = registration.owner + QChar(u'\x1f') + registration.clientId;
    for (const QString& serial : registration.serials) {
        static_cast<void>(m_pollScheduler.removeInterest(schedulerClientId, serial));
    }
}

std::chrono::milliseconds DaemonService::policyAdjustedInterval(const QString& serial,
                                                     std::chrono::milliseconds requested) const {
    const auto device = findDevice(serial);
    return applyRefreshPolicy(device ? device->refreshPolicy : QStringLiteral("balanced"),
                              requested);
}

void DaemonService::applyPollInterests(const ClientRegistration& registration) {
    const QString schedulerClientId = registration.owner + QChar(u'\x1f') + registration.clientId;
    for (const QString& serial : registration.serials) {
        m_pollScheduler.setInterest(
            {schedulerClientId,
             serial,
             {QStringLiteral("power"), QStringLiteral("screensaver"), QStringLiteral("activeApp"),
              QStringLiteral("media"), QStringLiteral("volume"), QStringLiteral("muted")},
             policyAdjustedInterval(serial, std::chrono::milliseconds(registration.intervalMs))});
    }
}

void DaemonService::reapplyPollInterests() {
    for (const ClientRegistration& registration : m_clients.registrations()) {
        applyPollInterests(registration);
    }
}

void DaemonService::watchOwner(const QString& owner) {
    if (owner.startsWith(QChar(u':')) && !m_ownerWatcher.watchedServices().contains(owner)) {
        m_ownerWatcher.addWatchedService(owner);
    }
}

void DaemonService::RegisterClient(const QString& clientId, const QStringList& serials,
                                   uint intervalMs) {
    const QString owner = callerOwner();
    const auto previous = m_clients.registration(owner, clientId);
    if (!m_clients.registerClient(owner, clientId, serials, intervalMs)) {
        if (calledFromDBus()) {
            sendErrorReply(QDBusError::InvalidArgs,
                           QStringLiteral("clientId and intervalMs must be valid"));
        }
        return;
    }
    if (previous) {
        removePollInterests(*previous);
    }
    applyPollInterests(*m_clients.registration(owner, clientId));
    watchOwner(owner);
    updateIdleTimer();
}

void DaemonService::UpdateClientInterest(const QString& clientId, const QStringList& serials,
                                         uint intervalMs) {
    const QString owner = callerOwner();
    const auto previous = m_clients.registration(owner, clientId);
    if (!previous || !m_clients.updateClient(owner, clientId, serials, intervalMs)) {
        if (calledFromDBus()) {
            sendErrorReply(QDBusError::InvalidArgs,
                           QStringLiteral("the client is not registered or arguments are invalid"));
        }
        return;
    }
    removePollInterests(*previous);
    applyPollInterests(*m_clients.registration(owner, clientId));
    updateIdleTimer();
}

void DaemonService::UnregisterClient(const QString& clientId) {
    const QString owner = callerOwner();
    const auto removed = m_clients.takeClient(owner, clientId);
    if (!removed) {
        if (calledFromDBus()) {
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("the client is not registered"));
        }
        return;
    }
    removePollInterests(*removed);
    if (!m_clients.containsOwner(owner)) {
        m_ownerWatcher.removeWatchedService(owner);
    }
    updateIdleTimer();
}

DaemonService::ValidatedRecord
DaemonService::validateDeviceJson(const QString& deviceJson,
                                  const std::optional<DeviceRecord>& base) const {
    const QByteArray encoded = deviceJson.toUtf8();
    if (encoded.size() > maximumDeviceJsonBytes) {
        return {{}, QStringLiteral("the device payload is too large")};
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(encoded, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {{}, QStringLiteral("the device payload is not a JSON object")};
    }
    const QJsonObject object = document.object();

    static const QSet<QString> allowedKeys{
        QStringLiteral("serial"),           QStringLiteral("displayName"),
        QStringLiteral("address"),          QStringLiteral("allowAutomations"),
        QStringLiteral("wakeOnWlan"),       QStringLiteral("refreshPolicy"),
        QStringLiteral("volumeCeiling")};
    const QStringList keys = object.keys();
    for (const QString& key : keys) {
        if (!allowedKeys.contains(key)) {
            return {{}, QStringLiteral("unsupported device key: %1").arg(key)};
        }
    }

    DeviceRecord device = base.value_or(DeviceRecord{});

    if (object.contains(QStringLiteral("serial"))) {
        const QJsonValue value = object.value(QStringLiteral("serial"));
        if (!value.isString()) {
            return {{}, QStringLiteral("serial must be a string")};
        }
        const QString serial = value.toString().trimmed();
        if (serial.isEmpty() || serial.size() > maximumSerialLength ||
            !isPrintableSingleLine(serial)) {
            return {{}, QStringLiteral("serial is empty or malformed")};
        }
        // A stored device's identity is its key; changing it would orphan its
        // snapshots, poll claims and per-widget selections.
        if (base && serial != base->serial) {
            return {{}, QStringLiteral("a device serial cannot be changed")};
        }
        device.serial = serial;
    }
    if (device.serial.isEmpty()) {
        return {{}, QStringLiteral("serial is required")};
    }

    if (object.contains(QStringLiteral("displayName"))) {
        const QJsonValue value = object.value(QStringLiteral("displayName"));
        if (!value.isString()) {
            return {{}, QStringLiteral("displayName must be a string")};
        }
        const QString displayName = value.toString().trimmed();
        if (displayName.size() > maximumDisplayNameLength || !isPrintableSingleLine(displayName)) {
            return {{}, QStringLiteral("displayName is too long or contains control characters")};
        }
        device.displayName = displayName;
        if (base) {
            device.nameIsUserChosen = !displayName.isEmpty();
        }
    }

    if (object.contains(QStringLiteral("address"))) {
        const QJsonValue value = object.value(QStringLiteral("address"));
        if (!value.isString() || !isRoutableIpv4(value.toString().trimmed())) {
            return {{}, QStringLiteral("address must be a routable IPv4 address")};
        }
        device.address = value.toString().trimmed();
    }
    if (device.address.isEmpty()) {
        return {{}, QStringLiteral("address is required")};
    }

    if (object.contains(QStringLiteral("allowAutomations"))) {
        const QJsonValue value = object.value(QStringLiteral("allowAutomations"));
        if (!value.isBool()) {
            return {{}, QStringLiteral("allowAutomations must be a boolean")};
        }
        device.allowAutomations = value.toBool();
    }

    if (object.contains(QStringLiteral("wakeOnWlan"))) {
        const QJsonValue value = object.value(QStringLiteral("wakeOnWlan"));
        if (!value.isBool()) {
            return {{}, QStringLiteral("wakeOnWlan must be a boolean")};
        }
        device.wakeOnWlan = value.toBool();
    }

    if (object.contains(QStringLiteral("refreshPolicy"))) {
        const QJsonValue value = object.value(QStringLiteral("refreshPolicy"));
        if (!value.isString() || !isSupportedRefreshPolicy(value.toString())) {
            return {{}, QStringLiteral("refreshPolicy must be responsive, balanced or battery")};
        }
        device.refreshPolicy = value.toString();
    }

    if (object.contains(QStringLiteral("volumeCeiling"))) {
        const QJsonValue value = object.value(QStringLiteral("volumeCeiling"));
        // JSON numbers are doubles: only a finite integral value in 0..100 is
        // a ceiling, so 50.5 and "50" cannot sneak through.
        if (!value.isDouble() || !std::isfinite(value.toDouble()) ||
            value.toDouble() < 0 || value.toDouble() > 100 ||
            value.toDouble() != std::trunc(value.toDouble())) {
            return {{}, QStringLiteral("volumeCeiling must be an integer between 0 and 100")};
        }
        device.volumeCeiling = value.toInt();
    }

    return {device, {}};
}

QString DaemonService::AddDevice(const QString& deviceJson) {
    const ValidatedRecord validated = validateDeviceJson(deviceJson, std::nullopt);
    if (!validated.errorMessage.isEmpty()) {
        if (calledFromDBus()) {
            sendErrorReply(QDBusError::InvalidArgs, validated.errorMessage);
        }
        return {};
    }
    if (findDevice(validated.device.serial)) {
        if (calledFromDBus()) {
            sendErrorReply(QDBusError::InvalidArgs,
                           QStringLiteral("device %1 is already configured")
                               .arg(validated.device.serial));
        }
        return {};
    }

    DeviceRecord device = validated.device;
    device.sortIndex = nextSortIndex();
    if (mergeDevice(device, MergeMode::ApplySettings)) {
        persistAndAnnounceDevices();
    }
    return compactJson(deviceObject(device));
}

QString DaemonService::UpdateDevice(const QString& serial, const QString& changesJson) {
    const auto existing = findDevice(serial);
    if (!existing) {
        if (calledFromDBus()) {
            sendErrorReply(QString::fromLatin1(DbusError::unknownDevice),
                           QStringLiteral("unknown device %1").arg(serial));
        }
        return {};
    }
    const ValidatedRecord validated = validateDeviceJson(changesJson, existing);
    if (!validated.errorMessage.isEmpty()) {
        if (calledFromDBus()) {
            sendErrorReply(QDBusError::InvalidArgs, validated.errorMessage);
        }
        return {};
    }
    // Consent lives on the machine itself: This PC always allows automations.
    if (serial == QLatin1String(localPcSerial) && !validated.device.allowAutomations) {
        if (calledFromDBus()) {
            sendErrorReply(QDBusError::InvalidArgs,
                           QStringLiteral("the local device always allows automations"));
        }
        return {};
    }
    if (mergeDevice(validated.device, MergeMode::ApplySettings)) {
        if (validated.device.refreshPolicy != existing->refreshPolicy) {
            reapplyPollInterests();
        }
        persistAndAnnounceDevices();
    }
    return compactJson(deviceObject(validated.device));
}

void DaemonService::RemoveDevice(const QString& serial) {
    if (serial == QLatin1String(localPcSerial)) {
        if (calledFromDBus()) {
            sendErrorReply(QDBusError::InvalidArgs,
                           QStringLiteral("the local device cannot be removed"));
        }
        return;
    }
    if (!m_deviceRegistry.remove(serial)) {
        if (calledFromDBus()) {
            sendErrorReply(QString::fromLatin1(DbusError::unknownDevice),
                           QStringLiteral("unknown device %1").arg(serial));
        }
        return;
    }
    m_snapshots.remove(serial);
    m_addressGenerations.remove(serial);
    persistAndAnnounceDevices();
}

void DaemonService::SetDeviceOrder(const QStringList& serials) {
    QSet<QString> seen;
    for (const QString& serial : serials) {
        if (seen.contains(serial)) {
            if (calledFromDBus()) {
                sendErrorReply(QDBusError::InvalidArgs,
                               QStringLiteral("device %1 is listed twice").arg(serial));
            }
            return;
        }
        if (!findDevice(serial)) {
            if (calledFromDBus()) {
                sendErrorReply(QString::fromLatin1(DbusError::unknownDevice),
                               QStringLiteral("unknown device %1").arg(serial));
            }
            return;
        }
        seen.insert(serial);
    }

    // Devices the caller left out keep their existing relative order behind the
    // ones it named, so a stale widget can never drop a device off the list.
    QStringList ordered = serials;
    for (const DeviceRecord& device : m_deviceRegistry.devices()) {
        if (!seen.contains(device.serial)) {
            ordered.append(device.serial);
        }
    }

    bool changed = false;
    for (qsizetype index = 0; index < ordered.size(); ++index) {
        const auto existing = findDevice(ordered.at(index));
        if (!existing || existing->sortIndex == static_cast<int>(index)) {
            continue;
        }
        DeviceRecord device = *existing;
        device.sortIndex = static_cast<int>(index);
        changed = mergeDevice(device, MergeMode::ApplySettings) || changed;
    }
    if (changed) {
        persistAndAnnounceDevices();
    }
}

QString DaemonService::TestDevice(const QString& address) {
    const QString trimmed = address.trimmed();
    if (!isRoutableIpv4(trimmed)) {
        if (calledFromDBus()) {
            sendErrorReply(QDBusError::InvalidArgs,
                           QStringLiteral("address must be a routable IPv4 address"));
        }
        return {};
    }

    constexpr qsizetype maximumConcurrentTests = 4;
    const bool alreadyProbing = m_deviceTestReplies.contains(trimmed);
    if (!alreadyProbing && m_deviceTestReplies.size() >= maximumConcurrentTests) {
        if (calledFromDBus()) {
            sendErrorReply(QDBusError::LimitsExceeded,
                           QStringLiteral("too many device tests are already running"));
        }
        return {};
    }

    if (calledFromDBus()) {
        setDelayedReply(true);
        m_deviceTestReplies[trimmed].append(message());
    } else {
        m_deviceTestReplies[trimmed];
    }
    if (alreadyProbing) {
        // A probe for this address is already in flight; share its result.
        return {};
    }

    updateIdleTimer();
    QPointer<DaemonService> service(this);
    m_rdsClient.queryDevice(trimmed, [service, trimmed](RdsQueryResult result) {
        if (!service || service->m_shuttingDown) {
            return;
        }
        const QList<QDBusMessage> replies = service->m_deviceTestReplies.take(trimmed);
        for (const QDBusMessage& pendingReply : replies) {
            if (result.has_value()) {
                DeviceSnapshot snapshot = result.value();
                snapshot.address = trimmed;
                service->m_connection.send(
                    pendingReply.createReply(DaemonService::snapshotJson(snapshot)));
            } else {
                const RdsError& error = result.error();
                const QString errorName =
                    error.code == RdsError::Code::TimedOut
                        ? QString::fromLatin1(DbusError::timeout)
                        : QString::fromLatin1(DbusError::transportFailure);
                service->m_connection.send(
                    pendingReply.createErrorReply(errorName, error.message));
            }
        }
        service->updateIdleTimer();
    });
    return {};
}

QString DaemonService::DiscoverDevices() {
    if (calledFromDBus()) {
        setDelayedReply(true);
        m_discoveryReplies.append(message());
    }
    if (m_discoveryActive) {
        return {};
    }

    m_discoveryActive = true;
    updateIdleTimer();
    ProcessRequest request;
    request.program = QStringLiteral("rds");
    request.arguments = {QStringLiteral("--json"), QStringLiteral("--quiet"),
                         QStringLiteral("--timeout"), QStringLiteral("3000"),
                         QStringLiteral("discover")};
    request.timeout = std::chrono::milliseconds(7000);
    QPointer<DaemonService> service(this);
    m_processRunner->start(request, [service](ProcessResult result) {
        if (!service || service->m_shuttingDown) {
            return;
        }
        service->m_discoveryActive = false;
        const DiscoveryResult discovery = parseDiscoveredDevices(result);
        bool changed = false;
        for (const DeviceRecord& device : discovery.devices) {
            changed = service->mergeDevice(device) || changed;
        }
        if (changed) {
            service->persistAndAnnounceDevices();
        }
        const QList<QDBusMessage> replies = std::exchange(service->m_discoveryReplies, {});
        for (const QDBusMessage& pendingReply : replies) {
            const QDBusMessage reply =
                discovery.errorName.isEmpty()
                    ? pendingReply.createReply(service->ListDevices())
                    : pendingReply.createErrorReply(discovery.errorName, discovery.errorMessage);
            service->m_connection.send(reply);
        }
        service->updateIdleTimer();
    });
    return {};
}

QString DaemonService::rulesJson() const {
    QJsonArray array;
    for (const RuleDocument& rule : m_rules) {
        array.append(ruleJson(rule));
    }
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                                  {QStringLiteral("rules"), array}})
            .toJson(QJsonDocument::Compact));
}

void DaemonService::announceRules() {
    m_configStore->saveRules(m_rules);
    m_ruleScheduler.updateRules(m_rules, m_wallClock());
    rearmScheduleTimer();
    Q_EMIT RulesChanged(rulesJson());
}

void DaemonService::rearmScheduleTimer() {
    const QDateTime deadline = m_ruleScheduler.nextDeadline();
    if (!deadline.isValid()) {
        m_scheduleTimer.stop();
        updateIdleTimer();
        return;
    }
    const qint64 until = std::max<qint64>(0, m_wallClock().msecsTo(deadline));
    // Cap the arm at 60s: a re-evaluation every minute re-anchors schedules
    // in local time and absorbs system sleep/wake without a second timer.
    m_scheduleTimer.start(static_cast<int>(std::min<qint64>(until, 60000)));
    updateIdleTimer();
}

void DaemonService::evaluateDueRules() {
    if (m_dispatchActive) {
        return;  // a run is mid-delay; the tail call re-evaluates when done
    }
    const QStringList due = m_ruleScheduler.dueRuleIds(m_wallClock());
    for (const QString& ruleId : due) {
        m_dispatchActive = true;
        static_cast<void>(requestRuleFire(ruleId));
        m_dispatchActive = false;
    }
    rearmScheduleTimer();
}

void DaemonService::runStateChangeFirings(const QStringList& ruleIds) {
    if (m_dispatchActive) {
        return;  // a run is mid-delay; dropping keeps the pipeline sequential
    }
    for (const QString& ruleId : ruleIds) {
        m_dispatchActive = true;
        static_cast<void>(requestRuleFire(ruleId));
        m_dispatchActive = false;
    }
}

QString DaemonService::nextRuleId() const {
    static const QString alphabet =
        QStringLiteral("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    QString id;
    do {
        id = QStringLiteral("r");
        while (id.size() < 9) {
            id.append(alphabet.at(QRandomGenerator::global()->bounded(alphabet.size())));
        }
    } while (std::any_of(m_rules.cbegin(), m_rules.cend(),
                         [&id](const RuleDocument& rule) { return rule.id == id; }));
    return id;
}

QString DaemonService::ListRules() const {
    return rulesJson();
}

QString DaemonService::AddRule(const QString& ruleJsonText) {
    const QJsonDocument document = QJsonDocument::fromJson(ruleJsonText.toUtf8());
    if (!document.isObject()) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("invalidRule"));
        return {};
    }
    QJsonObject object = document.object();
    // The daemon owns these fields; a client supplying them is a contract
    // violation rather than an update.
    for (const QString& key : {QStringLiteral("id"), QStringLiteral("enabled")}) {
        if (object.contains(key)) {
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("invalidRule"));
            return {};
        }
    }
    object.insert(QStringLiteral("schemaVersion"), 1);
    const auto parsed = parseRule(object);
    if (!parsed) {
        sendErrorReply(QDBusError::InvalidArgs, parsed.error().message);
        return {};
    }
    RuleDocument rule = parsed.value();
    rule.id = nextRuleId();
    rule.enabled = true;
    m_rules.append(rule);
    announceRules();
    return QString::fromUtf8(QJsonDocument(ruleJson(rule)).toJson());
}

QList<RuleDocument>::iterator DaemonService::findRule(const QString& ruleId) {
    return std::find_if(m_rules.begin(), m_rules.end(),
                        [&ruleId](const RuleDocument& rule) { return rule.id == ruleId; });
}

void DaemonService::UpdateRule(const QString& ruleJsonText) {
    const QJsonDocument document = QJsonDocument::fromJson(ruleJsonText.toUtf8());
    if (!document.isObject()) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("invalidRule"));
        return;
    }
    const auto parsed = parseRule(document.object());
    if (!parsed) {
        sendErrorReply(QDBusError::InvalidArgs, parsed.error().message);
        return;
    }
    const auto existing = findRule(parsed.value().id);
    if (existing == m_rules.end()) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("unknownRule"));
        return;
    }
    // Replacement is in place: the rule keeps its insertion position.
    *existing = parsed.value();
    announceRules();
}

void DaemonService::DeleteRule(const QString& ruleId) {
    const auto existing = findRule(ruleId);
    if (existing == m_rules.end()) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("unknownRule"));
        return;
    }
    m_rules.erase(existing);
    announceRules();
}

void DaemonService::SetRuleEnabled(const QString& ruleId, bool enabled) {
    const auto existing = findRule(ruleId);
    if (existing == m_rules.end()) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("unknownRule"));
        return;
    }
    // Toggling never round-trips the whole definition.
    existing->enabled = enabled;
    announceRules();
}

QString DaemonService::RunRule(const QString& ruleId) {
    const auto existing = findRule(ruleId);
    if (existing == m_rules.end()) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("unknownRule"));
        return {};
    }
    // Spec §4.3: only enabled rules fire — manual runs included.
    if (!existing->enabled) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("disabledRule"));
        return {};
    }
    // Manual runs pass the same mode gate as every other firing origin.
    return requestRuleFire(ruleId);
}

QString DaemonService::requestRuleFire(const QString& ruleId) {
    const auto existing = findRule(ruleId);
    if (existing == m_rules.end() || !existing->enabled) {
        return {};  // erased or disabled between scheduling and firing
    }

    // Ruling 1: the time-of-day window gates BEFORE the mode machine —
    // outside it this firing never happened, whatever the run states say.
    // Every origin (scheduled, state edge, D-Bus RunRule) funnels through
    // here, so one check covers them all.
    if (existing->trigger.activeWindow.has_value()
        && !timeWithinWindow(m_wallClock().time(),
            existing->trigger.activeWindow->start, existing->trigger.activeWindow->end)) {
        recordSuppressedFiring(ruleId, QStringLiteral("inactive-window"));
        return QStringLiteral("inactive-window");
    }
    const QString mode = existing->mode;

    // The §4 mode gate: one Active run per rule, plus a single queued slot in
    // queued mode. Macro children never register here — an action is not a
    // firing — so gating stays trigger-side only.
    QString outcome;
    const int state = m_runStates.value(ruleId, kRunIdle);
    if (state != kRunIdle) {
        if (state == kRunQueued) {
            // Both slots busy (single/queued), or restart merely replaces the
            // coalesced pending firing with this fresher one — still one slot.
            if (mode != QStringLiteral("restart")) {
                recordSuppressedFiring(ruleId, QStringLiteral("dropped"));
                return QStringLiteral("dropped");
            }
            return {};
        }
        if (mode == QStringLiteral("single")) {
            recordSuppressedFiring(ruleId, QStringLiteral("dropped"));
            return QStringLiteral("dropped");
        }
        if (mode != QStringLiteral("restart")) {  // queued, active run in flight
            m_runStates.insert(ruleId, kRunQueued);
            return {};
        }
        // restart: supersede the active run; it unwinds without recording as
        // soon as its current wait yields, while this fresh run starts now.
        if (const auto token = m_runTokens.value(ruleId)) {
            token->store(true);
        }
    }

    for (;;) {
        m_runStates.insert(ruleId, kRunActive);
        auto token = std::make_shared<std::atomic_bool>(false);
        m_runTokens.insert(ruleId, token);
        outcome = executeRuleRun(ruleId, {}, token);
        const bool cancelled = token->load();
        finishRuleRun(ruleId, token);
        if (cancelled) {
            return {};  // superseded: the replacement owns the fields now
        }
        if (m_runStates.value(ruleId, kRunIdle) != kRunQueued) {
            break;
        }
        // A firing arrived while this run ran: take it from queued to active
        // and go again (the drain loop, not recursion).
        m_runStates.insert(ruleId, kRunActive);
    }
    m_runStates.remove(ruleId);
    return outcome;
}

void DaemonService::finishRuleRun(const QString& ruleId,
                                  const std::shared_ptr<std::atomic_bool>& token) {
    // Ownership-aware cleanup: a superseded frame unwinds through its
    // replacement's event pumps, so it must never touch entries a newer run
    // has already replaced — erasing them would leave that replacement
    // un-supersedeable (lost token) or invisible to the gate (lost state).
    if (m_runTokens.value(ruleId) != token) {
        return;
    }
    m_runTokens.remove(ruleId);
    if (!token->load()) {
        return;  // normal completion; the drain loop consumes any queued slot
    }
    // Superseded while still the registered owner: clear the field.
    m_runStates.remove(ruleId);
}

void DaemonService::recordSuppressedFiring(const QString& ruleId, const QString& outcome) {
    if (const auto record = findRule(ruleId); record != m_rules.end()) {
        record->lastRun.firedAtIso = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        record->lastRun.outcome = outcome;
    }
    announceRules();
}

QString DaemonService::executeRuleRun(const QString& ruleId, const QStringList& callStack,
                                      const std::shared_ptr<std::atomic_bool>& cancelToken) {
    const auto existing = findRule(ruleId);
    if (existing == m_rules.end()) {
        return {};
    }
    RuleDocument rule = *existing;

    // Resolve the consenting target set. An empty device list means every
    // known device; listed serials the daemon no longer knows are skipped.
    QStringList targets;
    if (rule.devices.isEmpty()) {
        for (const DeviceRecord& device : m_deviceRegistry.devices()) {
            // This PC joins only when a rule names it explicitly: wildcards
            // mean "my TVs", and the machine running the daemon is not one.
            if (device.serial == QLatin1String(localPcSerial)) {
                continue;
            }
            if (device.allowAutomations) {
                targets.append(device.serial);
            }
        }
    } else {
        for (const QString& serial : rule.devices) {
            const auto device = findDevice(serial);
            if (device && device->allowAutomations) {
                targets.append(serial);
            }
        }
    }

    // Conditions gate any firing after consent has been resolved, so a
    // consent-blocked run never reports condition-false (spec §4 order).
    if (!targets.isEmpty() && rule.conditions &&
        !conditionsMet(*rule.conditions, m_snapshots, targets)) {
        rule.lastRun.firedAtIso = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        rule.lastRun.outcome = QStringLiteral("condition-false");
        // Delay steps pump the event loop, so D-Bus calls may have erased or
        // rewritten the rule while we ran; never resurrect a deleted entry,
        // and only write the fields this run owns so a concurrent rewrite
        // (name, actions, enabled) is not reverted by a stale whole-struct.
        if (const auto record = findRule(rule.id); record != m_rules.end()) {
            record->lastRun.firedAtIso = rule.lastRun.firedAtIso;
            record->lastRun.outcome = rule.lastRun.outcome;
        }
        announceRules();
        evaluateDueRules();  // schedules that fell due while we evaluated
        return QStringLiteral("condition-false");
    }

    QString outcome;
    if (targets.isEmpty()) {
        outcome = QStringLiteral("consent-blocked");
    } else {
        const ActionResult result =
            executeActions(rule, targets, callStack, rule.id, cancelToken);
        if (cancelToken && cancelToken->load()) {
            // A restart superseded this run mid-flight: its partial effects
            // already dispatched stay, but it records no lastRun and announces
            // nothing — the replacing run owns the rule's history.
            return {};
        }
        // Attempts cover both dispatch-time rejections and commands that went
        // out but were not acknowledged by rds; a run only earns "ok" when
        // everything it attempted actually landed.
        const int attempts = result.dispatched + result.rejected;
        const int failures = result.rejected + result.deliveryFailures;
        if (!result.accepted) {
            outcome = QStringLiteral("failed");
        } else if (attempts == 0) {
            outcome = QStringLiteral("ok");
        } else if (failures >= attempts) {
            outcome = QStringLiteral("failed");
        } else if (failures > 0) {
            outcome = QStringLiteral("partial");
        } else {
            outcome = QStringLiteral("ok");
        }
        // A macro cycle cut anywhere in this run's tree is the honest verdict,
        // even when every other step landed. Consent-blocked never ran actions
        // (and condition-false returns earlier), so both keep precedence.
        if (result.cycleDetected && outcome != QStringLiteral("consent-blocked")) {
            outcome = QStringLiteral("cycle-error");
        }
    }

    rule.lastRun.firedAtIso = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    rule.lastRun.outcome = outcome;
    // Delay steps pump the event loop, so D-Bus calls may have erased or
    // rewritten the rule while we ran; never resurrect a deleted entry,
    // and only write the fields this run owns so a concurrent rewrite
    // (name, actions, enabled) is not reverted by a stale whole-struct.
    if (const auto record = findRule(rule.id); record != m_rules.end()) {
        record->lastRun.firedAtIso = rule.lastRun.firedAtIso;
        record->lastRun.outcome = rule.lastRun.outcome;
    }
    announceRules();
    evaluateDueRules();  // schedules that fell due during a delay step
    return outcome;
}

DaemonService::ActionResult DaemonService::executeActions(const RuleDocument& rule,
                                                           const QStringList& targets,
                                                           const QStringList& callStack,
                                                           const QString& currentRuleId,
                                                           const std::shared_ptr<
                                                               std::atomic_bool>& cancelToken) {
    int dispatched = 0;
    int rejected = 0;
    int deliveryFailures = 0;
    bool cycleDetected = false;
    QStringList pendingRequests;
    if (!dispatchActionList(rule.actions, targets, callStack, currentRuleId, dispatched,
                            rejected, deliveryFailures, pendingRequests, cycleDetected,
                            cancelToken)) {
        return {false, QStringLiteral("unsupportedAction"), 0, 0, 0, false};
    }

    // A queued command is an attempt, not a result: wait (bounded by each
    // command's own completion deadline, which always emits a verdict) until
    // every dispatch this run made has either been acknowledged by rds or
    // expired, so the outcome reflects what actually happened on the devices.
    if (!pendingRequests.isEmpty()) {
        QStringList remaining = std::move(pendingRequests);
        QEventLoop wait;
        QTimer backstop;
        backstop.setSingleShot(true);
        backstop.start(CommandContract::daemonCompletionDeadlineMs + 5000);
        auto harvest = [&]() {
            for (auto it = remaining.begin(); it != remaining.end();) {
                const auto verdict = m_deliveryOutcomes.constFind(*it);
                if (verdict == m_deliveryOutcomes.cend()) {
                    ++it;
                    continue;
                }
                if (!verdict.value()) {
                    ++deliveryFailures;
                }
                m_deliveryOutcomes.erase(verdict);
                it = remaining.erase(it);
            }
            return remaining.isEmpty();
        };
        harvest();
        const QMetaObject::Connection verdicts =
            connect(this, &DaemonService::CommandFinished, &wait,
                    [&](const QString&, const QString&) {
                        if (harvest()) {
                            wait.quit();
                        }
                    });
        connect(&backstop, &QTimer::timeout, &wait, &QEventLoop::quit);
        if (!remaining.isEmpty()) {
            wait.exec();
        }
        disconnect(verdicts);
        // Anything the backstop gave up on counts as a failure.
        deliveryFailures += static_cast<int>(remaining.size());
        for (const QString& requestId : remaining) {
            m_deliveryOutcomes.remove(requestId);
        }
    }

    return {true, {}, dispatched, rejected, deliveryFailures, cycleDetected};
}

bool DaemonService::dispatchActionList(const QList<ActionNode>& actions,
                                        const QStringList& targets,
                                        const QStringList& callStack,
                                        const QString& currentRuleId, int& dispatched,
                                        int& rejected, int& deliveryFailures,
                                        QStringList& pendingRequests, bool& cycleDetected,
                                        const std::shared_ptr<std::atomic_bool>& cancelToken) {
    // The effective ancestry for cycle detection: every enclosing macro run
    // plus this rule itself. Naming any of them would never terminate.
    QStringList ancestry = callStack;
    ancestry.append(currentRuleId);

    // Action types whose execution lands in later milestones abort the whole
    // list up front, so a mixed list never half-executes (and an unknown type
    // can never be mistaken for a launch).
    static const QSet<QString> executableTypes{
        QStringLiteral("delay"),     QStringLiteral("keypress"),
        QStringLiteral("launchApp"), QStringLiteral("mute"),
        QStringLiteral("setVolume"), QStringLiteral("runRule"),
        QStringLiteral("ifThenElse")};
    for (const ActionNode& action : actions) {
        if (!executableTypes.contains(action.type)) {
            return false;
        }
    }

    auto enqueue = [&](const QString& serial, const QJsonObject& command) {
        const QString commandJson =
            QString::fromUtf8(QJsonDocument(command).toJson(QJsonDocument::Compact));
        // Same validation/safety gates as widget-issued SendCommand calls,
        // minus the D-Bus error replies a rejected step must not emit.
        const ValidatedCommand validated = validateCommand(serial, commandJson);
        if (!validated.errorCode.isEmpty()) {
            ++rejected;
            return;
        }
        QQueue<QueuedCommand>& queue = m_commandQueues[serial];
        if (queue.size() >= CommandContract::maximumOutstandingPerDevice) {
            ++rejected;
            return;
        }
        const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        queue.enqueue({requestId, serial, validated.arguments,
                       QDeadlineTimer(CommandContract::daemonCompletionDeadlineMs,
                                      Qt::PreciseTimer),
                       true});
        pendingRequests.append(requestId);
        ++dispatched;
        processNextCommand(serial);
    };
    // This PC control runs wpctl directly: the arguments are final, so no rds
    // schema validation applies — only the shared queue bounds do.
    auto enqueueProcess = [&](const QString& serial, const QString& program,
                              const QStringList& arguments) {
        QQueue<QueuedCommand>& queue = m_commandQueues[serial];
        if (queue.size() >= CommandContract::maximumOutstandingPerDevice) {
            ++rejected;
            return;
        }
        const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        queue.enqueue({requestId, serial, arguments,
                       QDeadlineTimer(CommandContract::daemonCompletionDeadlineMs,
                                      Qt::PreciseTimer),
                       true, program});
        pendingRequests.append(requestId);
        ++dispatched;
        processNextCommand(serial);
    };

    for (const ActionNode& action : actions) {
        // Cooperative cancellation checkpoint: once the owning run is
        // superseded, remaining actions are skipped without erroring — the
        // run's caller drops its record entirely.
        if (cancelToken && cancelToken->load()) {
            return true;
        }
        if (action.type == QStringLiteral("delay")) {
            // parseRule already bounds this to 1..600 seconds; sleeping here
            // keeps sequential actions ordered exactly as authored. Ticking
            // in 100 ms slices lets a superseded run bail out promptly.
            const QDeadlineTimer remaining(action.delaySeconds * 1000);
            while (!remaining.hasExpired()) {
                if (cancelToken && cancelToken->load()) {
                    return true;
                }
                QEventLoop pause;
                QTimer::singleShot(
                    static_cast<int>(std::min<qint64>(100, remaining.remainingTime())),
                    &pause, &QEventLoop::quit);
                pause.exec();
            }
            continue;
        }
        if (action.type == QStringLiteral("mute")) {
            // A conditional toggle against the latest observation: matching
            // state is a successful no-op, unknown state a rejected attempt.
            const bool wantMuted = action.mutedFlag != 0;
            for (const QString& serial : targets) {
                if (serial == QLatin1String(localPcSerial)) {
                    // Spec §4: absolute mute control via wpctl, gated on a
                    // fresh observation (stale health leaves nothing to trust).
                    const DeviceSnapshot snapshot = m_snapshots.value(serial);
                    if (!snapshot.muted.has_value()) {
                        ++rejected;
                        continue;
                    }
                    if (*snapshot.muted == wantMuted) {
                        continue;
                    }
                    enqueueProcess(
                        serial, QStringLiteral("wpctl"),
                        {QStringLiteral("set-mute"), QStringLiteral("@DEFAULT_AUDIO_SINK@"),
                         wantMuted ? QStringLiteral("1") : QStringLiteral("0")});
                    continue;
                }
                const DeviceSnapshot snapshot = m_snapshots.value(serial);
                if (!snapshot.muted.has_value()) {
                    ++rejected;
                    continue;
                }
                if (*snapshot.muted == wantMuted) {
                    continue;
                }
                QJsonObject command;
                command.insert(QStringLiteral("operation"), QStringLiteral("keypress"));
                command.insert(QStringLiteral("key"), QStringLiteral("VolumeMute"));
                enqueue(serial, command);
            }
            continue;
        }
        if (action.type == QStringLiteral("setVolume")) {
            // ECP offers no absolute volume: walk relative from the last
            // observed reading, bounded at 30 presses (~1 unit per press).
            // The per-device ceiling is an always-on policy: it clamps the
            // authored target before the delta math, so a walk can never aim
            // past it (spec §2).
            for (const QString& serial : targets) {
                if (serial == QLatin1String(localPcSerial)) {
                    // Spec §4: absolute set-volume via wpctl. The M5 ceiling
                    // clamps the authored target first; a stale/offline health
                    // means no trustworthy reading, so the attempt is rejected.
                    const auto device = findDevice(serial);
                    const DeviceSnapshot snapshot = m_snapshots.value(serial);
                    if (!device || !snapshot.volume.has_value()) {
                        ++rejected;
                        continue;
                    }
                    const int target = std::min(action.volume, device->volumeCeiling);
                    if (target == *snapshot.volume) {
                        continue;  // already there: a clean no-op
                    }
                    enqueueProcess(
                        serial, QStringLiteral("wpctl"),
                        {QStringLiteral("set-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@"),
                         QString::number(target / 100.0, 'f', 2)});
                    continue;
                }
                const auto device = findDevice(serial);
                const DeviceSnapshot snapshot = m_snapshots.value(serial);
                if (!device || !snapshot.volume.has_value()) {
                    ++rejected;
                    continue;
                }
                const int target = std::min(action.volume, device->volumeCeiling);
                const int delta = target - *snapshot.volume;
                if (delta == 0) {
                    continue;
                }
                QJsonObject command;
                command.insert(QStringLiteral("operation"), QStringLiteral("keypress"));
                command.insert(
                    QStringLiteral("key"),
                    delta > 0 ? QStringLiteral("VolumeUp") : QStringLiteral("VolumeDown"));
                const int presses = std::min(qAbs(delta), 30);
                for (int press = 0; press < presses; ++press) {
                    if (delta > 0) {
                        // Relative keys cannot be clamped by the protocol, so
                        // each upward nudge re-checks the latest observation
                        // and stops at the ceiling (best-effort, spec §2).
                        const DeviceSnapshot latest = m_snapshots.value(serial);
                        if (latest.volume.has_value() && *latest.volume >= device->volumeCeiling) {
                            break;
                        }
                    }
                    enqueue(serial, command);
                }
            }
            continue;
        }
        if (action.type == QStringLiteral("runRule")) {
            // Re-entering any rule on the ancestry (including this one) is a
            // cycle: only that branch aborts, as one failed attempt.
            if (ancestry.contains(action.ruleId)) {
                ++deliveryFailures;
                cycleDetected = true;
                continue;
            }
            // A dangling macro target is one rejected attempt, not an abort.
            const auto child = findRule(action.ruleId);
            if (child == m_rules.end() || !child->enabled) {
                ++rejected;
                continue;
            }
            // The child run inherits this run's token: superseding the root
            // stops the whole in-flight macro tree, and a cancelled child
            // records nothing (its root will not record either).
            const QString childOutcome =
                executeRuleRun(action.ruleId, ancestry, cancelToken);
            if (childOutcome != QStringLiteral("ok")) {
                ++deliveryFailures;
                cycleDetected = cycleDetected || childOutcome == QStringLiteral("cycle-error");
            }
            continue;
        }
        if (action.type == QStringLiteral("ifThenElse")) {
            // Branch lists re-enter this dispatcher, so the recursive call's
            // pre-scan still aborts on unsupported steps and an empty branch
            // is a clean no-op. callStack/currentRuleId pass through intact,
            // keeping macro cycle detection honest inside branches.
            const bool taken = conditionsMet(action.condition, m_snapshots, targets);
            if (!dispatchActionList(taken ? action.thenActions : action.elseActions,
                                    targets, callStack, currentRuleId, dispatched,
                                    rejected, deliveryFailures, pendingRequests,
                                    cycleDetected, cancelToken)) {
                return false;
            }
            continue;
        }
        QJsonObject command;
        if (action.type == QStringLiteral("keypress")) {
            command.insert(QStringLiteral("operation"), QStringLiteral("keypress"));
            command.insert(QStringLiteral("key"), action.key);
        } else {
            command.insert(QStringLiteral("operation"), QStringLiteral("launch"));
            command.insert(QStringLiteral("appId"), action.appId);
        }
        for (const QString& serial : targets) {
            if (serial == QLatin1String(localPcSerial)) {
                // There is nothing to press or launch on This PC; the attempt
                // counts as rejected rather than aborting sibling actions.
                ++rejected;
                continue;
            }
            enqueue(serial, command);
        }
    }
    return true;
}

DaemonService::ValidatedCommand DaemonService::validateCommand(const QString& serial,
                                                               const QString& commandJson) const {
    constexpr qsizetype maximumCommandBytes = 2048;
    const QByteArray encoded = commandJson.toUtf8();
    if (encoded.size() > maximumCommandBytes) {
        return {{}, QStringLiteral("commandTooLarge")};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(encoded, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {{}, QStringLiteral("invalidCommand")};
    }
    const QJsonObject object = document.object();
    const QStringList objectKeys = object.keys();
    const QString operation = object.value(QStringLiteral("operation")).toString();
    if (operation == QStringLiteral("keypress")) {
        const QSet<QString> expectedKeys{QStringLiteral("operation"), QStringLiteral("key")};
        if (QSet<QString>(objectKeys.cbegin(), objectKeys.cend()) != expectedKeys) {
            return {{}, QStringLiteral("invalidCommand")};
        }
        const QString key = object.value(QStringLiteral("key")).toString();
        static const QSet<QString> allowedKeys{
            QStringLiteral("Home"),      QStringLiteral("Back"), QStringLiteral("Up"),
            QStringLiteral("Down"),      QStringLiteral("Left"), QStringLiteral("Right"),
            QStringLiteral("Select"),    QStringLiteral("Play"), QStringLiteral("Pause"),
            QStringLiteral("Rev"),       QStringLiteral("Fwd"),  QStringLiteral("InstantReplay"),
            QStringLiteral("FindRemote"), QStringLiteral("Power"), QStringLiteral("PowerOn")};
        // Audio keys are only safe once a reading was observed: they would
        // otherwise act on a device state nobody has measured.
        static const QSet<QString> volumeGatedKeys{QStringLiteral("VolumeUp"),
                                                   QStringLiteral("VolumeDown"),
                                                   QStringLiteral("VolumeMute")};
        if (volumeGatedKeys.contains(key)) {
            const auto snapshotIt = m_snapshots.constFind(serial);
            if (snapshotIt == m_snapshots.cend() || !snapshotIt->volume.has_value()) {
                return {{}, QStringLiteral("safetyRejected")};
            }
            return {{QStringLiteral("keypress"), key}, {}};
        }
        if (key.isEmpty() || key.size() > 32 || key.startsWith(QChar(u'-')) ||
            !allowedKeys.contains(key)) {
            return {{}, QStringLiteral("unsupportedCommand")};
        }
        if (key == QStringLiteral("FindRemote")) {
            const auto snapshotIt = m_snapshots.constFind(serial);
            if (snapshotIt == m_snapshots.cend() ||
                !snapshotIt->capabilities.contains(QStringLiteral("supports-find-remote"))) {
                return {{}, QStringLiteral("unsupportedCapability")};
            }
        }
        return {{QStringLiteral("keypress"), key}, {}};
    }

    if (operation == QStringLiteral("launch")) {
        const QSet<QString> expectedKeys{QStringLiteral("operation"), QStringLiteral("appId")};
        if (QSet<QString>(objectKeys.cbegin(), objectKeys.cend()) != expectedKeys) {
            return {{}, QStringLiteral("invalidCommand")};
        }
        const QString appId = object.value(QStringLiteral("appId")).toString();
        static const QRegularExpression safeAppId(
            QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"));
        if (!safeAppId.match(appId).hasMatch()) {
            return {{}, QStringLiteral("unsupportedCommand")};
        }
        return {{QStringLiteral("launch"), appId}, {}};
    }

    return {{}, QStringLiteral("unsupportedOperation")};
}

QString DaemonService::SendCommand(const QString& serial, const QString& commandJson) {
    const auto device = findDevice(serial);
    if (!device) {
        if (calledFromDBus()) {
            sendErrorReply(QString::fromLatin1(DbusError::unknownDevice),
                           QStringLiteral("unknown Roku device serial"));
        }
        return {};
    }

    const ValidatedCommand validated = validateCommand(serial, commandJson);
    if (!validated.errorCode.isEmpty()) {
        if (calledFromDBus()) {
            sendErrorReply(commandErrorName(validated.errorCode), validated.errorCode);
        }
        return {};
    }

    QQueue<QueuedCommand>& queue = m_commandQueues[serial];
    if (queue.size() >= CommandContract::maximumOutstandingPerDevice) {
        if (calledFromDBus()) {
            sendErrorReply(QString::fromLatin1(DbusError::transportFailure),
                           QStringLiteral("the per-device command queue is full"));
        }
        return {};
    }

    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    queue.enqueue({requestId, serial, validated.arguments,
                   QDeadlineTimer(CommandContract::daemonCompletionDeadlineMs, Qt::PreciseTimer)});
    updateIdleTimer();
    processNextCommand(serial);
    return requestId;
}

void DaemonService::processNextCommand(const QString& serial) {
    auto queueIt = m_commandQueues.find(serial);
    if (m_shuttingDown || m_activeCommandSerials.contains(serial) ||
        queueIt == m_commandQueues.end() || queueIt->isEmpty()) {
        return;
    }
    while (!queueIt->isEmpty() && m_deadlineRemaining(queueIt->head().deadline) <= 0) {
        const QueuedCommand expired = queueIt->dequeue();
        finishCommandWithError(expired.requestId, serial, QStringLiteral("timeout"),
                               expired.trackDelivery);
    }
    if (queueIt->isEmpty()) {
        m_commandQueues.erase(queueIt);
        updateIdleTimer();
        return;
    }
    const auto device = findDevice(serial);
    if (!device) {
        const QueuedCommand command = queueIt->dequeue();
        if (queueIt->isEmpty()) {
            m_commandQueues.erase(queueIt);
        }
        finishCommandWithError(command.requestId, serial, QStringLiteral("unknownDevice"),
                               command.trackDelivery);
        updateIdleTimer();
        processNextCommand(serial);
        return;
    }

    const QueuedCommand command = queueIt->head();
    ProcessRequest request;
    if (command.program == QLatin1String("wpctl")) {
        // Absolute local-audio control runs wpctl directly, without any of
        // the rds-specific flags or address routing.
        request.program = command.program;
        request.arguments = command.arguments;
    } else {
        request.program = QStringLiteral("rds");
        request.arguments = {QStringLiteral("--json"),    QStringLiteral("--quiet"),
                             QStringLiteral("--timeout"), QStringLiteral("3000"),
                             QStringLiteral("--ip"),      device->address};
        request.arguments.append(command.arguments);
    }
    const qint64 remainingMs = m_deadlineRemaining(command.deadline);
    if (remainingMs <= 0) {
        queueIt->dequeue();
        if (queueIt->isEmpty()) {
            m_commandQueues.erase(queueIt);
        }
        finishCommandWithError(command.requestId, serial, QStringLiteral("timeout"),
                               command.trackDelivery);
        updateIdleTimer();
        processNextCommand(serial);
        return;
    }
    request.timeout = std::chrono::milliseconds(
        std::min<qint64>(CommandContract::executionTimeoutMs, remainingMs));
    m_activeCommandSerials.insert(serial);

    QPointer<DaemonService> service(this);
    m_processRunner->start(request, [service, serial, command](ProcessResult result) {
        if (!service || service->m_shuttingDown) {
            return;
        }
        auto activeQueueIt = service->m_commandQueues.find(serial);
        if (activeQueueIt != service->m_commandQueues.end() && !activeQueueIt->isEmpty()) {
            activeQueueIt->dequeue();
            if (activeQueueIt->isEmpty()) {
                service->m_commandQueues.erase(activeQueueIt);
            }
        }
        service->m_activeCommandSerials.remove(serial);
        service->finishCommand(command, result);
        service->processNextCommand(serial);
        service->updateIdleTimer();
    });
}

void DaemonService::finishCommand(const QueuedCommand& command, const ProcessResult& result) {
    const bool succeeded = !result.timedOut && result.exitCode == 0;
    if (command.trackDelivery) {
        m_deliveryOutcomes.insert(command.requestId, succeeded);
    }
    QJsonObject payload{{QStringLiteral("schemaVersion"), 1},
                        {QStringLiteral("requestId"), command.requestId},
                        {QStringLiteral("serial"), command.serial},
                        {QStringLiteral("success"), succeeded}};
    if (!succeeded) {
        payload.insert(QStringLiteral("error"), result.timedOut
                                                    ? QStringLiteral("timeout")
                                                    : QStringLiteral("transportFailure"));
    }
    Q_EMIT CommandFinished(command.requestId, compactJson(payload));
}

void DaemonService::finishCommandWithError(const QString& requestId, const QString& serial,
                                           const QString& errorCode, bool trackDelivery) {
    if (trackDelivery) {
        m_deliveryOutcomes.insert(requestId, false);
    }
    Q_EMIT CommandFinished(requestId,
                           compactJson(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                                                   {QStringLiteral("requestId"), requestId},
                                                   {QStringLiteral("serial"), serial},
                                                   {QStringLiteral("success"), false},
                                                   {QStringLiteral("error"), errorCode}}));
}

std::optional<DeviceRecord> DaemonService::findDevice(const QString& serial) const {
    const QList<DeviceRecord> devices = m_deviceRegistry.devices();
    const auto it = std::ranges::find(devices, serial, &DeviceRecord::serial);
    if (it == devices.cend()) {
        return std::nullopt;
    }
    return *it;
}

void DaemonService::pollDueDevices() {
    const QList<PollClaim> claims = m_pollScheduler.claimDuePolls();
    for (const PollClaim& claim : claims) {
        const auto device = findDevice(claim.serial);
        if (!device) {
            static_cast<void>(m_pollScheduler.recordResult(claim, false));
            continue;
        }
        const QString address = device->address;
        const quint64 addressGeneration = m_addressGenerations.value(claim.serial);
        QPointer<DaemonService> service(this);
        m_rdsClient.queryDevice(
            address, [service, claim, address, addressGeneration](RdsQueryResult result) {
                if (!service || service->m_shuttingDown) {
                    return;
                }
                const auto currentDevice = service->findDevice(claim.serial);
                if (!currentDevice || currentDevice->address != address ||
                    service->m_addressGenerations.value(claim.serial) != addressGeneration) {
                    static_cast<void>(service->m_pollScheduler.recordResult(claim, true));
                    return;
                }
                if (!result) {
                    static_cast<void>(service->m_pollScheduler.recordResult(claim, false));
                    if (result.error().code == RdsError::Code::IncompatibleCli ||
                        result.error().code == RdsError::Code::DependencyUnavailable) {
                        service->m_pollingEnabled = false;
                        service->m_pollingDiagnostic = result.error().message;
                        service->m_pollTimer.stop();
                    }
                    return;
                }
                if (result.value().serial != claim.serial) {
                    static_cast<void>(service->m_pollScheduler.recordResult(claim, false));
                    return;
                }

                DeviceSnapshot snapshot = result.value();
                snapshot.serial = claim.serial;
                snapshot.address = address;
                snapshot.capturedAt = QDateTime::currentDateTimeUtc();
                if (snapshot.displayName.isEmpty()) {
                    snapshot.displayName = currentDevice->displayName;
                }
                const bool deviceChanged =
                    service->mergeDevice({claim.serial, snapshot.displayName, address});
                const auto mergedDevice = service->findDevice(claim.serial);
                if (mergedDevice) {
                    // The registry is authoritative once user preferences have
                    // been merged in; serving the advertised name from the
                    // snapshot would undo a rename everywhere it is displayed.
                    snapshot.displayName = mergedDevice->displayName;
                    snapshot.address = mergedDevice->address;
                }
                // The pre-insert snapshot is the diff baseline; a serial never
                // seen before only establishes it and must not fire (§4.1).
                const bool hadPrevious = service->m_snapshots.contains(claim.serial);
                const DeviceSnapshot previous = service->m_snapshots.value(claim.serial);
                service->m_snapshots.insert(claim.serial, snapshot);
                if (deviceChanged) {
                    service->m_configStore->saveDevices(service->m_deviceRegistry.devices());
                    Q_EMIT service->DeviceListChanged(service->ListDevices());
                }
                static_cast<void>(service->m_pollScheduler.recordResult(claim, true));
                Q_EMIT service->DeviceSnapshotChanged(claim.serial, snapshotJson(snapshot));
                if (hadPrevious) {
                    service->runStateChangeFirings(
                        dueStateChangeRuleIds(service->m_rules, claim.serial, previous, snapshot));
                }
            });
    }
}

void DaemonService::mprisPlayerAppeared(const QString& service) {
    if (!service.startsWith(QLatin1String("org.mpris.MediaPlayer2."))) {
        return;
    }
    m_mprisPlayers.insert(service);
    queryMprisStatus(service);
}

void DaemonService::queryMprisStatus(const QString& service) {
    QDBusMessage request =
        QDBusMessage::createMethodCall(service, QStringLiteral("/org/mpris/MediaPlayer2"),
                                       QStringLiteral("org.freedesktop.DBus.Properties"),
                                       QStringLiteral("Get"));
    request.setArguments({QStringLiteral("org.mpris.MediaPlayer2.Player"),
                          QStringLiteral("PlaybackStatus")});
    QPointer<DaemonService> self(this);
    auto* pending = new QDBusPendingCallWatcher(m_connection.asyncCall(request), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this,
            [self, service](QDBusPendingCallWatcher* finished) {
                finished->deleteLater();
                if (!self || self->m_shuttingDown || !self->m_mprisPlayers.contains(service)) {
                    return;  // daemon gone, or the player vanished meanwhile
                }
                const QDBusMessage reply = finished->reply();
                if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
                    self->storeMprisStatus(service, reply.arguments().front());
                } else {
                    self->m_mprisPlaybackStatuses.remove(service);
                    self->recomputeMprisPlaying();
                }
            });
}

void DaemonService::mprisPlayerVanished(const QString& service) {
    m_mprisPlayers.remove(service);
    m_mprisPlaybackStatuses.remove(service);
    recomputeMprisPlaying();
}

void DaemonService::mprisNameOwnerChanged(const QString& name, const QString& oldOwner,
                                          const QString& newOwner) {
    if (!name.startsWith(QLatin1String("org.mpris.MediaPlayer2."))) {
        return;
    }
    if (newOwner.isEmpty()) {
        if (!oldOwner.isEmpty()) {
            mprisPlayerVanished(name);
        }
        return;
    }
    // A fresh registration or an owner handover: either way refresh its status.
    mprisPlayerAppeared(name);
}

void DaemonService::mprisPropertiesChanged(const QString& interfaceName,
                                           const QVariantMap& /*changedProperties*/,
                                           const QStringList& /*invalidatedProperties*/) {
    // The sender identity of an unsolicited signal is not tracked here; any
    // Player-interface flip triggers a cheap re-query of every tracked player
    // so `playing` converges on the truth (spec §2.2).
    if (interfaceName != QLatin1String("org.mpris.MediaPlayer2.Player")) {
        return;
    }
    const QList<QString> players = m_mprisPlayers.values();
    for (const QString& service : players) {
        queryMprisStatus(service);
    }
}

void DaemonService::recomputeMprisPlaying() {
    bool anyPlaying = false;
    for (const QString& status : std::as_const(m_mprisPlaybackStatuses)) {
        if (status == QLatin1String("Playing")) {
            anyPlaying = true;
            break;
        }
    }
    m_mprisPlaying = anyPlaying;
}

// Decodes a PlaybackStatus property reply and refreshes the merged flag.
void DaemonService::storeMprisStatus(const QString& service, const QVariant& value) {
    const QString status = value.value<QDBusVariant>().variant().toString();
    if (status.isEmpty()) {
        m_mprisPlaybackStatuses.remove(service);
    } else {
        m_mprisPlaybackStatuses.insert(service, status);
    }
    recomputeMprisPlaying();
}

void DaemonService::pollLocalPcAudio() {
    if (m_pcPollInFlight || !findDevice(QLatin1String(localPcSerial))) {
        return;
    }
    ProcessRequest request;
    request.program = QStringLiteral("wpctl");
    request.arguments = {QStringLiteral("get-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@")};
    request.timeout = std::chrono::milliseconds(CommandContract::executionTimeoutMs);
    m_pcPollInFlight = true;
    QPointer<DaemonService> service(this);
    m_processRunner->start(request, [service](ProcessResult result) {
        if (!service || service->m_shuttingDown) {
            return;
        }
        service->finishLocalPcPoll(result);
    });
}

void DaemonService::finishLocalPcPoll(const ProcessResult& result) {
    m_pcPollInFlight = false;
    const bool ranCleanly = !result.timedOut && result.exitCode == 0;
    const auto parsed =
        ranCleanly ? parseWpctlVolume(result.standardOutput) : std::nullopt;
    if (!parsed) {
        ++m_pcPollFailures;
        // Spec §2.1: three consecutive failures flip health to Offline; the
        // interim failures leave the last good reading on display.
        if (m_pcPollFailures == 3) {
            DeviceSnapshot offline;
            offline.serial = QLatin1String(localPcSerial);
            if (const auto device = findDevice(offline.serial)) {
                offline.displayName = device->displayName;
                offline.address = device->address;
            }
            offline.power = PowerState::On;
            offline.health = DeviceHealth::Offline;
            applyLocalPcSnapshot(std::move(offline));
        }
        return;
    }

    const auto device = findDevice(QLatin1String(localPcSerial));
    if (!device) {
        return;
    }
    m_pcPollFailures = 0;
    DeviceSnapshot snapshot;
    snapshot.serial = QLatin1String(localPcSerial);
    snapshot.displayName = device->displayName;
    snapshot.address = device->address;
    snapshot.power = PowerState::On;
    snapshot.health = DeviceHealth::Online;
    snapshot.volume = parsed->first;
    snapshot.muted = parsed->second;
    applyLocalPcSnapshot(std::move(snapshot));
}

void DaemonService::applyLocalPcSnapshot(DeviceSnapshot snapshot) {
    // The synthetic playing flag rides every This PC snapshot (spec §2.2).
    if (snapshot.serial == QLatin1String(localPcSerial)) {
        snapshot.playing = m_mprisPlaying;
    }
    snapshot.capturedAt = QDateTime::currentDateTimeUtc();
    // The pre-insert snapshot is the diff baseline; a serial never seen before
    // only establishes it and must not fire (§4.1) — identical to rds polls.
    const bool hadPrevious = m_snapshots.contains(snapshot.serial);
    const DeviceSnapshot previous = m_snapshots.value(snapshot.serial);
    m_snapshots.insert(snapshot.serial, snapshot);
    Q_EMIT DeviceSnapshotChanged(snapshot.serial, snapshotJson(snapshot));
    if (hadPrevious) {
        // Opt-in targeting (spec §1): only enabled rules that explicitly list
        // This PC may fire from its edges; Roku edges keep the full list.
        QList<RuleDocument> pcRules;
        for (const RuleDocument& rule : std::as_const(m_rules)) {
            if (rule.enabled && rule.devices.contains(QLatin1String(localPcSerial))) {
                pcRules.append(rule);
            }
        }
        runStateChangeFirings(dueStateChangeRuleIds(pcRules, snapshot.serial, previous, snapshot));
    }
}

void DaemonService::removeClientsForOwner(const QString& owner) {
    const QList<ClientRegistration> registrations = m_clients.takeOwner(owner);
    for (const ClientRegistration& registration : registrations) {
        removePollInterests(registration);
    }
    m_ownerWatcher.removeWatchedService(owner);
    updateIdleTimer();
}

void DaemonService::automationEnablementChanged() {
    updateIdleTimer();
}

void DaemonService::updateIdleTimer() {
    if (m_clients.isEmpty() && !m_hasEnabledAutomations() && !hasAcceptedWork()) {
        m_idleTimer.start();
    } else {
        m_idleTimer.stop();
    }
}

bool DaemonService::hasAcceptedWork() const {
    return m_discoveryActive || !m_deviceTestReplies.isEmpty() ||
           !m_activeCommandSerials.isEmpty() || !m_commandQueues.isEmpty() ||
           m_scheduleTimer.isActive() || hasEnabledLocalPcRules();
}

// Opt-in idle parity: an enabled rule that explicitly names This PC keeps the
// daemon alive exactly like a scheduled rule does, because PC state changes
// may fire it at any moment. Wildcards never reach This PC.
bool DaemonService::hasEnabledLocalPcRules() const {
    for (const RuleDocument& rule : m_rules) {
        // Explicit listing only: wildcards never reach This PC.
        if (rule.enabled && rule.devices.contains(QLatin1String(localPcSerial))) {
            return true;
        }
    }
    return false;
}

void DaemonService::handleIdleTimeout() {
    if (!m_clients.isEmpty() || m_hasEnabledAutomations() || hasAcceptedWork()) {
        updateIdleTimer();
        return;
    }
    Q_EMIT idleExitRequested();
}

quint32 DaemonService::effectiveInterval(const QString& serial) const {
    if (!m_pollScheduler.nextDueFor(serial)) {
        return 0;
    }
    const auto interval = m_pollScheduler.intervalFor(serial).count();
    return static_cast<quint32>(std::clamp<std::chrono::milliseconds::rep>(
        interval, 0, std::numeric_limits<quint32>::max()));
}

bool DaemonService::idleTimerActive() const {
    return m_idleTimer.isActive();
}

int DaemonService::idleGracePeriodMs() const {
    return m_idleTimer.interval();
}

qsizetype DaemonService::watchedClientOwnerCount() const {
    return m_ownerWatcher.watchedServices().size();
}

QString DaemonService::lastRegistrationError() const {
    return m_lastRegistrationError;
}

bool DaemonService::pollingEnabled() const {
    return m_pollingEnabled;
}

QString DaemonService::pollingDiagnostic() const {
    return m_pollingDiagnostic;
}

} // namespace RokuDeck
