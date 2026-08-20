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
    void parsesHomeFieldsAndDecodesEntities();
    void readyInvalidatesDisplayState();
    void screensaverCountsAsPowerOn();
    void parsesAudioVolumeAndMute();
    void audioParserHandlesFalseAbsentAndInvalidValues();
    void parsesMediaPlaybackStates();
    void rejectsMissingStableDeviceIdentity();
    void rejectsMalformedXmlAfterRecognizedElements();
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
    QVERIFY(!device.capabilities.contains(u"supports-audio-guide"_s));
    QVERIFY(!device.capabilities.contains(u"supports-fixture-mode"_s));
}

void DeviceParsersTest::parsesHomeFieldsAndDecodesEntities()
{
    const auto result = parseActiveApp(loadFixture(u"active-app-home.xml"_s));

    QVERIFY(result.has_value());
    QCOMPARE(result.value().id, u"home"_s);
    QCOMPARE(result.value().name, u"Fixture & Home"_s);
    QCOMPARE(result.value().type, u"home"_s);
    QCOMPARE(result.value().input, u"fixture-input"_s);
    QCOMPARE(result.value().screensaver, TriState::Inactive);
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
    const auto result = parseActiveApp(loadFixture(u"active-app-screensaver.xml"_s));

    QVERIFY(result.has_value());
    QCOMPARE(device.power, PowerState::On);
    QCOMPARE(result.value().screensaver, TriState::Active);
    QCOMPARE(result.value().type, u"screensaver"_s);
    QCOMPARE(result.value().id, u"fixture-screensaver"_s);
}

void DeviceParsersTest::parsesAudioVolumeAndMute()
{
    const auto result = parseAudioDevice(loadFixture(u"audio-device.xml"_s));

    QVERIFY(result.has_value());
    QCOMPARE(result.value().volume, std::optional<int>{17});
    QCOMPARE(result.value().muted, std::optional<bool>{true});
}

void DeviceParsersTest::audioParserHandlesFalseAbsentAndInvalidValues()
{
    const auto falseMute = parseAudioDevice(loadFixture(u"audio-device-muted-false.xml"_s));
    const auto empty = parseAudioDevice(loadFixture(u"audio-device-empty.xml"_s));
    const auto invalid = parseAudioDevice(loadFixture(u"audio-device-invalid-volume.xml"_s));

    QVERIFY(falseMute.has_value());
    QCOMPARE(falseMute.value().muted, std::optional<bool>{false});
    QVERIFY(!empty.value().volume.has_value());
    QVERIFY(!empty.value().muted.has_value());
    QVERIFY(!invalid.has_value());
    QCOMPARE(invalid.error().code, ParseError::Code::InvalidValue);
}

void DeviceParsersTest::parsesMediaPlaybackStates()
{
    const auto playing = parseMediaPlayer(loadFixture(u"media-player-playing.xml"_s));
    const auto idle = parseMediaPlayer(loadFixture(u"media-player-idle.xml"_s));
    const auto paused = parseMediaPlayer(loadFixture(u"media-player-paused.xml"_s));
    const auto stopped = parseMediaPlayer(loadFixture(u"media-player-stopped.xml"_s));
    const auto unknown = parseMediaPlayer(loadFixture(u"media-player-unknown.xml"_s));

    QVERIFY(playing.has_value());
    QVERIFY(idle.has_value());
    QVERIFY(paused.has_value());
    QVERIFY(stopped.has_value());
    QVERIFY(unknown.has_value());
    QCOMPARE(playing.value(), MediaPlaybackState::Playing);
    QCOMPARE(idle.value(), MediaPlaybackState::Idle);
    QCOMPARE(paused.value(), MediaPlaybackState::Paused);
    QCOMPARE(stopped.value(), MediaPlaybackState::Stopped);
    QCOMPARE(unknown.value(), MediaPlaybackState::Unknown);
}

void DeviceParsersTest::rejectsMissingStableDeviceIdentity()
{
    const auto result = parseDeviceInfo(QByteArrayLiteral("<device-info/>"));

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, ParseError::Code::MissingRequiredElement);
}

void DeviceParsersTest::rejectsMalformedXmlAfterRecognizedElements()
{
    const auto app = parseActiveApp(loadFixture(u"active-app-malformed-trailing.xml"_s));
    const auto media = parseMediaPlayer(loadFixture(u"media-player-malformed-trailing.xml"_s));

    QVERIFY(!app.has_value());
    QCOMPARE(app.error().code, ParseError::Code::MalformedXml);
    QVERIFY(!media.has_value());
    QCOMPARE(media.error().code, ParseError::Code::MalformedXml);
}

QTEST_GUILESS_MAIN(DeviceParsersTest)
#include "device_parsers_test.moc"
