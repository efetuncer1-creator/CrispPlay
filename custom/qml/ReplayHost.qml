// Instant replay: shortcut and save notice. Loaded by one VEYRA_CUSTOM line in
// qml/Veyra/Main.qml; `veyra` is the upstream bridge, `crispReplay` ours.
import QtQuick

Item {
    visible: false
    // Rebindable in Ayarlar → Kısayollar (action "replay", default F9).
    Shortcut {
        sequence: veyra.shortcuts.replay || ""
        autoRepeat: false   // a held key must not save at the repeat rate
        onActivated: crispReplay.save(veyra.recordingDirectory)
    }
    // The stream codec follows the recording setting (H.264 / HEVC).
    Binding { target: crispReplay; property: "hevc"; value: veyra.recordingHevc }
    Connections {
        target: crispReplay
        // The bridge's notice signal is what Main.qml shows as a toast.
        function onSaved(fileName, path, seconds) {
            veyra.notice("Anlık tekrar kaydedildi (" + Math.round(seconds) + " sn): " + fileName, false)
        }
        function onFailed(message) { veyra.notice(message, true) }
    }
}
