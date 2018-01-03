#include "controller.h"
#include "robotservice.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

namespace EvoBot {

class TestApplication : public QGuiApplication
{
    Q_OBJECT

public:
    using QGuiApplication::QGuiApplication;

    int run()
    {
        qmlRegisterUncreatableType<Controller>("EvoBotTest", 1, 0, "Controller", {});
        qmlRegisterUncreatableType<RobotService>("EvoBotTest", 1, 0, "RobotService", {});

        QQmlApplicationEngine qml;
        qml.rootContext()->setContextProperty("_evobot", &m_controller);
        qml.load("qrc:/main.qml");

        return exec();
    }

private:
    Controller m_controller;
};

} // namespace EvoBot

int main(int argc, char *argv[])
{
    return EvoBot::TestApplication{argc, argv}.run();
}

#include "main.moc"
