import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Page Capteurs : live OBD PID + turbo MAP/baro/delta/MAF/RPM
Item {
    id: page
    readonly property int touchMin: 44

    // Modèle local : mises à jour in-place (set) pour ne pas reset le scroll
    ListModel { id: sensorModel }

    function syncSensors() {
        const vals = Drive.sensorValues
        if (sensorModel.count !== vals.length) {
            sensorModel.clear()
            for (let i = 0; i < vals.length; ++i)
                sensorModel.append(vals[i])
            return
        }
        for (let i = 0; i < vals.length; ++i)
            sensorModel.set(i, vals[i])
    }

    Component.onCompleted: syncSensors()

    Connections {
        target: Drive
        function onSensorValuesChanged() { page.syncSensors() }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: contentCol.implicitHeight + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: contentCol
            width: flick.width
            spacing: 12

            // Marges via Item spacers (Layout.margins sur ColumnLayout racine est peu fiable)
            Item { Layout.preferredHeight: 12; Layout.fillWidth: true }

            // ── Turbo live ────────────────────────────────────────────────
            Label {
                text: "TURBO / PRESSION"
                color: "#94a3b8"; font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 1.2
                Layout.leftMargin: 16; Layout.rightMargin: 16
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 12; Layout.rightMargin: 12
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
                Layout.fillWidth: true
                Layout.leftMargin: 16; Layout.rightMargin: 12
                Label { Layout.fillWidth: true; text: "CAPTEURS OBD LIVE"; color: "#94a3b8"; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.2 }
                Button {
                    text: "Rafraîchir"; flat: true; implicitHeight: touchMin
                    contentItem: Label { text: parent.text; color: "#3b82f6"; font.pixelSize: 13; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter }
                    onClicked: Drive.ensureSensorsPolling()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 12; Layout.rightMargin: 12
                color: "#1e293b"; radius: 10; border.color: "#334155"; border.width: 1
                implicitHeight: sensorCol.implicitHeight
                visible: sensorModel.count > 0
                clip: true

                Column {
                    id: sensorCol
                    width: parent.width

                    Repeater {
                        model: sensorModel
                        delegate: Rectangle {
                            required property int index
                            required property string name
                            required property string value
                            required property string unit

                            width: sensorCol.width
                            height: 40
                            color: index % 2 === 0 ? "#0f172a" : "#111827"

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12; anchors.rightMargin: 12
                                Label {
                                    Layout.fillWidth: true
                                    text: name
                                    color: "#cbd5e1"; font.pixelSize: 13
                                    elide: Text.ElideRight
                                }
                                Label {
                                    text: value + " " + unit
                                    color: value === "—" ? "#475569" : "#4ade80"
                                    font.pixelSize: 13; font.weight: Font.DemiBold
                                    font.family: "monospace"
                                }
                            }
                        }
                    }
                }
            }

            Label {
                visible: sensorModel.count === 0
                text: Drive.connected ? "Rafraîchis pour lire les capteurs" : "Connecte l'ELM pour lire les capteurs"
                color: "#475569"; font.pixelSize: 12
                Layout.alignment: Qt.AlignHCenter
            }

            Item { Layout.preferredHeight: 16; Layout.fillWidth: true }
        }
    }
}
