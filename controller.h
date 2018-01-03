#ifndef EVOBOT_CONTROLLER_H
#define EVOBOT_CONTROLLER_H

#include <QObject>

namespace EvoBot {

class RobotService;

class Controller : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int error READ error NOTIFY errorOccured FINAL)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorOccured FINAL)
    Q_PROPERTY(int state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(EvoBot::RobotService *robotService READ robotService CONSTANT FINAL)

public:
    enum State {
        UninitializedState,
        DeviceDiscoveryState,
        ServiceDiscoveryState,
        ConnectingState,
        ConnectedState,
        ErrorState,
    };

    Q_ENUM(State)

    enum Error {
        NoError,
        BluetoothMissingError,
        DeviceDiscoveryError,
        DeviceError,
    };

    Q_ENUM(Error)

    explicit Controller(QObject *parent = {});
    ~Controller();

    Error error() const;
    QString errorString() const;
    State state() const;

    Q_INVOKABLE static QByteArray stateName(int state) { return stateName(static_cast<State>(state)); }
    static const char *stateName(State state);

    RobotService *robotService() const;

signals:
    void errorOccured(Error error, const QString &errorString);
    void stateChanged(State newState, State oldState);

private:
    class Private;
    Private *const d;
};

} // namespace EvoBot

#endif // EVOBOT_CONTROLLER_H
