#include "robotservice.h"

#include "utilities.h"

#include <QLoggingCategory>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyController>
#include <QRegularExpression>
#include <QTimer>

#include <chrono>
#include <memory>

namespace EvoBot {

using namespace std::chrono_literals;

namespace {
Q_LOGGING_CATEGORY(lcRobotService, "evobot.robotservice", QtInfoMsg)

const QBluetoothUuid s_serviceUuid{quint16{0xfff3}};
const QBluetoothUuid s_notifyUuid{quint16{0xfff4}};
const QBluetoothUuid s_writeUuid{quint16{0xfff5}};

const auto s_pauseMessage = QByteArrayLiteral("X\x11\x40\x40\x00\x00");
const auto s_transmitterInterval = 100ms;

} // namespace

class RobotService::Private
{
    struct MessageFragment
    {
        constexpr MessageFragment() noexcept = default;
        constexpr MessageFragment(int offset, char value) noexcept
            : offset{offset}, value{value}
        {}

        constexpr explicit operator bool() const noexcept { return offset >= 0 && offset < 6; }

        int offset = -1;
        char value = 0;
    };

public:
    explicit Private(RobotService *q);

    bool attach(QLowEnergyController *central);

    void setCurrentMessage(int offset, char value);
    void setCurrentMessage(const QByteArray &message);
    QByteArray currentMessage() const { return m_message; }
    int currentSound() const { return m_currentSound; }
    State state() const;

    bool startAction(char action, int index);
    bool stopAction(char action, int index);

private:
    std::unique_ptr<QLowEnergyService> createService(QLowEnergyController *central, const QBluetoothUuid &serviceUuid);
    MessageFragment fragmentForAction(char action, int index) const;
    void transmitMessage();
    void checkState();

    bool readFirmwareVersion();
    bool startNotification();
    bool startTransmission();

    void onServiceStateChanged(QLowEnergyService *service, QLowEnergyService::ServiceState newState);
    void onCharacteristicChanged(const QLowEnergyCharacteristic &info, const QByteArray &value);
    void onCharacteristicWritten(const QLowEnergyCharacteristic &info, const QByteArray &value);

    void onEmitterTimeout();

    RobotService *const q;

    State m_oldState = DisconnectedState;
    QLowEnergyService *m_deviceInformation = {};
    QLowEnergyService *m_robotControl = {};

    QLowEnergyCharacteristic m_writeCharacteristic;
    int m_firmwareRevision = -1;
    int m_currentSound = 0;

