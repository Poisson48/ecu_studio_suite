import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Page Conduite : connexion + session + affichage boost temps réel
Item {
    id: page

    // ── Helpers couleur boost ─────────────────────────────────────────────
    function boostBannerColor() {
        const v = Drive.verdict
        if (v === "") return "#1e293b"
        if (v.indexOf("UNDERBOOST") >= 0 || v.indexOf("OVERBOOST") >= 0)
            return "#7f1d1d"
        if (v.indexOf("LÉGER") >= 0)
            return "#78350f"
        if (v === "TURBO OK")
            return "#14532d"
        return "#1e293b"
    }
    function boostTextColor() {
        const v = Drive.verdict
        if (v.indexOf("UNDERBOOST") >= 0 || v.indexOf("OVERBOOST") >= 0) return "#f87171"
        if (v.indexOf("LÉGER") >= 0) return "#fbbf24"
        if (v === "TURBO OK") return "#4ade80"
        return "#60a5fa"
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: page.width
            spacing: 0

            // ── Connexion ─────────────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                color: "#1e293b"
                radius: 0
                height: connectSection.height + 24

                ColumnLayout {
                    id: connectSection
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                    anchors.margins: 12
                    spacing: 8

                    Label {
                        text: "Connexion ELM327"
                        color: "#94a3b8"; font.pixelSize: 11; font.weight: Font.DemiBold
                        font.letterSpacing: 1.2
                    }

                    // Tune
                    Rectangle {
                        Layout.fillWidth: true
                        height: tuneRow.height + 12; radius: 10
                        color: "#0f172a"; border.color: "#334155"; border.width: 1
                        RowLayout {
                            id: tuneRow
                            anchors { left: parent.left; right: parent.right; top: parent.top }
                            anchors.margins: 10; spacing: 8
                            Label {
                                Layout.fillWidth: true
                                text: Drive.tuneLabel
                                color: Drive.tuneReady ? "#4ade80" : "#94a3b8"
                                font.pixelSize: 11; wrapMode: Text.WordWrap
                            }
                            Button {
                                text: "Importer"
                                Material.background: "#1e3a5f"; Material.foreground: "#93c5fd"
                                font.pixelSize: 12; implicitHeight: 36
                                onClicked: Drive.importTune()
                            }
                        }
                    }

                    // BT + USB
                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        ComboBox {
                            id: btCombo
                            Layout.fillWidth: true
                            model: Drive.btDevices.map(d => (d.likelyObd ? "★ " : "") + d.name + " (" + d.addr + ")")
                            displayText: Drive.btDevices.length > 0
                                ? (currentIndex >= 0 ? model[currentIndex] : "Sélectionner…")
                                : "Aucun appareil"
                            Material.background: "#0f172a"
                            font.pixelSize: 12
                            onActivated: {
                                if (Drive.btDevices[currentIndex])
                                    Drive.selectedBt = Drive.btDevices[currentIndex].addr
                            }
                        }
                        Button {
                            text: Drive.btScanning ? "■" : "Scan BT"
                            Material.background: Drive.btScanning ? "#4c1d95" : "#1e293b"
                            Material.foreground: "#c4b5fd"
                            font.pixelSize: 12; implicitHeight: 36; implicitWidth: 80
                            onClicked: Drive.startBtScan()
                        }
                    }

                    // USB ports (non-Android)
                    ComboBox {
                        Layout.fillWidth: true
                        visible: Drive.ports.length > 0
                        model: Drive.ports.map(p => p.split("|")[0])
                        Material.background: "#0f172a"; font.pixelSize: 12
                    }

                    // Boutons Connecter / Session
                    RowLayout {
                        Layout.fillWidth: true; spacing: 8

                        Button {
                            Layout.fillWidth: true
                            text: Drive.connected ? "Déconnecter" : "Connecter"
                            Material.background: Drive.connected ? "#991b1b" : "#1d4ed8"
                            Material.foreground: "#ffffff"
                            font.pixelSize: 13; font.weight: Font.DemiBold; implicitHeight: 44
                            onClicked: Drive.toggleConnect()
                        }

                        Button {
                            Layout.fillWidth: true
                            text: Drive.sessionOn ? "■ Arrêter" : "▶ Session"
                            enabled: Drive.connected && Drive.tuneReady
                            Material.background: Drive.sessionOn ? "#166534" : "#1e3a5f"
                            Material.foreground: Drive.sessionOn ? "#4ade80" : (Drive.connected && Drive.tuneReady ? "#93c5fd" : "#475569")
                            font.pixelSize: 13; font.weight: Font.DemiBold; implicitHeight: 44
                            onClicked: Drive.toggleSession()
                        }
                    }

                    // Options BT Only
                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        CheckBox {
                            text: "OBD seulement (BT)"
                            checked: Drive.btObdOnly
                            onCheckedChanged: Drive.btObdOnly = checked
                            Material.foreground: "#94a3b8"; font.pixelSize: 12
                        }
                        CheckBox {
                            text: "Bip alerte"
                            checked: Drive.beepAlert
                            onCheckedChanged: Drive.beepAlert = checked
                            Material.foreground: "#94a3b8"; font.pixelSize: 12
                        }
                    }
                }
            }

            // ── Bannière boost ────────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                height: boostContent.height + 24
                color: boostBannerColor()
                Behavior on color { ColorAnimation { duration: 300 } }
                radius: 0

                ColumnLayout {
                    id: boostContent
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                    anchors.margins: 16
                    spacing: 4

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: Drive.verdict || "—"
                        color: boostTextColor()
                        font.pixelSize: 22; font.weight: Font.Bold
                        Behavior on color { ColorAnimation { duration: 200 } }
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: Drive.boostBig || "—"
                        color: boostTextColor()
                        font.pixelSize: 32; font.weight: Font.Black
                        Behavior on color { ColorAnimation { duration: 200 } }
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: Drive.boostSub
                        color: "#94a3b8"; font.pixelSize: 14
                        visible: Drive.boostSub.length > 0
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: Drive.rpmLoad
                        color: "#64748b"; font.pixelSize: 12
                        visible: Drive.rpmLoad.length > 0
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: Drive.sessionLive
                        color: "#94a3b8"; font.pixelSize: 11
                        visible: Drive.sessionOn && Drive.sessionLive.length > 0
                    }
                }
            }

            // ── Liste maps ────────────────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Layout.leftMargin: 12; Layout.rightMargin: 12; Layout.topMargin: 12

                Label {
                    text: "Maps validées"
                    color: "#94a3b8"; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.2
                    visible: Drive.mapsList.length > 0
                }

                Repeater {
                    model: Drive.mapsList
                    Rectangle {
                        width: page.width - 24; height: mapRow.height + 16
                        color: "#111827"; radius: 8
                        border.color: "#1e293b"; border.width: 1
                        Row {
                            id: mapRow
                            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
                            anchors.margins: 10; spacing: 8
                            Label {
                                text: modelData.status
                                color: modelData.status === "OK" ? "#4ade80"
                                     : modelData.status === "Ecart" ? "#f87171" : "#fbbf24"
                                font.pixelSize: 12; font.weight: Font.DemiBold
                            }
                            Label {
                                text: modelData.mapName; color: "#e2e8f0"; font.pixelSize: 12
                            }
                            Label {
                                text: "Δ" + modelData.delta + " " + modelData.unit
                                color: "#64748b"; font.pixelSize: 11
                            }
                        }
                    }
                }

                Item { height: 16 }
            }
        }
    }
}
