import EvoBotTest 1.0

import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtQuick.Window 2.3

ApplicationWindow {
    readonly property string allActions: "LLLLRRRRFFFFBBBBUDOCSEEEEEEVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV"

    width: 1024
    height: 768
    visible: true

    font.pointSize: 16

    ColumnLayout {
        Label {
            text: {
                var text = "%1 (service state: %2)".arg(_evobot.stateName(_evobot.state)).arg(_evobot.robotService.state);
                if (_evobot.state == Controller.ErrorState)
                    text += " - " + _evobot.errorString;
                return text;
            }
        }

        Label {
            text: "current sound: " + _evobot.robotService.currentSound
        }
    }

    GridLayout {
        anchors.centerIn: parent

        enabled: _evobot.state === Controller.ConnectedState
        opacity: enabled ? 1 : 0.5
        Behavior on opacity { NumberAnimation {} }

        Repeater {
            model: allActions.length

            Button {
                readonly property string action: allActions.charAt(modelData)
                readonly property int force: {
                    if (modelData < 16)
                        return modelData % 4;
                    if (action === "V")
                        return modelData - allActions.indexOf("V");
                    if (action === "E")
                        return modelData - allActions.indexOf("E");

                    return -1;
                }

                Layout.column: {
                    switch(action) {
                    case "L": return 3 - force;
                    case "R": return 5 + force;

                    case "F":
                    case "B":
                    case "S": return 4;

                    case "C": return 0;
                    case "U":
                    case "D": return 1;
                    case "O": return 2;

                    case "E": return force % 2 + 1
                    case "V": return force % 4 + 6
                    }
                }

                Layout.row: {
                    switch(action) {
                    case "F": return 4 - force;
                    case "B": return 6 + force;

                    case "L":
                    case "R":
                    case "S": return 5;

                    case "O":
                    case "C": return 2;

                    case "U": return 1;
                    case "D": return 3;

                    case "E": return force / 2 + 7;
                    case "V": return force / 4 + (force >= 16 ? 3 : 0)
                    }
                }

                Layout.fillWidth: true

                checkable: action === "V"
                checked: action === "V" && Math.max(0, _evobot.robotService.currentSound) === force
                enabled: action !== "V" || force === 0 || _evobot.robotService.currentSound <= 0
                font.pointSize: 20
                text: action + (force >= 0 ? force : "")

                onPressed: {
                    if (action !== "V" && action != "E")
                        _evobot.robotService.startAction(action, force);
                }

                onReleased: {
                    if (action !== "V" && action != "E")
                        _evobot.robotService.stopAction(action, force);
                }

                onClicked: {
                    if (action === "V" || action == "E")
                        _evobot.robotService.startAction(action, force);
                }

                onPressAndHold: {
                    if (action === "V")
                        _evobot.robotService.playLoop(force, true);
                }
            }
        }

        RowLayout {
            Layout.column: 0
            Layout.columnSpan: 10
            Layout.row: 11

            Repeater {
                id: messageRepeater

                model: 6

                TextField {
                    Layout.fillWidth: true

                    //inputMask: "#dd9"
                    inputMethodHints: Qt.ImhPreferNumbers
                    text: _evobot.robotService.currentMessage[modelData]
                    validator: IntValidator { bottom: -128; top: 127 }
                    maximumLength: 3
                }
            }

            Button {
                text: "Send"

                onClicked: {
                    var message = [];

                    for (var i = 0; i < messageRepeater.count; ++i)
                        message.push(parseInt(messageRepeater.itemAt(i).text));

                    _evobot.robotService.currentMessage = message;
                }
            }
        }
    }
}
