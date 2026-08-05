import QtQuick
import QtQuick.Controls

// The TickTick section of the Settings dialog's Integrations page.
//
// Owned by the module rather than by SettingsDialog.qml: this file is only
// compiled into the app's QML module when SUOPNOTES_ENABLE_TICKTICK is on
// (see the guarded qt_target_qml_sources in src/CMakeLists.txt), and
// SettingsDialog reaches it through a Loader by relative URL — never by type
// name — so a build without the module has no reference to resolve. The
// Loader's `active: tickTick !== null` is what keeps the bindings below,
// which dereference `tickTick` freely, from ever evaluating against null.
Column {
    id: tickTickColumn
    spacing: 8

    // The Settings dialog hosting this section, set by the Loader that
    // creates it — the reopen-refresh below needs its visibility signal.
    property var dialog: null

    // The editable fields below are populated from here rather than by
    // binding `text`/`value` directly to the module. A live binding on a
    // field the user is typing into gets re-evaluated whenever the source
    // notifies — and saving notifies — which silently replaced what they had
    // just typed with the previously stored value.
    function reloadFromModule() {
        clientIdField.text = tickTick.auth.clientId
        callbackPortField.value = tickTick.auth.callbackPort
        clientSecretField.text = ""
        // The list of lists is only knowable over the network, so ask for it
        // when the dialog opens rather than holding a stale one from last
        // time. A no-op when not connected.
        tickTick.sync.refreshProjects()
    }

    Component.onCompleted: reloadFromModule()

    // Refresh when the dialog is reopened, so it reflects anything changed
    // since it was last closed.
    Connections {
        target: tickTickColumn.dialog
        function onVisibleChanged() {
            if (tickTickColumn.dialog.visible)
                tickTickColumn.reloadFromModule()
        }
    }

    Label { text: "TickTick"; font.bold: true }

    Label {
        width: tickTickColumn.width
        wrapMode: Text.WordWrap
        text: tickTick.auth.authorized
              ? "Connected. Open tasks in the list chosen below appear as notes; "
                + "completing one in TickTick closes its note."
              : (tickTick.auth.configured
                 ? "Not connected."
                 : "Register an app at developer.ticktick.com, then enter its credentials below.")
    }

    // Surfaced rather than assumed: the access token is valid for six months
    // and grants full task read/write, so a user deserves to know before it
    // lands in a file.
    Label {
        visible: !tickTick.secureStorageAvailable
        width: tickTickColumn.width
        wrapMode: Text.WordWrap
        color: "#c04040"
        text: "No system keyring is available, so the TickTick token will be stored "
              + "in plain text in this app's config directory."
    }

    Row {
        spacing: 8
        Label {
            width: 90
            text: "Client ID"
            anchors.verticalCenter: parent.verticalCenter
        }
        TextField {
            id: clientIdField
            objectName: "tickTickClientIdField"
            width: 260
            // Populated by reloadFromModule(), never bound.
        }
    }

    Row {
        spacing: 8
        Label {
            width: 90
            text: "Client secret"
            anchors.verticalCenter: parent.verticalCenter
        }
        TextField {
            id: clientSecretField
            objectName: "tickTickClientSecretField"
            width: 260
            echoMode: TextInput.Password
            // Never populated from storage — left blank means
            // "keep the saved one", which is why the form can
            // be re-saved without wiping it.
            placeholderText: tickTick.auth.configured ? "unchanged" : ""
        }
    }

    Row {
        spacing: 8
        Label {
            width: 90
            text: "Callback port"
            anchors.verticalCenter: parent.verticalCenter
        }
        SpinBox {
            id: callbackPortField
            objectName: "tickTickCallbackPortField"
            // Same style opt-out as spellcheckLanguageCombo in
            // SettingsDialog.qml.
            wheelEnabled: false
            from: 1024
            to: 65535
            editable: true
            // Populated by reloadFromModule(), never bound.
        }
    }

    // Must match the registered redirect URI byte for byte, and a mismatch
    // fails with an error that does not say which side is wrong — so show
    // exactly what to paste.
    Row {
        spacing: 8
        Label {
            width: 90
            text: "Redirect URI"
            anchors.verticalCenter: parent.verticalCenter
        }
        TextField {
            objectName: "tickTickRedirectUriField"
            width: 260
            readOnly: true
            text: tickTick.auth.redirectUri
        }
    }

    Row {
        spacing: 8

        Button {
            objectName: "tickTickSaveButton"
            text: "Save credentials"
            onClicked: {
                // Read everything up front. Each setter below notifies, and
                // anything still bound to those properties would otherwise be
                // re-evaluated mid-save — so the second call would read a
                // field the first call had already overwritten.
                var enteredId = clientIdField.text
                var enteredSecret = clientSecretField.text
                var enteredPort = callbackPortField.value

                tickTick.auth.setCallbackPort(enteredPort)
                tickTick.auth.setClientCredentials(enteredId, enteredSecret)

                // Cleared because it is a secret; an empty field means "keep
                // the stored one".
                clientSecretField.text = ""
            }
        }

        Button {
            objectName: "tickTickConnectButton"
            text: "Connect"
            enabled: tickTick.auth.configured && !tickTick.auth.authorized
            onClicked: tickTick.auth.beginAuthorization()
        }

        Button {
            objectName: "tickTickSyncButton"
            text: tickTick.sync.busy ? "Syncing…" : "Sync now"
            enabled: tickTick.auth.authorized && !tickTick.sync.busy
            onClicked: tickTick.sync.pullNow()
        }

        Button {
            objectName: "tickTickSignOutButton"
            text: "Sign out"
            enabled: tickTick.auth.authorized
            onClicked: tickTick.auth.signOut()
        }
    }

    // Which list to pull from, and how often. Both are meaningless before a
    // connection exists — the list names can only be learned over the
    // network, and nothing is scheduled while signed out — so the whole
    // block is hidden until then rather than shown empty.
    Row {
        spacing: 8
        visible: tickTick.auth.authorized
        Label {
            width: 90
            text: "List"
            anchors.verticalCenter: parent.verticalCenter
        }
        ComboBox {
            id: projectField
            objectName: "tickTickProjectField"
            width: 260
            model: tickTick.sync.availableProjects
            textRole: "name"
            valueRole: "id"
            currentIndex: indexOfValue(tickTick.sync.selectedProjectId)
            // Until refreshProjects() answers, the model is empty and a
            // ComboBox with no rows displays nothing — which reads as "no
            // list chosen" when one very much is. The remembered name covers
            // exactly that gap.
            displayText: currentIndex >= 0
                         ? currentText
                         : (tickTick.sync.selectedProjectName !== ""
                            ? tickTick.sync.selectedProjectName
                            : "Loading…")
            // onActivated (a user picking a row), NOT onCurrentIndexChanged:
            // the latter also fires when the binding above re-resolves, which
            // happens every time the project list arrives — so opening this
            // dialog would re-select the already-selected list, and
            // selectProject() drops the whole note cache to re-pull.
            onActivated: (index) => tickTick.sync.selectProject(valueAt(index),
                                                                textAt(index))
        }
    }

    Row {
        spacing: 8
        visible: tickTick.auth.authorized
        Label {
            width: 90
            text: "Sync every"
            anchors.verticalCenter: parent.verticalCenter
        }
        // Presets rather than a free spinbox: the useful range spans 5
        // minutes to a day, which no single step size covers without either
        // 288 clicks to reach the top or a step too coarse at the bottom.
        ComboBox {
            id: syncIntervalField
            objectName: "tickTickSyncIntervalField"
            width: 260
            textRole: "label"
            valueRole: "minutes"
            model: [
                { label: "Never — sync manually", minutes: 0 },
                { label: "5 minutes", minutes: 5 },
                { label: "15 minutes", minutes: 15 },
                { label: "30 minutes", minutes: 30 },
                { label: "Hour", minutes: 60 },
                { label: "6 hours", minutes: 360 },
                { label: "Day", minutes: 1440 },
            ]
            currentIndex: indexOfValue(tickTick.sync.syncIntervalMinutes)
            // -1 only for a value no preset covers, which means a hand-edited
            // config file. Showing the real number beats showing a blank box.
            displayText: currentIndex >= 0
                         ? currentText
                         : tickTick.sync.syncIntervalMinutes + " minutes"
            onActivated: (index) => {
                tickTick.sync.syncIntervalMinutes = valueAt(index)
            }
        }
    }

    Label {
        objectName: "tickTickErrorLabel"
        visible: text !== ""
        width: tickTickColumn.width
        wrapMode: Text.WordWrap
        color: "#c04040"
        text: tickTick.sync.lastError
    }

    Connections {
        target: tickTick.auth
        function onAuthorizationFailed(reason) {
            tickTickAuthError.text = reason
        }
        function onAuthorizationSucceeded() {
            tickTickAuthError.text = ""
        }
    }

    Label {
        id: tickTickAuthError
        objectName: "tickTickAuthErrorLabel"
        visible: text !== ""
        width: tickTickColumn.width
        wrapMode: Text.WordWrap
        color: "#c04040"
    }
}
