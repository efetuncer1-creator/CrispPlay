// Game subtitle translation: toggle shortcut, status notice, the region picker
// and the way to the settings tab. Loaded from ReplayHost.qml (our own file),
// so qml/Veyra/Main.qml needs no extra hook. `veyra` is the upstream bridge,
// `crispSubs` ours; `root`, `settingsPage` and `fullBar` are Main.qml's ids.
import QtQuick

Item {
    id: host
    visible: false
    // Rebindable in Ayarlar → Kısayollar (action "subs", default F10).
    Shortcut {
        sequence: veyra.shortcuts.subs || ""
        autoRepeat: false
        onActivated: crispSubs.toggle()
    }
    // Test hook for screenshots: open Ayarlar → Altyazı çevirisi (settingsPage is Main.qml's id).
    Timer {
        interval: 600; running: crispSubs.testOpenSettings
        onTriggered: if (typeof settingsPage !== "undefined") settingsPage.section = "subs"
    }

    // Region picker (its own window), built on first use.
    Loader {
        id: picker
        active: false
        source: "qrc:/crispplay/SubsRegionPicker.qml"
    }
    function openPicker() {
        picker.active = true
        if (picker.item) picker.item.openPicker()
    }
    function openSettings() {
        if (typeof settingsPage !== "undefined") settingsPage.section = "subs"
        if (typeof root !== "undefined" && root.goPage) {
            if (root.fullscreen) root.toggleFullscreen()
            root.goPage("set")
        }
    }

    Connections {
        target: crispSubs
        // The bridge's notice signal is what Main.qml shows as a toast.
        function onNotice(message, error) { veyra.notice(message, error) }
        function onRegionPickerRequested() { host.openPicker() }
        function onSettingsRequested() { host.openSettings() }
    }

    // ---- test hooks (CRISPPLAY_TEST_SUBS_UI, custom/scripts/subs-test.ps1) ----------
    // "menu": open the player bar's subtitle menu and log where our items are on
    // screen (the script clicks them with the real mouse). "picker": open the
    // picker and log where its picture is. Never set in normal use.
    function findItem(item, test) {
        if (!item) return null
        if (test(item)) return item
        for (let i = 0; i < item.children.length; ++i) {
            const found = findItem(item.children[i], test)
            if (found) return found
        }
        return null
    }
    function logRect(what, item) {
        const p = item.mapToGlobal(0, 0)
        veyra.logUi("subs-ui", what + " " + Math.round(p.x) + " " + Math.round(p.y) + " " + Math.round(item.width) + " " + Math.round(item.height))
    }
    function logMenu() {
        // The menu's popup lives in the pill window's overlay (a child of its content item).
        const labels = []
        findItem(fullBar.contentItem, it => { if (it.objectName === "vmenu-option-label" && it.visible) labels.push(it); return false })
        for (const label of labels) {
            const row = label.parent.parent.parent   // Text -> ColumnLayout -> RowLayout -> option Rectangle
            logRect("item " + label.text.replace(/ /g, "_"), row)
        }
    }
    function logPicker() {
        if (!picker.item || !picker.item.visible) return
        logRect("picker stage", picker.item.stage)
        findItem(picker.item.contentItem, it => {
            if (it.objectName.startsWith("subs-region-") && it.objectName !== "subs-region-stage") logRect("button " + it.objectName, it)
            return false
        })
    }
    Timer {
        id: testTimer
        property int step: 0
        running: crispSubs.testUi.length > 0
        interval: 2500
        repeat: true
        onTriggered: {
            const what = crispSubs.testUi
            ++step
            if (picker.item && picker.item.visible) { host.logPicker(); return }
            // "pick:<what>": the same call CineBar makes for one of our menu items (no pointer needed).
            if (what.startsWith("pick:")) { if (step === 3) crispSubs.menuPicked({ crisp: what.substring(5) }); return }
            if (what.startsWith("menu")) {
                // Only on the player page, where the pill and its menu are shown.
                if (typeof fullBar === "undefined" || typeof root === "undefined" || root.page !== "min") return
                const cc = host.findItem(fullBar.contentItem, it => it.glyph === "cc" && it.tapped !== undefined)
                // Re-open whenever the menu is closed (after each scripted click), then log the rows.
                if (cc && !fullBar.menuOpen) { cc.tapped(); testLog.restart() }
            } else if (what === "picker-paused") {
                // Pause first: the still must come from a paused picture too.
                if (step === 2) veyra.togglePlayPause()
                else if (step === 3) host.openPicker()
            } else if (what.startsWith("picker") && step === 1) {
                host.openPicker()
            }
        }
    }
    Timer {
        id: testLog
        interval: 700
        onTriggered: {
            if (picker.item && picker.item.visible) host.logPicker()
            else if (crispSubs.testUi.startsWith("menu")) host.logMenu()
        }
    }
    Connections {
        target: picker.item
        enabled: crispSubs.testUi.length > 0
        function onVisibleChanged() { if (picker.item.visible) testLog.restart() }
    }
}
