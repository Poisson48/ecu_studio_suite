import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Page Diagnostic : Security Access, Actionneurs EDC16, DTC, Console OBD brute
Item {
    id: page
    readonly property int touchMin: 44
    readonly property bool compactLayout: page.width < 360

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            id: mainCol
            width: page.width
            spacing: 12

                Item { Layout.preferredHeight: 12 }

                // ── Session ECU rapide ─────────────────────────────────────
                DiagSectionLabel { text: "SESSION ECU — BOUTONS RAPIDES" }
                Rectangle {
                    Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                    color: "#1e293b"; radius: 10; border.color: "#334155"; border.width: 1
                    height: grid1.height + 16
                    GridLayout {
                        id: grid1
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        columns: compactLayout ? 1 : 2; columnSpacing: 8; rowSpacing: 6
                        DiagBtn { text: "Ouvrir session"; tip: "10 C0"; cmd: "10 C0"; layout: parent }
                        DiagBtn { text: "Keep-alive";     tip: "3E 00"; cmd: "3E 00"; layout: parent }
                        DiagBtn { text: "Reboot ECU";     tip: "31 A8 00"; cmd: "31 A8 00"; layout: parent }
                        DiagBtn { text: "Reset comm.";    tip: "FF FF"; cmd: "FF FF"; layout: parent }
                        DiagBtn { text: "Lire VIN";       tip: "22 F1 90"; cmd: "22 F1 90"; layout: parent }
                        DiagBtn { text: "ID ECU";         tip: "21 80"; cmd: "21 80"; layout: parent }
                    }
                }

                // ── Zones PSA lecture rapide ────────────────────────────────
                DiagSectionLabel { text: "ZONES ECU — LECTURE RAPIDE (SERVICE 21)" }
                Rectangle {
                    Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                    color: "#1e293b"; radius: 10; border.color: "#334155"; border.width: 1
                    height: grid2.height + 16
                    GridLayout {
                        id: grid2
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        columns: compactLayout ? 1 : 2; columnSpacing: 8; rowSpacing: 6
                        DiagBtn { text: "ID ECU (21 80)";    cmd: "21 80";    layout: parent }
                        DiagBtn { text: "VIN (22 F1 90)";    cmd: "22 F1 90"; layout: parent }
                        DiagBtn { text: "Calib. (21 01)";    cmd: "21 01";    layout: parent }
                        DiagBtn { text: "Compt. (21 B2)";    cmd: "21 B2";    layout: parent }
                        DiagBtn { text: "Erreurs (21 10)";   cmd: "21 10";    layout: parent }
                        DiagBtn { text: "Live data (21 E0)"; cmd: "21 E0";    layout: parent }
                    }
                }

                // ── Security Access ─────────────────────────────────────────
                DiagSectionLabel { text: "ACCÈS CONSTRUCTEUR — SECURITY ACCESS" }
                Rectangle {
                    Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                    color: "#1e293b"; radius: 10; border.color: "#334155"; border.width: 1
                    height: saCol.height + 16

                    ColumnLayout {
                        id: saCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            ComboBox {
                                id: saProtoCombo; Layout.fillWidth: true
                                model: ["PSA_EDC16", "PSA_KWP_GENERIC", "VAG_SA2", "DAIMLER", "BOSCH_GENERIC"]
                                Material.background: "#0f172a"; font.pixelSize: 12
                            }
                            Label { text: "Seed (hex)"; color: "#64748b"; font.pixelSize: 12 }
                            TextField {
                                id: saSeedEdit; Layout.preferredWidth: compactLayout ? 120 : 140
                                placeholderText: "DEADBEEF"; font.pixelSize: 12
                                color: "#e2e8f0"; background: Rectangle { color: "#0f172a"; border.color: "#334155"; radius: 6 }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            Label { text: "Clé ECU (hex)"; color: "#64748b"; font.pixelSize: 12 }
                            TextField {
                                id: saKeyEdit; Layout.preferredWidth: compactLayout ? 120 : 140
                                placeholderText: "optionnel"; font.pixelSize: 12
                                color: "#e2e8f0"; background: Rectangle { color: "#0f172a"; border.color: "#334155"; radius: 6 }
                            }
                            Button {
                                text: "Calculer"
                                Material.background: "#1e3a5f"; Material.foreground: "#93c5fd"
                                font.pixelSize: 12; implicitHeight: 36
                                onClicked: Drive.computeSaKey(saProtoCombo.currentText, saSeedEdit.text, saKeyEdit.text)
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true; height: saResultLabel.height + 12
                            color: "#0f172a"; radius: 8; border.color: "#334155"; border.width: 1
                            Label {
                                id: saResultLabel
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                                text: Drive.saResult.length > 0 ? Drive.saResult : "—"
                                color: Drive.saResult.startsWith("✓") ? "#4ade80"
                                     : Drive.saResult.startsWith("✗") ? "#f87171" : "#94a3b8"
                                font.pixelSize: 13; font.family: "monospace"; wrapMode: Text.WrapAnywhere
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            Button {
                                Layout.fillWidth: true; text: "Déverr. auto (KWP)"
                                Material.background: "#4c1d95"; Material.foreground: "#c4b5fd"
                                font.pixelSize: 11; implicitHeight: 36
                                onClicked: Drive.sendSecurityAccess(saProtoCombo.currentText, 1, saKeyEdit.text, true)
                            }
                            Button {
                                Layout.fillWidth: true; text: "Déverr. auto (UDS)"
                                Material.background: "#1e3a5f"; Material.foreground: "#93c5fd"
                                font.pixelSize: 11; implicitHeight: 36
                                onClicked: Drive.sendSecurityAccess(saProtoCombo.currentText, 1, saKeyEdit.text, false)
                            }
                        }
                    }
                }

                // ── Actionneurs EDC16 (service 30 IO Control) ──────────────
                DiagSectionLabel { text: "ACTIONNEURS EDC16 — IO CONTROL (SERVICE 30 / 31)" }
                Rectangle {
                    Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                    color: "#1e293b"; radius: 10; border.color: "#334155"; border.width: 1
                    height: actCol.height + 16

                    ColumnLayout {
                        id: actCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        spacing: 6

                        Label { text: "EGR"; color: "#94a3b8"; font.pixelSize: 11; font.weight: Font.DemiBold }
                        ActPair { lbl: "EGR"; on_cmd: "30 21 07 FF"; off_cmd: "30 21 00" }

                        Label { text: "Injecteurs"; color: "#94a3b8"; font.pixelSize: 11; font.weight: Font.DemiBold }
                        ActPair { lbl: "Inj 1"; on_cmd: "30 10 07 FF"; off_cmd: "30 10 00" }
                        ActPair { lbl: "Inj 2"; on_cmd: "30 11 07 FF"; off_cmd: "30 11 00" }
                        ActPair { lbl: "Inj 3"; on_cmd: "30 12 07 FF"; off_cmd: "30 12 00" }
                        ActPair { lbl: "Inj 4"; on_cmd: "30 13 07 FF"; off_cmd: "30 13 00" }

                        Label { text: "FAP / DPF"; color: "#94a3b8"; font.pixelSize: 11; font.weight: Font.DemiBold }
                        ActPair { lbl: "Regen FAP";  on_cmd: "30 24 07 FF"; off_cmd: "30 24 00" }
                        ActPair { lbl: "Temp regen"; on_cmd: "30 25 07 FF"; off_cmd: "30 25 00" }

                        Label { text: "Admission"; color: "#94a3b8"; font.pixelSize: 11; font.weight: Font.DemiBold }
                        ActPair { lbl: "Papillon"; on_cmd: "30 2B 07 FF"; off_cmd: "30 2B 00" }

                        Label { text: "Wastegate (PSA — à vérifier)"; color: "#64748b"; font.pixelSize: 10 }
                        ActPair { lbl: "Wastegate"; on_cmd: "31 A0 01"; off_cmd: "31 A0 00" }
                    }
                }

                // ── Routines PSA (service 31) ───────────────────────────────
                DiagSectionLabel { text: "ROUTINES PSA CONFIRMÉES (SERVICE 31)" }
                Rectangle {
                    Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                    color: "#1e293b"; radius: 10; border.color: "#334155"; border.width: 1
                    height: grid3.height + 16
                    GridLayout {
                        id: grid3
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        columns: compactLayout ? 1 : 2; columnSpacing: 8; rowSpacing: 6
                        DiagBtn { text: "Reboot ECU";        cmd: "31 A8 00"; layout: parent }
                        DiagBtn { text: "Flash autocontrol"; cmd: "31 02 01"; layout: parent }
                        DiagBtn { text: "Raz adapt.";        cmd: "31 DE 00"; layout: parent }
                        DiagBtn { text: "Reset injections";  cmd: "31 D8 00"; layout: parent }
                    }
                }

                // ── DTC ────────────────────────────────────────────────────
                DiagSectionLabel { text: "CODES DÉFAUT (DTC)" }
                Rectangle {
                    Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                    color: "#1e293b"; radius: 10; border.color: "#334155"; border.width: 1
                    height: dtcCol.height + 16

                    ColumnLayout {
                        id: dtcCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            Button {
                                Layout.fillWidth: true; text: "Lire DTC"
                                enabled: Drive.dtcReadEnabled
                                Material.background: "#1e3a5f"; Material.foreground: "#93c5fd"
                                font.pixelSize: 12; implicitHeight: 36
                                onClicked: Drive.readDtcs()
                            }
                            Button {
                                Layout.fillWidth: true; text: "Effacer"
                                enabled: Drive.dtcClearEnabled
                                Material.background: "#7f1d1d"; Material.foreground: "#fca5a5"
                                font.pixelSize: 12; implicitHeight: 36
                                onClicked: Drive.clearDtcs()
                            }
                            Button {
                                text: "Copier"; enabled: Drive.dtcClearEnabled
                                Material.background: "#1e293b"; Material.foreground: "#94a3b8"
                                font.pixelSize: 12; implicitHeight: 36; implicitWidth: 64
                                onClicked: Drive.copyDtcs()
                            }
                        }

                        Repeater {
                            model: Drive.dtcList
                            Rectangle {
                                width: mainCol.width - 44; height: dtcRow.height + 12
                                color: "#0f172a"; radius: 8
                                border.color: modelData.status === "en cours" ? "#7f1d1d" : "#1e293b"
                                border.width: 1
                                RowLayout {
                                    id: dtcRow
                                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                                    spacing: 8
                                    Label { text: modelData.code; color: "#f87171"; font.pixelSize: 13; font.weight: Font.DemiBold; font.family: "monospace" }
                                    Label { Layout.fillWidth: true; text: modelData.family; color: "#94a3b8"; font.pixelSize: 12 }
                                    Label { text: modelData.status; color: modelData.status === "en cours" ? "#f87171" : "#fbbf24"; font.pixelSize: 11 }
                                }
                            }
                        }

                        Label {
                            visible: Drive.dtcList.length === 0
                            text: Drive.dtcReadEnabled ? "Aucun code défaut lu" : "Connecte l'ELM pour lire les DTC"
                            color: "#475569"; font.pixelSize: 12; Layout.alignment: Qt.AlignHCenter
                        }
                    }
                }

                // ── Console OBD brute ──────────────────────────────────────
                DiagSectionLabel { text: "CONSOLE OBD BRUTE" }
                Rectangle {
                    Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                    color: "#1e293b"; radius: 10; border.color: "#334155"; border.width: 1
                    height: consoleCol.height + 16

                    ColumnLayout {
                        id: consoleCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            TextField {
                                id: rawCmdEdit
                                Layout.fillWidth: true
                                placeholderText: "ex: 01 0C"
                                color: "#e2e8f0"; font.pixelSize: 13; font.family: "monospace"
                                background: Rectangle { color: "#0f172a"; border.color: "#334155"; radius: 6 }
                                onAccepted: { Drive.sendRawCommand(text); text = "" }
                            }
                            Button {
                                text: "Envoyer"
                                Material.background: "#1e3a5f"; Material.foreground: "#93c5fd"
                                font.pixelSize: 12; implicitHeight: 40
                                onClicked: { Drive.sendRawCommand(rawCmdEdit.text); rawCmdEdit.text = "" }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true; height: 180
                            color: "#0f172a"; radius: 8; border.color: "#334155"; border.width: 1
                            clip: true
                            Flickable {
                                id: rawLogFlick
                                anchors.fill: parent; anchors.margins: 8
                                contentHeight: rawLogText.implicitHeight
                                flickableDirection: Flickable.VerticalFlick
                                clip: true
                                onContentHeightChanged: {
                                    if (contentHeight > height)
                                        contentY = contentHeight - height
                                }
                                Text {
                                    id: rawLogText
                                    width: rawLogFlick.width
                                    text: Drive.rawLog
                                    color: "#4ade80"; font.pixelSize: 11; font.family: "monospace"
                                    wrapMode: Text.WrapAnywhere
                                }
                            }
                        }
                    }
                }

            Item { Layout.preferredHeight: 16 }
        }
    }

    // ── Composants locaux (non-inline pour compatibilité Qt 6.2) ────────────

    component DiagSectionLabel : Label {
        Layout.fillWidth: true
        Layout.leftMargin: 16
        color: "#94a3b8"; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 1.2
    }

    component DiagBtn : Button {
        required property string cmd
        property string tip: ""
        property Item layout: null
        Layout.fillWidth: true
        Material.background: "#0f172a"; Material.foreground: "#93c5fd"
        font.pixelSize: 12; implicitHeight: touchMin
        ToolTip.visible: tip.length > 0 && hovered; ToolTip.text: tip
        onClicked: if (cmd.length > 0) Drive.sendRawCommand(cmd)
    }

    component ActPair : RowLayout {
        required property string lbl
        required property string on_cmd
        required property string off_cmd
        Layout.fillWidth: true; spacing: 6
        Button {
            Layout.fillWidth: true; text: parent.lbl + " ▲ ON"
            Material.background: "#14532d"; Material.foreground: "#4ade80"
            font.pixelSize: 11; implicitHeight: touchMin
            onClicked: Drive.sendActuatorOn(parent.on_cmd)
        }
        Button {
            Layout.fillWidth: true; text: parent.lbl + " ▼ OFF"
            Material.background: "#7f1d1d"; Material.foreground: "#fca5a5"
            font.pixelSize: 11; implicitHeight: touchMin
            onClicked: Drive.sendActuatorOff(parent.off_cmd)
        }
    }
}
