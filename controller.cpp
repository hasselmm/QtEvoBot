#include "controller.h"

#include "robotservice.h"
#include "utilities.h"

#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothLocalDevice>
#include <QLoggingCategory>
#include <QLowEnergyController>

namespace EvoBot {

namespace {
Q_LOGGING_CATEGORY(lcController, "evobot.controller")
} // namespace

class Controller::Private
{
    class StateGuard
    {
    public:
        StateGuard(Private *d) : d{d} {}
        ~StateGuard() { d->checkState(); }

    private:
        Private *const d;
    };

public:
    explicit Private(Controller *q)
        : q{q}
    {
        if (!m_localDevice.isValid()) {
            raiseError(BluetoothMissingError, tr("No Bluetooth controller available"));
            return;
        }

        connect(&m_localDevice, &QBluetoothLocalDevice::hostModeStateChanged,
                q, [this](auto state) { this->onHostStateChanged(state); });

        connect(&m_deviceDiscovery, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
                q, [this](const auto &device) { this->onDeviceDiscovered(device); });
        connect(&m_deviceDiscovery, QOverload<QBluetoothDeviceDiscoveryAgent::Error>::of(&QBluetoothDeviceDiscoveryAgent::error),
                q, [this](auto error) { this->onDeviceDiscoveryError(error); });
        connect(&m_deviceDiscovery,  &QBluetoothDeviceDiscoveryAgent::finished,
                q, [this] { onDeviceDiscoveryFinished(); });

        connect(&m_robotService, &RobotService::stateChanged, q, [this] { checkState(); });

        onHostStateChanged(m_localDevice.hostMode());
    }

    Error error() const { return m_error; }
    QString errorString() const { return m_errorString; }

    State state() const
    {
        if (m_error != NoError)
            return ErrorState;

        if (m_robotService.state() == RobotService::ConnectedState)
            return ConnectedState;
        if (m_central)
            return ServiceDiscoveryState;
        if (m_deviceDiscovery.isActive())
            return DeviceDiscoveryState;

        return UninitializedState;
    }

    RobotService *robotService()
    {
        return &m_robotService;
    }

private:
    void checkState()
    {
        const auto newState = state();

        if (m_oldState != newState) {
            qCInfo(lcController, "state changed: %s => %s", key(m_oldState), key(newState));
            emit q->stateChanged(newState, m_oldState);
            m_oldState = newState;
        }
    }

    void raiseError(Error error, const QString &errorString)
    {
        StateGuard stateGuard{this};

        m_error = error;
        m_errorString = errorString;
        qCCritical(lcController, "%ls", qUtf16Printable(errorString));
        emit q->errorOccured(m_error, m_errorString);
    }

    // QBluetoothLocalDevice
    void onDeviceDiscovered(const QBluetoothDeviceInfo &device)
    {
        const auto last = end(m_knownDevices);
        if (std::find(begin(m_knownDevices), last, device.address()) != last)
            return;

        m_knownDevices.emplace_back(device.address());

        qCDebug(lcController, "Bluetooth device `%ls' (%ls) discovered",
                qUtf16Printable(device.name()), qUtf16Printable(device.address().toString()));

        if (device.name() == "Evolution-Robot" && !m_central) {
            StateGuard stateGuard{this};

            m_deviceDiscovery.stop();
            m_central = QLowEnergyController::createCentral(device, q);

            connect(m_central, &QLowEnergyController::connected, q, [this] { onDeviceConnected(); });
            connect(m_central, QOverload<QLowEnergyController::Error>::of(&QLowEnergyController::error),
                    q, [this](auto error) { this->onDeviceError(error); });
            connect(m_central, &QLowEnergyController::discoveryFinished,
                    q, [this] { onServiceDiscoveryFinished(); });

            qCInfo(lcController, "Connecting to `%ls' (%ls)", qUtf16Printable(device.name()),
                   qUtf16Printable(device.address().toString()));

            m_central->connectToDevice();
        }
    }

    void onHostStateChanged(QBluetoothLocalDevice::HostMode state)
    {
        StateGuard stateGuard{this};

        if (state == QBluetoothLocalDevice::HostPoweredOff) {
            qCInfo(lcController, "Activating Bluetooth controller");
            m_localDevice.powerOn();
        } else {
            m_deviceDiscovery.start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
        }
    }

    // QBluetoothDeviceDiscoveryAgent
    void onDeviceDiscoveryError(QBluetoothDeviceDiscoveryAgent::Error error)
    {
        raiseError(DeviceDiscoveryError, tr("Device discovery failed: %ls (%d)").
                   arg(m_deviceDiscovery.errorString()).arg(error));
    }

    void onDeviceDiscoveryFinished()
    {
        StateGuard stateGuard{this};
        qCInfo(lcController, "Device discovery has finished");
    }

    // QLowEnergyController
    void onDeviceConnected()
    {
        StateGuard stateGuard{this};

        qCInfo(lcController, "Connected to %ls (%ls)", qUtf16Printable(m_central->remoteName()),
               qUtf16Printable(m_central->remoteAddress().toString()));
        m_central->discoverServices();
    }

    void onDeviceError(QLowEnergyController::Error error)
    {
        raiseError(DeviceError, tr("Device communication failed (%1 (%2))").
                   arg(m_central->errorString()).arg(error));
        resetCentral();
    }

    void onServiceDiscoveryFinished()
    {
        StateGuard stateGuard{this};

        qCInfo(lcController, "Service discovery has finished");

        if (!m_robotService.attach(m_central)) {
            qCWarning(lcController, "Could not find Evolution Robot service at `%ls' (%ls)",
                      qUtf16Printable(m_central->remoteName()), qUtf16Printable(m_central->remoteAddress().toString()));
            resetCentral();
        }
    }

    void resetCentral()
    {
        StateGuard stateGuard{this};

        if (auto central = std::exchange(m_central, {})) {
            central->disconnect(q);
            central->deleteLater();
        }
    }

    //

    Controller *const q;

    Error m_error = NoError;
    QString m_errorString;
    State m_oldState = UninitializedState;

    QBluetoothLocalDevice m_localDevice;
    QBluetoothDeviceDiscoveryAgent m_deviceDiscovery;
    std::vector<QBluetoothAddress> m_knownDevices;
    QLowEnergyController *m_central = {};
    RobotService m_robotService;
};

Controller::Controller(QObject *parent)
    : QObject{parent}
    , d{new Private{this}}
{}

Controller::~Controller()
{
    delete d;
}

Controller::Error Controller::error() const
{
    return d->error();
}

QString Controller::errorString() const
{
    return d->errorString();
}

Controller::State Controller::state() const
{
    return d->state();
}

const char *Controller::stateName(Controller::State state)
{
    return key(state);
}

RobotService *Controller::robotService() const
{
    return d->robotService();
}

} // namespace EvoBot
