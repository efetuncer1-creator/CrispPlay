import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Veyra

// Discord Rich Presence setting (CrispPlay). Loaded by one Loader hook in
// SettingsPage.qml; styled with the Veyra components so it matches the page.
Item {
    id: root
    width: parent ? parent.width : 0
    implicitHeight: col.implicitHeight

    ColumnLayout {
        id: col
        width: parent.width
        spacing: 12

        VH2 { text: "Discord" }

        VGroup {
            VRow {
                visible: !crispDiscord.appIdConfigured
                label: "Discord durumu (Rich Presence)"
                hint: "Bu sürümde Discord uygulama kimliği tanımlı değil, bu yüzden özellik kapalı."
                hintBelow: true
            }
            VRow {
                visible: crispDiscord.appIdConfigured
                label: "Discord durumu (Rich Presence)"
                hint: "Ne izlediğini Discord profilinde gösterir. Dosya adı, yol ve başlık hiçbir seviyede gönderilmez."
                hintBelow: true
                VSeg {
                    objectName: "set-discord-level"
                    options: [
                        { id: "0", label: "Kapalı" },
                        { id: "1", label: "Temel" },
                        { id: "2", label: "Ayrıntılı" }
                    ]
                    current: String(crispDiscord.level)
                    onPicked: id => crispDiscord.level = Number(id)
                }
            }
            VRow {
                visible: crispDiscord.appIdConfigured && crispDiscord.level > 0
                label: crispDiscord.level === 1
                       ? "Temel: yalnızca \"CrispPlay ile izliyor\" ve geçen süre."
                       : "Ayrıntılı: ek olarak kaynak türü (video dosyası, yakalama kartı, PS5 yayını…) ve açık iyileştirmeler (SR, NR, FG)."
                hint: crispDiscord.status
                hintBelow: true
            }
        }
    }
}
