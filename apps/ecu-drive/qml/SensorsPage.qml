import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Page Capteurs : live OBD PID + turbo MAP/baro/delta/MAF/RPM
Item {
    id: page

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: page.width
            spacing: 12
            Layout.leftMargin: 12; Layout.rightMargin: 12; Layout.topMargin: 12

            // ── Turbo live ────────────────────────────────────────────────
            Label {
                text: "TURBO / PRESSION"
                color: "#94a3b8"; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.2
                Layout.leftMargin: 4
            }
            Rectangle {
                Layout.fillWidth: true
                color: "#1e293b"; radius: 10; border.color: "#334155"; border.width: 1
                height: turboCol.height + 16

                GridLayout {
                    id: turboCol
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                    anchors.margins: 12
                    columns: 2; rowSpacing: 10; columnSpacing: 12

                    Label { text: "MAP";   color: "#64748b"; font.pixelSize: 13 }
                    Label { text: Drive.turboMap;   color: Drive.turboMap   === "—" ? "#475569" : "#4ade80"; font.pixelSize: 14; font.weight: Font.DemiBold; font.family: "monospace" }

                    Label { text: "Baro";  color: "#64748b"; font.pixelSize: 13 }
                    Label { text: Drive.turboBaro;  color: Drive.turboBaro  === "—" ? "#475569" : "#4ade80"; font.pixelSize: 14; font.weight: Font.DemiBold; font.family: "monospace" }

                    Label { text: "ΔP";    color: "#64748b"; font.pixelSize: 13 }
                    Label { text: Drive.turboDelta; color: Drive.turboDelta === "—" ? "#475569" : "#4ade80"; font.pixelSize: 14; font.weight: Font.DemiBold; font.family: "monospace" }

                    Label { text: "MAF";   color: "#64748b"; font.pixelSize: 13 }
                    Label { text: Drive.turboMaf;   color: Drive.turboMaf   === "—" ? "#475569" : "#4ade80"; font.pixelSize: 14; font.weight: Font.DemiBold; font.family: "monospace" }

                    Label { text: "RPM";   color: "#64748b"; font.pixelSize: 13 }
                    Label { text: Drive.turboRpm;   color: Drive.turboRpm   === "—" ? "#475569" : "#4ade80"; font.pixelSize: 14; font.weight: Font.DemiBold; font.family: "monospace" }
                }
            }

            // ── Capteurs OBD live ─────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true; Layout.leftMargin: 4
                Label { Layout.fillWidth: true; text: "CAPTEURS OBD LIVE"; color: "#94a3b8"; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.2 }
                Button {
                    text: "Rafraîchir"; flat: true; implicitHeight: 28
                    contentItem: Label { text: parent.text; color: "#3b82f6"; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter }
                    onClicked: Drive.ensureSensorsPolling()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                color: "#1e293b"; radius: 10; border.color: "#334155"; border.width: 1
                height: sensorList.height

                ListView {
                    id: sensorList
                    width: parent.width
                    height: Math.min(contentHeight, 800)
                    model: Drive.sensorValues
                    clip: true
                    interactive: false

                    delegate: Rectangle {
                        width: sensorList.width; height: 40
                        color: index % 2 === 0 ? "#0f172a" : "#111827"
                        radius: index === 0 ? 10 : (index === sensorList.count - 1 ? 10 : 0)

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12; anchors.rightMargin: 12
                            Label {
                                Layout.fillWidth: true
                                text: modelData.name
                                color: "#cbd5e1"; font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                            Label {
                                text: modelData.value + " " + modelData.unit
                                color: modelData.value === "—" ? "#475569" : "#4ade80"
                                font.pixelSize: 13; font.weight: Font.DemiBold
                                font.family: "monospace"
                            }
                        }
                    }
                }
            }

            Label {
                visible: Drive.sensorValues.length === 0
                text: Drive.connected ? "Rafraîchis pour lire les capteurs" : "Connecte l'ELM pour lire les capteurs"
                color: "#475569"; font.pixelSize: 12
                Layout.alignment: Qt.AlignHCenter
            }

            Item { height: 16 }
        }
    }
}