    int m_pendingMessageCount = 0;
    QByteArray m_message = s_pauseMessage;
    bool m_audioLoop = false;
    QTimer m_emitter;
};

RobotService::RobotService(QObject *parent)
    : QObject{parent}
    , d{new Private{this}}
{}

RobotService::~RobotService()
{
    delete d;
}

bool RobotService::attach(QLowEnergyController *central)
{
    return d->attach(central);
}

void RobotService::setCurrentMessage(const QList<int> &message)
{
    QByteArray chars;
    chars.reserve(message.size());
    std::copy(message.begin(), message.end(), std::back_inserter(chars));
    d->setCurrentMessage(chars);
}

QList<int> RobotService::currentMessage() const
{
    const auto &currentMessage = d->currentMessage();

    QList<int> numbers;
    numbers.reserve(currentMessage.size());
    std::copy(currentMessage.begin(), currentMessage.end(), std::back_inserter(numbers));
    return numbers;
}

int RobotService::currentSound() const
{
    return d->currentSound();
}

RobotService::State RobotService::state() const
{
    return d->state();
}

bool RobotService::startAction(QChar action, int index)
{
    return d->startAction(action.toLatin1(), index);
}

bool RobotService::stopAction(QChar action, int index)
{
    return d->stopAction(action.toLatin1(), index);
}

bool RobotService::playSound(int index)
{
    return startAction('V', index);
}

bool RobotService::playLoop(int index)
{
    return startAction('M', index);
}

RobotService::Private::Private(RobotService *q)
    : q{q}
{
    connect(&m_emitter, &QTimer::timeout, q, [this] { onEmitterTimeout(); });
}

bool RobotService::Private::attach(QLowEnergyController *central)
{
    if (state() == ConnectedState) {
        qCWarning(lcRobotService, "Connected already");
        return false;
    }

    auto deviceInformation = createService(central, QBluetoothUuid::DeviceInformation);
    auto robotControl = createService(central, s_serviceUuid);

    if (deviceInformation && robotControl) {
        m_deviceInformation = deviceInformation.release();
        m_robotControl = robotControl.release();
        checkState();
        return true;
    }

    qCWarning(lcRobotService, "Could not resolve required services");
    return false;
}

bool RobotService::Private::readFirmwareVersion()
{
    if (m_deviceInformation) {
        const auto revisionCharacteristic = m_deviceInformation->characteristic(QBluetoothUuid::FirmwareRevisionString);

        if (revisionCharacteristic.isValid()) {
            const auto value = revisionCharacteristic.value();

            if (value == "Ver2.0") {
                m_firmwareRevision = 2;
                return true;
            }

            if (value == "Ver1.0") {
                m_firmwareRevision = 1;
                return true;
            }
        }
    }

    qCWarning(lcRobotService, "Could not identify firmware revision");
    return false;
}

void RobotService::Private::setCurrentMessage(int offset, char value)
{
    if (offset >= 0 && offset < m_message.size() && m_message[offset] != value) {
        m_message[offset] = value;
        emit q->currentMessageChanged(m_message);
        transmitMessage();
    }
}

void RobotService::Private::setCurrentMessage(const QByteArray &message)
{
    if (message != m_message && message.size() == m_message.size()) {
        m_message = message;
        emit q->currentMessageChanged(m_message);
        transmitMessage();
    }
}

RobotService::State RobotService::Private::state() const
{
    if (m_writeCharacteristic.isValid())
        return ConnectedState;
    if (m_robotControl && m_deviceInformation)
        return ConnectingState;

    return DisconnectedState;
}

RobotService::Private::MessageFragment RobotService::Private::fragmentForAction(char action, int index) const
{
    switch (action) {
    case 'F': return {1, static_cast<char>(qBound<char>(0, index, 3) + 1)};
    case 'B': return {1, static_cast<char>(qBound<char>(0, index, 3) + 5)};
    case 'L': return {1, static_cast<char>(qBound<char>(0, index, 3) + 9)};
    case 'R': return {1, static_cast<char>(qBound<char>(0, index, 3) + 13)};
    case 'O': return {2, 0x3c};
    case 'C': return {2, 0x3d};
    case 'U': return {3, 0x3e};
    case 'D': return {3, 0x3f};
    case 'M':
    case 'V': return {4, static_cast<char>(qMax(0, index) + 21)};
    case 'E': return {5, static_cast<char>(index ? qBound(1, index, 63) + (m_firmwareRevision == 1 ? 0x35 : 0x47) : 0x3b)};
    }

    return {};
}

void RobotService::Private::transmitMessage()
{
    if (m_robotControl) {
        ++m_pendingMessageCount;
        m_robotControl->writeCharacteristic(m_writeCharacteristic, m_message);
    }
}

void RobotService::Private::checkState()
{
    const auto newState = state();

    if (newState != m_oldState) {
        qCInfo(lcRobotService, "state changed: %s => %s", key(m_oldState), key(newState));
        emit q->stateChanged(newState, m_oldState);
        m_oldState = newState;
    }
}

bool RobotService::Private::startAction(char action, int index)
{
    if (action == 'S') {
        qCInfo(lcRobotService, "Pausing the robot");
        setCurrentMessage(s_pauseMessage);
        return true;
    }

    if (const auto fragment = fragmentForAction(action, index)) {
        qCInfo(lcRobotService, "Starting %c action (index=%d)", action, index);

        if (fragment.offset == 4)
            m_audioLoop = (action == 'M');

        setCurrentMessage(fragment.offset, fragment.value);
        return true;
    }

    qCWarning(lcRobotService, "Could not start unknown action %c (index=%d)", action, index);
    return false;
}

bool RobotService::Private::stopAction(char action, int index)
{
    if (const auto fragment = fragmentForAction(action, index)) {
        if (fragment.value == m_message[fragment.offset]) {
            qCInfo(lcRobotService, "Stopping %c action (index=%d)", action, index);
            setCurrentMessage(fragment.offset, s_pauseMessage[fragment.offset]);
            return true;
        }

        qCWarning(lcRobotService, "Could not stop inactive action %c (index=%d)", action, index);
        return false;
    }

    qCWarning(lcRobotService, "Could not stop unknown action %c (index=%d)", action, index);
    return false;
}

std::unique_ptr<QLowEnergyService> RobotService::Private::createService(QLowEnergyController *central,
                                                                        const QBluetoothUuid &serviceUuid)
{
    std::unique_ptr<QLowEnergyService> service{central->createServiceObject(serviceUuid, q)};

    connect(service.get(), &QLowEnergyService::stateChanged, q, [this, service = service.get()](auto newState) {
        this->onServiceStateChanged(service, newState);
    });

    onServiceStateChanged(service.get(), service->state());

    return service;
}

bool RobotService::Private::startNotification()
{
    if (m_robotControl) {
        const auto characteristic = m_robotControl->characteristic(s_notifyUuid);
        auto descriptor = characteristic.descriptor(QBluetoothUuid::ClientCharacteristicConfiguration);

        if (characteristic.isValid() && descriptor.isValid()) {
            m_robotControl->writeDescriptor(descriptor, QByteArray::fromHex("0100"));
            return true;
        }
    }

    qCWarning(lcRobotService, "Could not setup notification characteristic");
    return false;
}

bool RobotService::Private::startTransmission()
{
    if (m_robotControl) {
        m_writeCharacteristic = m_robotControl->characteristic(s_writeUuid);

        if (m_writeCharacteristic.isValid()) {
            connect(m_robotControl, &QLowEnergyService::characteristicChanged,
                    q, [this](const auto &info, const auto &value) {
                this->onCharacteristicChanged(info, value);
            });

            connect(m_robotControl, &QLowEnergyService::characteristicWritten,
                    q, [this](const auto &info, const auto &value) {
                this->onCharacteristicWritten(info, value);
            });

            checkState();
            m_emitter.start(s_transmitterInterval);
            onEmitterTimeout();

            return true;
        }
    }

    qCWarning(lcRobotService, "Could not setup write characteristic");
    return false;
}

void RobotService::Private::onServiceStateChanged(QLowEnergyService *service, QLowEnergyService::ServiceState newState)
{
    qCInfo(lcRobotService, "State of service %ls has changed: %s",
           qUtf16Printable(service->serviceUuid().toString()), key(newState));

    if (newState == QLowEnergyService::DiscoveryRequired) {
        service->discoverDetails();
    } else if (newState == QLowEnergyService::ServiceDiscovered) {
        if (service == m_deviceInformation)
            readFirmwareVersion();
        else if (service == m_robotControl)
            startNotification() && startTransmission();
    }
}

void RobotService::Private::onCharacteristicChanged(const QLowEnergyCharacteristic &info, const QByteArray &value)
{
    qCDebug(lcRobotService, "Value of characteristic %ls has changed: %s",
            qUtf16Printable(info.uuid().toString()), value.toHex().constData());

    if (info.uuid() == s_notifyUuid) {
        const auto text = QString::fromLatin1(value);

        for (const auto play = QRegularExpression(R"(V(\d+)Play)").match(text); play.hasMatch(); ) {
            m_currentSound = play.capturedRef(1).toInt();

            if (!m_audioLoop)
                stopAction('V', m_currentSound);

            emit q->currentSoundChanged(m_currentSound);
            return;
        }

        for (const auto end = QRegularExpression(R"(V(\d+)End)").match(text); end.hasMatch(); ) {
            m_currentSound = end.capturedRef(1).toInt();

            if (m_audioLoop) {
                startAction('M', m_currentSound);
            } else {
                m_currentSound = -m_currentSound;
                stopAction('V', -m_currentSound);
            }

            emit q->currentSoundChanged(m_currentSound);
            return;
        }
    }
}

void RobotService::Private::onCharacteristicWritten(const QLowEnergyCharacteristic &info, const QByteArray &value)
{
    qCDebug(lcRobotService, "Value of characteristic %ls has been written: %s",
            qUtf16Printable(info.uuid().toString()), value.toHex().constData());

    if (info == m_writeCharacteristic) {
        --m_pendingMessageCount;
        setCurrentMessage(5, 0);
    }
}

void RobotService::Private::onEmitterTimeout()
{
    if (m_pendingMessageCount == 0)
        transmitMessage();
}

} // namespace EvoBot
