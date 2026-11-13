// Region picker ("Çevrilecek alanı seç"): a still of the current video frame
// with the translated region on it. Draw a new rectangle by dragging, move it
// from inside, resize it from its edges and corners, or pick a preset; Kaydet
// stores it (whole percent of the frame, as the settings file does).
//
// Opened by SubsHost.qml (player bar menu, settings tab). The geometry lives in
// C++ (custom/subs/app/src/RegionEdit.*, unit tested); the still comes from
// image://crispsubs/frame (one full-frame copy of the presented picture). While
// this window is open the edited region is also outlined on the live video.
import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Veyra

Window {
    id: win
    objectName: "subs-region-picker"
    title: "Çevrilecek alanı seç"
    width: 980
    height: 720
    minimumWidth: 720
    minimumHeight: 560
    color: Theme.dialog
    flags: Qt.Dialog
    modality: Qt.ApplicationModal

    // The region being edited, percent of the frame: { left, top, right, bottom }.
    property var region: ({ left: 14, top: 62, right: 86, bottom: 97 })
    readonly property string presetId: crispSubs.presetIdOf(region)
    readonly property var rounded: ({ left: Math.round(region.left), top: Math.round(region.top),
                                      right: Math.round(region.right), bottom: Math.round(region.bottom) })
    // For the test hook (SubsHost.qml): the picture's place on screen.
    readonly property Item stage: frameBox

    function openPicker() {
        region = { left: crispSubs.regionLeft, top: crispSubs.regionTop, right: crispSubs.regionRight, bottom: crispSubs.regionBottom }
        crispSubs.captureFrame()
        show()
        raise()
        requestActivate()
    }
    function save() {
        crispSubs.saveRegion(region)
        close()
    }
    onRegionChanged: if (visible) crispSubs.setPreviewOutline(true, region)
    onVisibleChanged: crispSubs.setPreviewOutline(visible, region)

    Shortcut { sequence: "Esc"; onActivated: win.close() }
    Shortcut { sequences: ["Return", "Enter"]; onActivated: win.save() }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            VH2 { text: "Çevrilecek alanı seç" }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "Altyazının göründüğü yeri çerçeveleyin: boş bir yerden sürükleyerek yeni bir alan çizin, "
                      + "alanın içinden tutup taşıyın, kenar ve köşelerinden boyutlandırın. Yalnız bu alan okunur ve çevrilir."
                color: Theme.t2
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsBody
            }
        }

        // The picture.
        Rectangle {
            id: stageBg
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 12
            color: "#000000"
            border.width: 1
            border.color: Theme.stroke

            Item {
                id: frameBox
                objectName: "subs-region-stage"
                readonly property real aspect: crispSubs.frameWidth > 0 && crispSubs.frameHeight > 0
                                               ? crispSubs.frameWidth / crispSubs.frameHeight : 16 / 9
                width: Math.max(16, Math.min(parent.width - 24, (parent.height - 24) * aspect))
                height: width / aspect
                anchors.centerIn: parent

                readonly property real rx: win.region.left / 100 * width
                readonly property real ry: win.region.top / 100 * height
                readonly property real rw: (win.region.right - win.region.left) / 100 * width
                readonly property real rh: (win.region.bottom - win.region.top) / 100 * height

                Image {
                    anchors.fill: parent
                    visible: crispSubs.frameReady
                    source: crispSubs.frameReady ? "image://crispsubs/frame?" + crispSubs.frameSerial : ""
                    cache: false
                    smooth: true
                    fillMode: Image.Stretch
                }
                // Nothing playing: a neutral stand-in, the region can still be drawn.
                Rectangle {
                    anchors.fill: parent
                    visible: !crispSubs.frameReady
                    color: "#16161A"
                    Grid {
                        anchors.fill: parent
                        columns: 4; rows: 4
                        Repeater {
                            model: 16
                            Rectangle {
                                width: parent.width / 4; height: parent.height / 4
                                color: "transparent"
                                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.04)
                            }
                        }
                    }
                    Column {
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: -parent.height * 0.18
                        spacing: 6
                        width: parent.width * 0.7
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: crispSubs.frameBusy ? "Görüntü alınıyor…" : "Önizleme yok"
                            color: Theme.t1
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsH3
                        }
                        Text {
                            width: parent.width
                            visible: !crispSubs.frameBusy
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            text: "Bir video ya da oyun kaynağı açıp oynatın, sonra Yenile'ye basın: burada o anki kare görünür. "
                                  + "Alanı önizleme olmadan da çizebilirsiniz."
                            color: Theme.t3
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsBody
                        }
                    }
                }

                // Outside the region, dimmed.
                Rectangle { x: 0; y: 0; width: parent.width; height: frameBox.ry; color: Qt.rgba(0, 0, 0, 0.5) }
                Rectangle { x: 0; y: frameBox.ry + frameBox.rh; width: parent.width; height: parent.height - y; color: Qt.rgba(0, 0, 0, 0.5) }
                Rectangle { x: 0; y: frameBox.ry; width: frameBox.rx; height: frameBox.rh; color: Qt.rgba(0, 0, 0, 0.5) }
                Rectangle { x: frameBox.rx + frameBox.rw; y: frameBox.ry; width: parent.width - x; height: frameBox.rh; color: Qt.rgba(0, 0, 0, 0.5) }

                Rectangle {
                    id: box
                    x: frameBox.rx; y: frameBox.ry; width: frameBox.rw; height: frameBox.rh
                    color: "transparent"
                    border.width: 2
                    border.color: Theme.accent
                }
                // The region's name and size, above the box (inside it at the top edge).
                Rectangle {
                    x: Math.min(box.x, frameBox.width - width)
                    y: box.y >= height + 4 ? box.y - height - 4 : box.y + 4
                    width: tagText.implicitWidth + 14
                    height: tagText.implicitHeight + 6
                    radius: 6
                    color: Theme.accent
                    Text {
                        id: tagText
                        anchors.centerIn: parent
                        text: "Çeviri alanı · " + (win.rounded.right - win.rounded.left) + "% × " + (win.rounded.bottom - win.rounded.top) + "%"
                        color: Theme.accentInk
                        font.family: Theme.fontUi
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                }
                // Handles: corners and edge middles (L 1, T 2, R 4, B 8).
                Repeater {
                    model: [[0, 0], [0.5, 0], [1, 0], [1, 0.5], [1, 1], [0.5, 1], [0, 1], [0, 0.5]]
                    Rectangle {
                        required property var modelData
                        width: 10; height: 10; radius: 2
                        x: box.x + modelData[0] * box.width - width / 2
                        y: box.y + modelData[1] * box.height - height / 2
                        color: Theme.t1
                        border.width: 1
                        border.color: "#000000"
                    }
                }

                MouseArea {
                    id: drag
                    anchors.fill: parent
                    // A little outside too, so the outer handles are easy to catch.
                    anchors.margins: -8
                    hoverEnabled: true
                    preventStealing: true
                    property int mode: 0        // 1 draw, 2 move, 3 resize
                    property int edges: 0
                    property real sx: 0
                    property real sy: 0
                    property var start: null
                    property bool moved: false

                    function pctX(mx) { return (mx + anchors.margins) / frameBox.width * 100 }
                    function pctY(my) { return (my + anchors.margins) / frameBox.height * 100 }
                    function hit(mx, my) {
                        const x = mx + anchors.margins, y = my + anchors.margins, t = 9
                        const l = box.x, r = box.x + box.width, top = box.y, b = box.y + box.height
                        const inX = x >= l - t && x <= r + t, inY = y >= top - t && y <= b + t
                        let e = 0
                        if (inY && Math.abs(x - l) <= t) e |= 1
                        else if (inY && Math.abs(x - r) <= t) e |= 4
                        if (inX && Math.abs(y - top) <= t) e |= 2
                        else if (inX && Math.abs(y - b) <= t) e |= 8
                        if (e) return { mode: 3, edges: e }
                        if (x > l && x < r && y > top && y < b) return { mode: 2, edges: 0 }
                        return { mode: 1, edges: 0 }
                    }
                    function cursorFor(h) {
                        if (h.mode === 2) return Qt.SizeAllCursor
                        if (h.mode === 1) return Qt.CrossCursor
                        if (h.edges === 3 || h.edges === 12) return Qt.SizeFDiagCursor
                        if (h.edges === 6 || h.edges === 9) return Qt.SizeBDiagCursor
                        return (h.edges & 5) ? Qt.SizeHorCursor : Qt.SizeVerCursor
                    }
                    onPressed: mouse => {
                        const h = hit(mouse.x, mouse.y)
                        mode = h.mode; edges = h.edges
                        sx = pctX(mouse.x); sy = pctY(mouse.y)
                        start = win.region
                        moved = false
                        cursorShape = cursorFor(h)
                    }
                    onPositionChanged: mouse => {
                        if (!pressed) { cursorShape = cursorFor(hit(mouse.x, mouse.y)); return }
                        const px = pctX(mouse.x), py = pctY(mouse.y)
                        // A click is not a drag: a few pixels first.
                        if (!moved && Math.abs(px - sx) * frameBox.width < 300 && Math.abs(py - sy) * frameBox.height < 300) return
                        moved = true
                        if (mode === 1) win.region = crispSubs.regionDrag(sx, sy, px, py)
                        else if (mode === 2) win.region = crispSubs.regionMove(start, px - sx, py - sy)
                        else if (mode === 3) win.region = crispSubs.regionResize(start, edges, px - sx, py - sy)
                    }
                    onReleased: mode = 0
                }
            }
        }

        // Still status and refresh.
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: crispSubs.frameNote.length ? crispSubs.frameNote
                      : crispSubs.frameReady ? "Önizleme: " + crispSubs.frameWidth + "×" + crispSubs.frameHeight + " görüntünün o anki karesi"
                      : ""
                color: crispSubs.frameNote.length && !crispSubs.frameBusy ? Theme.warn : Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSmall
            }
            VButton {
                objectName: "subs-region-refresh"
                text: "Yenile"
                iconName: "refresh"
                enabled: !crispSubs.frameBusy
                onClicked: crispSubs.captureFrame()
            }
        }

        // Presets and the numbers.
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Text {
                text: "Hazır alanlar"
                color: Theme.t2
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsBody
            }
            VSeg {
                objectName: "subs-region-presets"
                options: ["default", "half", "full", "top"].map(id => ({ id: id, label: crispSubs.presetLabel(id) }))
                current: win.presetId
                onPicked: id => win.region = crispSubs.regionOfPreset(id)
            }
            Item { Layout.fillWidth: true }
            Text {
                text: (win.presetId === "custom" ? "Özel" : crispSubs.presetLabel(win.presetId))
                      + " · sol %" + win.rounded.left + " · üst %" + win.rounded.top
                      + " · sağ %" + win.rounded.right + " · alt %" + win.rounded.bottom
                color: Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSmall
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            VButton {
                objectName: "subs-region-default"
                text: "Varsayılana dön"
                ghost: true
                onClicked: win.region = crispSubs.regionOfPreset("default")
            }
            Item { Layout.fillWidth: true }
            VButton {
                objectName: "subs-region-cancel"
                text: "Vazgeç"
                onClicked: win.close()
            }
            VButton {
                objectName: "subs-region-save"
                text: "Kaydet"
                primary: true
                onClicked: win.save()
            }
        }
    }
}
