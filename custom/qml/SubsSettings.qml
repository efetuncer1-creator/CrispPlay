// Game subtitle translation settings tab ("Altyazı çevirisi"). Loaded by one
// VEYRA_CUSTOM Loader line in qml/Veyra/SettingsPage.qml; `crispSubs` is the
// controller (custom/subs/app/src/SubsController.h), styled with the Veyra
// components so it matches the page.
import QtQuick
import QtQuick.Layouts
import Veyra

Item {
    id: root
    objectName: "set-subs"
    width: parent ? parent.width : 0
    implicitHeight: col.implicitHeight
    Component.onCompleted: crispSubs.refresh()

    // A single-line input in the page's style; `secret` hides the text (API key).
    component Field: Rectangle {
        property alias text: input.text
        property string placeholder: ""
        property bool secret: false
        signal accepted(string text)
        implicitWidth: 220
        implicitHeight: 30
        radius: 9
        color: Qt.rgba(1, 1, 1, 0.04)
        border.width: 1
        border.color: input.activeFocus ? Theme.accent : Theme.stroke
        TextInput {
            id: input
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            verticalAlignment: TextInput.AlignVCenter
            clip: true
            color: Theme.t1
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
            selectByMouse: true
            echoMode: parent.secret ? TextInput.Password : TextInput.Normal
            onAccepted: parent.accepted(text)
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 10
            visible: input.text.length === 0
            text: parent.placeholder
            color: Theme.t3
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
        }
    }

    ColumnLayout {
        id: col
        width: parent.width
        spacing: 12

        VH1 { text: "Altyazı çevirisi" }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: "Oyunun ekrandaki İngilizce altyazısını okur, Türkçeye çevirir ve İngilizce satırın üstüne yazar. "
                  + "Video dosyası, yakalama kartı, ekran yakalama ve yayın kaynaklarının hepsinde çalışır. "
                  + "Varsayılan olarak her şey bu bilgisayarda yapılır; hiçbir görüntü ya da metin dışarı gönderilmez."
            color: Theme.t2
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
        }

        VGroup {
            VRow {
                label: "Oyun altyazısını Türkçeye çevir"
                hint: "Kısayol: " + (veyra.shortcuts.subs || "atanmamış") + " · Ayarlar → Kısayollar'dan değiştirilir"
                VSwitch {
                    objectName: "set-subs-enabled"
                    checked: crispSubs.enabled
                    onToggled: v => crispSubs.enabled = v
                }
            }
            VRow {
                label: "Kaynak dil"
                hint: "İngilizce → Türkçe. Latin harfli İngilizce altyazılar okunur; başka diller şimdilik desteklenmez."
                hintBelow: true
            }
            VRow {
                label: "Durum"
                hint: crispSubs.summary
                hintBelow: true
            }
            VRow {
                visible: crispSubs.ocrWarning.length > 0
                label: "Windows OCR dil paketi"
                hint: crispSubs.ocrWarning
                hintColor: Theme.warn
                hintBelow: true
            }
            VRow {
                visible: crispSubs.engineWarning.length > 0
                label: "Çeviri motoru"
                hint: crispSubs.engineWarning
                hintColor: Theme.warn
                hintBelow: true
            }
        }

        VH2 { text: "Okunan bölge" }
        VGroup {
            VRow {
                label: "Altyazının yeri"
                hint: "Yalnız bu bölge kopyalanıp okunur. Dar bölge daha hızlıdır ve harita, görev yazısı gibi metinleri daha az yakalar."
                hintBelow: true
                VSeg {
                    objectName: "set-subs-region"
                    options: [
                        { id: "default", label: "Alt orta" },
                        { id: "wide", label: "Alt geniş" },
                        { id: "low", label: "Alt kenar" },
                        { id: "top", label: "Üst" },
                        { id: "custom", label: "Özel" }
                    ]
                    // Presets only the picker offers (Alt yarı, Tüm ekran) show as "Özel" here.
                    current: ["default", "wide", "low", "top"].indexOf(crispSubs.regionPreset) >= 0 ? crispSubs.regionPreset : "custom"
                    onPicked: id => { if (id === "custom") crispSubs.openRegionPicker(); else crispSubs.regionPreset = id }
                }
            }
            VRow {
                label: "Bölgeyi görüntü üzerinde seç"
                hint: "Şu an: " + crispSubs.regionLabel
                      + (crispSubs.regionPreset === "custom" ? "" : " · sol %" + crispSubs.regionLeft + " · üst %" + crispSubs.regionTop
                         + " · sağ %" + crispSubs.regionRight + " · alt %" + crispSubs.regionBottom)
                      + ". Oynatılan videonun o anki karesi üzerinde alanı çizin, taşıyın ya da boyutlandırın."
                hintBelow: true
                VButton {
                    objectName: "set-subs-region-pick"
                    text: "Alanı seç…"
                    iconName: "scan"
                    onClicked: crispSubs.openRegionPicker()
                }
            }
        }

        VH2 { text: "Türkçe satır" }
        VGroup {
            VRow {
                label: "Yazı boyutu"
                hint: "Türkçe satır İngilizce altyazının üstünü kapatan koyu bir kutuya yazılır"
                VSeg {
                    objectName: "set-subs-size"
                    options: [{ id: "85", label: "Küçük" }, { id: "100", label: "Normal" }, { id: "120", label: "Büyük" }, { id: "140", label: "Çok büyük" }]
                    current: String(crispSubs.overlaySize)
                    onPicked: id => crispSubs.overlaySize = Number(id)
                }
            }
            VRow {
                label: "Kutu opaklığı"
                hint: "Düşük değerde İngilizce satır kutunun arkasından seçilebilir"
                VSeg {
                    objectName: "set-subs-opacity"
                    options: [{ id: "75", label: "%75" }, { id: "85", label: "%85" }, { id: "92", label: "%92" }, { id: "100", label: "%100" }]
                    current: String(crispSubs.coverOpacity)
                    onPicked: id => crispSubs.coverOpacity = Number(id)
                }
            }
        }

        VH2 { text: "Çeviri motoru" }
        VGroup {
            VRow {
                label: "Motor"
                hint: crispSubs.online
                      ? "Çevrimiçi: okunan altyazı cümleleri (yalnız metin) seçtiğiniz servise gönderilir. Yanıt gelmezse yerel motor kullanılır."
                      : "Yerel: çeviri bu bilgisayarda OPUS-MT ile yapılır, internet gerekmez, hiçbir şey gönderilmez."
                hintBelow: true
                VSeg {
                    objectName: "set-subs-engine"
                    options: [{ id: "local", label: "Yerel (OPUS-MT)" }, { id: "online", label: "Çevrimiçi" }]
                    current: crispSubs.online ? "online" : "local"
                    onPicked: id => crispSubs.online = id === "online"
                }
            }
            VRow {
                label: "Yerel model ve OCR ağı"
                hint: crispSubs.modelSummary
                hintColor: crispSubs.models.state === 3 ? Theme.warn : Theme.t3
                hintBelow: true
                VButton {
                    objectName: "set-subs-download"
                    visible: crispSubs.models.state !== 2
                    text: crispSubs.models.state === 1 ? "İptal" : (crispSubs.models.state === 3 ? "Yeniden dene" : "İndir")
                    onClicked: crispSubs.models.state === 1 ? crispSubs.models.cancel() : crispSubs.models.download()
                }
            }
            VRow {
                visible: crispSubs.models.state === 1 && crispSubs.models.bytesTotal > 0
                label: "İndirme"
                Rectangle {
                    implicitWidth: 220; implicitHeight: 6; radius: 3
                    color: Qt.rgba(1, 1, 1, 0.08)
                    Rectangle {
                        height: parent.height; radius: 3; color: Theme.accent
                        width: parent.width * Math.max(0, Math.min(1, crispSubs.models.bytesDone / Math.max(1, crispSubs.models.bytesTotal)))
                    }
                }
            }
        }

        VGroup {
            visible: crispSubs.online
            VRow {
                label: "Servis"
                hint: "Varsayılan kapalıdır. Görüntü hiçbir zaman gönderilmez; yalnız çevrilecek İngilizce cümle gider."
                hintBelow: true
                VSeg {
                    id: provider
                    objectName: "set-subs-provider"
                    options: [{ id: "1", label: "DeepL" }, { id: "2", label: "OpenAI uyumlu" }]
                    current: String(crispSubs.onlineProvider)
                    onPicked: id => crispSubs.saveOnline(Number(id), endpoint.text, model.text)
                }
            }
            VRow {
                visible: crispSubs.onlineProvider === 2
                label: "Adres (base URL)"
                hint: "Örn. https://api.openai.com/v1 ya da kendi sunucunuz"
                Field {
                    id: endpoint
                    implicitWidth: 300
                    text: crispSubs.onlineEndpoint
                    placeholder: "https://…/v1"
                    onAccepted: t => crispSubs.saveOnline(crispSubs.onlineProvider, t, model.text)
                }
            }
            VRow {
                visible: crispSubs.onlineProvider === 2
                label: "Model"
                Field {
                    id: model
                    implicitWidth: 220
                    text: crispSubs.onlineModel
                    placeholder: "model adı"
                    onAccepted: t => crispSubs.saveOnline(crispSubs.onlineProvider, endpoint.text, t)
                }
            }
            VRow {
                label: "API anahtarı"
                hint: crispSubs.onlineHasKey ? "Kayıtlı. Windows hesabınıza şifreli saklanır (DPAPI); düz metin olarak yazılmaz."
                                             : "Kayıtlı anahtar yok"
                hintBelow: true
                RowLayout {
                    spacing: 6
                    Field { id: key; implicitWidth: 220; secret: true; placeholder: "anahtarı yapıştırın" }
                    VButton {
                        objectName: "set-subs-key-save"
                        text: "Kaydet"
                        onClicked: {
                            crispSubs.saveOnline(crispSubs.onlineProvider, endpoint.text, model.text)
                            crispSubs.saveApiKey(key.text)
                            key.text = ""
                        }
                    }
                    VButton {
                        visible: crispSubs.onlineHasKey
                        text: "Sil"
                        onClicked: crispSubs.clearApiKey()
                    }
                }
            }
            VRow {
                label: "Bağlantı denemesi"
                hint: crispSubs.onlineTest.length ? crispSubs.onlineTest : "Tek bir örnek cümle (\"Where are we going?\") gönderilir"
                hintBelow: true
                VButton {
                    objectName: "set-subs-test"
                    text: crispSubs.onlineTesting ? "Deneniyor…" : "Dene"
                    enabled: !crispSubs.onlineTesting
                    onClicked: {
                        crispSubs.saveOnline(crispSubs.onlineProvider, endpoint.text, model.text)
                        crispSubs.testOnline()
                    }
                }
            }
        }
    }
}
