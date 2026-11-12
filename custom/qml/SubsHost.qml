// Game subtitle translation: toggle shortcut and status notice. Loaded from
// ReplayHost.qml (our own file), so qml/Veyra/Main.qml needs no extra hook.
// `veyra` is the upstream bridge, `crispSubs` ours.
import QtQuick

Item {
    visible: false
    // Rebindable in Ayarlar → Kısayollar (action "subs", default F10).
    Shortcut {
        sequence: veyra.shortcuts.subs || ""
        autoRepeat: false
        onActivated: crispSubs.toggle()
    }
    Connections {
        target: crispSubs
        // The bridge's notice signal is what Main.qml shows as a toast.
        function onNotice(message, error) { veyra.notice(message, error) }
    }
}
