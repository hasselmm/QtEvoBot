#ifndef ROBOTSERVICE_H
#define ROBOTSERVICE_H

#include <QObject>

class QLowEnergyController;

namespace EvoBot {

class RobotService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(QList<int> currentMessage READ currentMessage WRITE setCurrentMessage NOTIFY currentMessageChanged FINAL)
    Q_PROPERTY(int currentSound READ currentSound NOTIFY currentSoundChanged FINAL)

public:
    enum State {
        DisconnectedState,
        ConnectingState,
        ConnectedState,
    };

    Q_ENUM(State)

    explicit RobotService(QObject *parent = {});
    ~RobotService();

    bool attach(QLowEnergyController *central);

    void setCurrentMessage(const QList<int> &message);
    QList<int> currentMessage() const;
    int currentSound() const;
    State state() const;

public slots:
    bool startAction(QChar action, int index = 0);
    bool stopAction(QChar action, int index = 0);
    bool playSound(int index);
    bool playLoop(int index);

signals:
    void currentMessageChanged(const QByteArray &message);
    void currentSoundChanged(int currentSound);
    void stateChanged(int newState, int oldState);

private:
    class Private;
    Private *const d;
};

} // namespace EvoBot

#endif // ROBOTSERVICE_H
