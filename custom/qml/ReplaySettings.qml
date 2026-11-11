// Instant replay settings card. Loaded by one VEYRA_CUSTOM line in
// qml/Veyra/SettingsPage.qml (Oynatma), right under the recording card.
import QtQuick
import QtQuick.Layouts
import Veyra

VGroup {
    objectName: "set-replay"
    VRow {
        label: "Anlık tekrar"
        hint: "Son saniyeleri bellekte tutar; " + (veyra.shortcuts.replay || "kısayol atanmamış")
              + " ile kayıt klasörüne MP4 olarak yazar. Açıkken video kodlayıcı sürekli çalışır."
        VSwitch {
            objectName: "set-replay-enabled"
            checked: crispReplay.enabled
            onToggled: v => crispReplay.enabled = v
        }
    }
    VRow {
        label: "Tekrar süresi"
        hint: "Saniye. Uzun süre daha çok bellek kullanır (en fazla 512 MB)"
        VSeg {
            objectName: "set-replay-seconds"
            options: [{ id: "15", label: "15" }, { id: "30", label: "30" }, { id: "60", label: "60" }, { id: "120", label: "120" }]
            current: String(crispReplay.seconds)
            onPicked: id => crispReplay.seconds = Number(id)
        }
    }
    VRow {
        label: "Durum"
        hint: crispReplay.summary
        VButton {
            objectName: "set-replay-save"
            text: crispReplay.saving ? "Kaydediliyor…" : "Şimdi kaydet"
            enabled: crispReplay.enabled && !crispReplay.saving
            onClicked: crispReplay.save(veyra.recordingDirectory)
        }
    }
}
