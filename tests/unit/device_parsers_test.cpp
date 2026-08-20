// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/active_app_parser.h"
#include "core/audio_device_parser.h"
#include "core/device_info_parser.h"
#include "core/device_state.h"
#include "core/media_player_parser.h"

#include <QFile>
#include <QtTest>

using namespace RokuDeck;
using namespace Qt::Literals::StringLiterals;

class DeviceParsersTest final : public QObject {
    Q_OBJECT

private:
    [[nodiscard]] static QByteArray loadFixture(const QString &name)
    {
        QFile file(QFINDTESTDATA(u"../fixtures/"_s + name));
        if (!file.open(QIODevice::ReadOnly)) {
            qFatal("Unable to open fixture: %s", qPrintable(name));
        }
        return file.readAll();
    }

private Q_SLOTS:
    void parsesPowerOnDeviceIdentity();
    void readyInvalidatesDisplayState();
    void screensaverCountsAsPowerOn();
    void parsesAudioVolumeAndMute();
    void parsesMediaPlaybackStates();
    void rejectsMalformedXml();
};

void DeviceParsersTest::parsesPowerOnDeviceIdentity()
{
    const auto result = parseDeviceInfo(loadFixture(u"device-info-power-on.xml"_s));

    QVERIFY(result.has_value());
    const DeviceSnapshot device = result.value();
    QCOMPARE(device.serial, u"TEST-SERIAL-0001"_s);
    QCOMPARE(device.displayName, u"Fixture Display"_s);
    QCOMPARE(device.power, PowerState::On);
    QVERIFY(device.capabilities.contains(u"supports-find-remote"_s));
}

void DeviceParsersTest::readyInvalidatesDisplayState()
{
    DeviceSnapshot snapshot;
    snapshot.power = parseDeviceInfo(loadFixture(u"device-info-ready.xml"_s)).value().power;
    snapshot.activeApp = parseActiveApp(loadFixture(u"active-app-home.xml"_s)).value();
    snapshot.screensaver = TriState::Inactive;
    snapshot.media = MediaPlaybackState::Playing;
    snapshot.volume = 17;
    snapshot.muted = false;
    snapshot.normalize();

    QCOMPARE(snapshot.power, PowerState::Standby);
    QCOMPARE(snapshot.screensaver, TriState::Unknown);
    QCOMPARE(snapshot.media, MediaPlaybackState::Unknown);
    QVERIFY(!snapshot.activeApp.has_value());
    QVERIFY(!snapshot.volume.has_value());
    QVERIFY(!snapshot.muted.has_value());
}

void DeviceParsersTest::screensaverCountsAsPowerOn()
{
    const auto device = parseDeviceInfo(loadFixture(u"device-info-power-on.xml"_s)).value();
    const auto app = parseActiveApp(loadFixture(u"active-app-screensaver.xml"_s)).value();

    QCOMPARE(device.power, PowerState::On);
    QCOMPARE(app.screensaver, TriState::Active);
    QCOMPARE(app.type, u"screensaver"_s);
}

void DeviceParsersTest::parsesAudioVolumeAndMute()
{
    const auto result = parseAudioDevice(loadFixture(u"audio-device.xml"_s));

    QVERIFY(result.has_value());
    QCOMPARE(result.value().volume, std::optional<int>{17});
    QCOMPARE(result.value().muted, std::optional<bool>{true});
}

void DeviceParsersTest::parsesMediaPlaybackStates()
{
    const auto playing = parseMediaPlayer(loadFixture(u"media-player-playing.xml"_s));
    const auto idle = parseMediaPlayer(loadFixture(u"media-player-idle.xml"_s));

    QVERIFY(playing.has_value());
    QVERIFY(idle.has_value());
    QCOMPARE(playing.value(), MediaPlaybackState::Playing);
    QCOMPARE(idle.value(), MediaPlaybackState::Idle);
}

void DeviceParsersTest::rejectsMalformedXml()
{
    const auto result = parseActiveApp(QByteArrayLiteral("<active-app><app id=\"home\">Home</active-app>"));

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, ParseError::Code::MalformedXml);
}

QTEST_GUILESS_MAIN(DeviceParsersTest)
#include "device_parsers_test.moc"
