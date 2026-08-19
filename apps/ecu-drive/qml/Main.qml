import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Qt.labs.platform as Platform

ApplicationWindow {
    id: window
    visible: true
    title: "ECU Drive"
    width: 400
    height: 780

    Material.theme: Material.Dark
    Material.background: "#0f172a"
    Material.foreground: "#e2e8f0"
    Material.accent: "#3b82f6"

    // Bouton retour Android
    onClosing: function(close) {
        close.accepted = false
        if (tabBar.currentIndex !== 0) {
            tabBar.currentIndex = 0
            return
        }
        close.accepted = true
    }

    // Barre titre
    header: Rectangle {
        color: "#1e293b"
        implicitHeight: 56
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 8
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: "ECU Drive"
                color: "#e2e8f0"
                font.pixelSize: 20
                font.weight: Font.DemiBold
            }
            ToolButton {
                contentItem: Label { text: "⟳"; color: "#94a3b8"; font.pixelSize: 18 }
                onClicked: Drive.checkUpdates()
                implicitHeight: 48; implicitWidth: 48
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#334155" }
    }

    // Contenu principal : SwipeView + TabBar
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        SwipeView {
            id: swipeView
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex
            onCurrentIndexChanged: tabBar.currentIndex = currentIndex

            DrivePage  {}
            SensorsPage {}
            DiagPage   {}
        }

        TabBar {
            id: tabBar
            Layout.fillWidth: true
            currentIndex: swipeView.currentIndex
            background: Rectangle { color: "#1e293b" }

            TabButton {
                text: "Conduite"
                contentItem: ColumnLayout {
                    spacing: 2
                    Label { Layout.alignment: Qt.AlignHCenter; text: "🚗"; font.pixelSize: 18 }
                    Label { Layout.alignment: Qt.AlignHCenter; text: "Conduite"; font.pixelSize: 10; color: tabBar.currentIndex === 0 ? "#3b82f6" : "#94a3b8" }
                }
            }
            TabButton {
                text: "Capteurs"
                contentItem: ColumnLayout {
                    spacing: 2
                    Label { Layout.alignment: Qt.AlignHCenter; text: "📊"; font.pixelSize: 18 }
                    Label { Layout.alignment: Qt.AlignHCenter; text: "Capteurs"; font.pixelSize: 10; color: tabBar.currentIndex === 1 ? "#3b82f6" : "#94a3b8" }
                }
            }
            TabButton {
                text: "Diag"
                contentItem: ColumnLayout {
                    spacing: 2
                    Label { Layout.alignment: Qt.AlignHCenter; text: "🔧"; font.pixelSize: 18 }
                    Label { Layout.alignment: Qt.AlignHCenter; text: "Diag"; font.pixelSize: 10; color: tabBar.currentIndex === 2 ? "#3b82f6" : "#94a3b8" }
                }
            }
        }
    }

    // Bandeau mise à jour
    Rectangle {
        id: updateBanner
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: visible ? 56 : 0
        clip: true
        visible: Updater.updateAvailable || Updater.downloading || Updater.readyToInstall
        color: "#1e293b"
        z: 20
        Behavior on height { NumberAnimation { duration: 180 } }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14; anchors.rightMargin: 6
            spacing: 8
            ColumnLayout {
                Layout.fillWidth: true; spacing: 2
                Label {
                    Layout.fillWidth: true; elide: Text.ElideRight
                    font.pixelSize: 14; font.weight: Font.DemiBold; color: "#e2e8f0"
                    text: Updater.downloading ? "Téléchargement…"
                        : Updater.readyToInstall ? "Version " + Updater.latestVersion + " prête"
                        : "Version " + Updater.latestVersion + " disponible"
                }
                Rectangle {
                    Layout.fillWidth: true; height: 3; radius: 2
                    visible: Updater.downloading; color: "#334155"
                    Rectangle {
                        height: parent.height; radius: 2; color: "#3b82f6"
                        width: parent.width * Updater.progress
                        Behavior on width { NumberAnimation { duration: 120 } }
                    }
                }
                Label {
                    visible: !Updater.downloading
                    text: Updater.readyToInstall ? "Android vous demandera confirmation"
                        : "Vous avez la " + Updater.currentVersion
                    color: "#64748b"; font.pixelSize: 12
                }
            }
            Button {
                flat: true; visible: !Updater.downloading
                implicitHeight: 48
                contentItem: Label {
                    text: Updater.readyToInstall ? "Installer" : "Mettre à jour"
                    color: "#3b82f6"; font.pixelSize: 14; font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
                onClicked: Updater.readyToInstall ? Updater.install() : Updater.download()
            }
            ToolButton {
                visible: !Updater.downloading; implicitWidth: 36; implicitHeight: 48
                contentItem: Label { text: "✕"; color: "#64748b"; horizontalAlignment: Text.AlignHCenter }
                onClicked: Updater.dismiss()
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#334155" }
    }

    // Bandeau statut en bas
    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: tabBar.top }
        height: statusLabel.text.length > 0 ? 28 : 0
        clip: true
        color: "#0f172a"
        z: 5
        Behavior on height { NumberAnimation { duration: 120 } }

        Label {
            id: statusLabel
            anchors.fill: parent
            anchors.leftMargin: 12; anchors.rightMargin: 12
            text: Drive.statusText
            color: Drive.statusError ? "#ef4444" : "#64748b"
            font.pixelSize: 11
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }

    // Dialog info
    Popup {
        id: infoDialog
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 400)
        padding: 0
        modal: true
        closePolicy: Popup.NoAutoClose

        property string title: ""
        property string body: ""
        property string okLabel: "OK"
        property var onOkCb: null

        function showMsg(t, b, ok, cb) {
            title = t; body = b; okLabel = ok || "OK"; onOkCb = cb || null
            open()
        }

        background: Rectangle {
            color: "#1e293b"; radius: 14
            border.color: "#334155"; border.width: 1
        }
        contentItem: ColumnLayout {
            spacing: 0
            Label {
                Layout.fillWidth: true
                text: infoDialog.title
                color: "#e2e8f0"; font.pixelSize: 16; font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
                padding: 16; bottomPadding: 8
            }
            Label {
                Layout.fillWidth: true
                text: infoDialog.body
                color: "#94a3b8"; font.pixelSize: 13
                wrapMode: Text.WordWrap
                padding: 16; topPadding: 0
            }
            Rectangle { height: 1; Layout.fillWidth: true; color: "#334155" }
            Button {
                Layout.fillWidth: true; flat: true; implicitHeight: 48
                contentItem: Label {
                    text: infoDialog.okLabel; color: "#3b82f6"; font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
                onClicked: { infoDialog.close(); if (infoDialog.onOkCb) infoDialog.onOkCb() }
            }
        }
    }

    // ECU Picker
    Popup {
        id: ecuPicker
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 400)
        padding: 0; modal: true; closePolicy: Popup.NoAutoClose

        property var ecuIds: []
        property string hint: ""

        background: Rectangle { color: "#1e293b"; radius: 14; border.color: "#334155"; border.width: 1 }
        contentItem: ColumnLayout {
            spacing: 0
            Label {
                Layout.fillWidth: true; padding: 16; bottomPadding: 8
                text: "Sélectionner l'ECU"; color: "#e2e8f0"; font.pixelSize: 16; font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true; padding: 16; topPadding: 0
                text: ecuPicker.hint; color: "#64748b"; font.pixelSize: 12; wrapMode: Text.WordWrap
                visible: ecuPicker.hint.length > 0
            }
            ListView {
                id: ecuList
                Layout.fillWidth: true
                height: Math.min(contentHeight, 240)
                model: ecuPicker.ecuIds
                clip: true
                delegate: ItemDelegate {
                    width: ecuList.width
                    contentItem: Label {
                        text: modelData; color: "#e2e8f0"; font.pixelSize: 14
                        verticalAlignment: Text.AlignVCenter
                    }
                    highlighted: ecuList.currentIndex === index
                    onClicked: ecuList.currentIndex = index
                    background: Rectangle {
                        color: ecuList.currentIndex === index ? "#2563eb22" : "transparent"
                    }
                }
            }
            Rectangle { height: 1; Layout.fillWidth: true; color: "#334155" }
            RowLayout {
                Layout.fillWidth: true; spacing: 0
                Button {
                    Layout.fillWidth: true; flat: true; implicitHeight: 48
                    contentItem: Label { text: "Annuler"; color: "#64748b"; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    onClicked: { ecuPicker.close(); Drive.ecuPickerCancelled() }
                }
                Rectangle { width: 1; height: 48; color: "#334155" }
                Button {
                    Layout.fillWidth: true; flat: true; implicitHeight: 48
                    contentItem: Label { text: "OK"; color: "#3b82f6"; font.pixelSize: 14; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    onClicked: {
                        const idx = ecuList.currentIndex
                        ecuPicker.close()
                        Drive.ecuPickerAccepted(ecuPicker.ecuIds[idx >= 0 ? idx : 0])
                    }
                }
            }
        }
    }

    Connections {
        target: Drive
        function onShowDialog(title, body, okLabel) {
            infoDialog.showMsg(title, body, okLabel, null)
        }
        function onShowEcuPicker(ecuIds, hint) {
            ecuPicker.ecuIds = ecuIds
            ecuPicker.hint = hint
            ecuPicker.open()
        }
        function onToast(message) {
            snackbar.show(message)
        }
        function onRequestFilePicker() {
            filePicker.open()
        }
    }

    Platform.FileDialog {
        id: filePicker
        title: "Importer tune / ROM"
        nameFilters: ["Tune / ROM (*.ecutune *.bin *.zip)", "Tous les fichiers (*.*)"]
        onAccepted: {
            const path = file.toString().replace("file://", "")
            Qt.callLater(function() { Drive.loadTuneFile(path) })
        }
    }

    Popup {
        id: snackbar
        y: parent.height - height - 72
        x: 16; width: parent.width - 32
        padding: 14; modal: false; closePolicy: Popup.NoAutoClose
        property string message: ""
        function show(text) { message = text; open(); hideTimer.restart() }
        Timer { id: hideTimer; interval: 2800; onTriggered: snackbar.close() }
        background: Rectangle { color: "#1e293b"; radius: 12; border.color: "#334155"; border.width: 1 }
        contentItem: Label { text: snackbar.message; color: "#e2e8f0"; font.pixelSize: 14; wrapMode: Text.WordWrap }
    }
}
