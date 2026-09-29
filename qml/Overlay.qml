import QtQuick
import ScreenshotInternals

Item {
    id: root
    clip: true
    focus: true
    property int screenIndex: -1
    property rect screenRect: Qt.rect(0, 0, 0, 0)

    readonly property var activeRect: captureController.selection.width > 0
                                      ? captureController.selection : captureController.hovered
    readonly property int annotationCount: captureController.annotations.length
    readonly property bool captureReady: screenImage.status === Image.Ready
    readonly property bool editorFocused: textInput.activeFocus
    readonly property bool mosaicDraftBorderVisible: mosaicDraftBorder.visible
    readonly property int toolbarButtonCount: toolbarRepeater.count
    readonly property string editorText: editor.visible ? textInput.text : ""
    readonly property int editorRows: editor.visible ? textInput.lineCount : 0
    readonly property real textLineHeight: textMetrics.lineSpacing
    readonly property real localX: activeRect.x - screenRect.x
    readonly property real localY: activeRect.y - screenRect.y
    readonly property real holeLeft: activeRect.width > 0 ? Math.max(0, Math.min(width, localX)) : 0
    readonly property real holeTop: activeRect.height > 0 ? Math.max(0, Math.min(height, localY)) : 0
    readonly property real holeRight: activeRect.width > 0 ? Math.max(0, Math.min(width, localX + activeRect.width)) : 0
    readonly property real holeBottom: activeRect.height > 0 ? Math.max(0, Math.min(height, localY + activeRect.height)) : 0
    property real textX: 0
    property real textY: 0
    property string toolbarTooltipText: ""
    property real toolbarTooltipX: 0
    property real toolbarTooltipY: 0
    property bool toolbarTooltipVisible: false

    FontMetrics {
        id: textMetrics
        font.pixelSize: 22
        font.bold: true
    }

    function previewState() {
        let textItem = textRepeater.itemAt(1)
        return {
            mosaicExists: mosaicOverlay.hasMosaic,
            mosaicVisible: mosaicOverlay.visible && mosaicOverlay.hasMosaic,
            mosaicWidth: mosaicOverlay.width,
            mosaicImageReady: mosaicOverlay.imageReady,
            textExists: textItem !== null,
            textVisible: textItem !== null && textItem.visible,
            textValue: textItem !== null ? textItem.text : "",
            textRows: textItem !== null ? textItem.lines.length : 0,
            firstLineCenter: textItem !== null ? textItem.lineCenter(0) : -1,
            secondLineCenter: textItem !== null ? textItem.lineCenter(1) : -1,
            textLineHeight: root.textLineHeight
        }
    }

    function toolbarButtonCenter(index) {
        let button = toolbarRepeater.itemAt(index)
        if (button === null) return {x: -1, y: -1}
        let point = button.mapToItem(root, button.width / 2, button.height / 2)
        return {x: point.x, y: point.y}
    }

    function colorPresetCenter(index) {
        let swatch = colorPresetRepeater.itemAt(index)
        if (swatch === null) return {x: -1, y: -1}
        let point = swatch.mapToItem(root, swatch.width / 2, swatch.height / 2)
        return {x: point.x, y: point.y}
    }

    function visibleResizeHandleCount() {
        let count = 0
        for (let i = 0; i < resizeHandles.count; ++i) {
            let handle = resizeHandles.itemAt(i)
            if (handle !== null && handle.visible) ++count
        }
        return count
    }

    function toolbarHasAction(action) {
        for (let i = 0; i < toolbarRepeater.count; ++i) {
            let button = toolbarRepeater.itemAt(i)
            if (button !== null && button.modelData.action === action) return true
        }
        return false
    }

    function showToolbarTooltip(hint, button) {
        let point = button.mapToItem(root, button.width / 2, 0)
        toolbarTooltipText = hint
        toolbarTooltipX = point.x
        toolbarTooltipY = point.y
        toolbarTooltipVisible = false
        tooltipDelay.restart()
    }

    function hideToolbarTooltip() {
        tooltipDelay.stop()
        toolbarTooltipVisible = false
    }

    Timer {
        id: tooltipDelay
        interval: 450
        onTriggered: root.toolbarTooltipVisible = true
    }

    Keys.onPressed: event => {
        if (captureController.scrolling) {
            if (event.key === Qt.Key_Escape) captureController.cancelScrollCapture()
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                captureController.finishScrollCapture()
            else return
            event.accepted = true
            return
        }
        if (colorPanel.visible) {
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Q) {
                colorPanel.visible = false
                root.forceActiveFocus()
                event.accepted = true
            }
            return
        }
        if (event.key === Qt.Key_Escape) captureController.cancel()
        else if (captureController.selected && !captureController.hasScrollImage &&
                 captureController.tool === "select" &&
                 (event.key === Qt.Key_Up || event.key === Qt.Key_Down ||
                  event.key === Qt.Key_Left || event.key === Qt.Key_Right) &&
                 (event.modifiers === Qt.NoModifier || event.modifiers === Qt.ShiftModifier))
            captureController.adjustSelectionEdge(event.key, (event.modifiers & Qt.ShiftModifier) !== 0)
        else if (captureController.selected && event.key === Qt.Key_C &&
                 (event.modifiers & Qt.ShiftModifier)) captureController.startAutoScrollCapture(false)
        else if (captureController.selected && event.key === Qt.Key_V &&
                 (event.modifiers & Qt.ShiftModifier)) captureController.startAutoScrollCapture(true)
        else if (captureController.selected && event.key === Qt.Key_C) captureController.copy()
        else if (captureController.selected && event.key === Qt.Key_S &&
                 (event.modifiers & Qt.ShiftModifier)) captureController.startScrollCapture(false)
        else if (captureController.selected && event.key === Qt.Key_G &&
                 (event.modifiers & Qt.ShiftModifier)) captureController.startScrollCapture(true)
        else if (captureController.selected && event.key === Qt.Key_S) captureController.save()
        else if (captureController.selected && event.key === Qt.Key_F) captureController.ocr()
        else if (captureController.selected && event.key === Qt.Key_Z &&
                 (event.modifiers & Qt.ShiftModifier)) captureController.redo()
        else if (captureController.selected && event.key === Qt.Key_Z) captureController.undo()
        else if (event.key === Qt.Key_X) captureController.cancel()
        else if (captureController.selected && event.key === Qt.Key_Q) colorPanel.visible = true
        else if (captureController.selected && event.key === Qt.Key_V) captureController.tool = "select"
        else if (captureController.selected && event.key === Qt.Key_R &&
                 (event.modifiers & Qt.ShiftModifier)) captureController.tool = "roundrect"
        else if (captureController.selected && event.key === Qt.Key_E &&
                 (event.modifiers & Qt.ShiftModifier)) captureController.tool = "fillellipse"
        else if (captureController.selected && event.key === Qt.Key_A &&
                 (event.modifiers & Qt.ShiftModifier)) captureController.tool = "curvedarrow"
        else if (captureController.selected && event.key === Qt.Key_D &&
                 (event.modifiers & Qt.ShiftModifier)) captureController.tool = "fillrect"
        else if (captureController.selected && event.key === Qt.Key_T &&
                 (event.modifiers & Qt.ShiftModifier)) captureController.tool = "marker"
        else if (captureController.selected && event.key === Qt.Key_B &&
                 (event.modifiers & Qt.ShiftModifier)) captureController.tool = "spotlight"
        else if (captureController.selected && event.key === Qt.Key_W &&
                 (event.modifiers & Qt.ShiftModifier)) captureController.tool = "doublearrow"
        else if (captureController.selected && event.key === Qt.Key_W) captureController.tool = "line"
        else if (captureController.selected && event.key === Qt.Key_B) captureController.tool = "highlighter"
        else if (captureController.selected && event.key === Qt.Key_R) captureController.tool = "rect"
        else if (captureController.selected && event.key === Qt.Key_E) captureController.tool = "ellipse"
        else if (captureController.selected && event.key === Qt.Key_A) captureController.tool = "arrow"
        else if (captureController.selected && event.key === Qt.Key_D) captureController.tool = "pen"
        else if (captureController.selected && event.key === Qt.Key_T) captureController.tool = "text"
        else if (captureController.selected && event.key === Qt.Key_G) captureController.tool = "mosaic"
        else return
        event.accepted = true
    }

    Image {
        id: screenImage
        objectName: "screenCaptureImage"
        anchors.fill: parent
        source: screenIndex >= 0 ? "image://captures/screen/" + screenIndex : ""
        fillMode: Image.Stretch
        smooth: true
        cache: true
        visible: !(captureController.scrolling && captureController.scrollMonitor === screenIndex)
    }

    Item {
        x: root.localX
        y: root.localY
        width: captureController.selection.width
        height: captureController.selection.height
        clip: true
        visible: !captureController.scrolling && captureController.hasScrollImage
                 && captureController.scrollMonitor === screenIndex
        ScrollPreview {
            anchors.fill: parent
            controller: captureController
            screenIndex: root.screenIndex
        }
    }

    Rectangle {
        visible: captureController.hasScrollImage && !captureController.scrolling &&
                 captureController.scrollMonitor === screenIndex &&
                 captureController.scrollDocumentHeight > captureController.selection.height
        z: 7
        x: root.localX + captureController.selection.width - width - 3
        y: root.localY + captureController.scrollOffset /
           captureController.scrollDocumentHeight * captureController.selection.height
        width: 5
        height: Math.max(24, captureController.selection.height *
                         captureController.selection.height /
                         captureController.scrollDocumentHeight)
        radius: 2
        color: "#cceef5ff"
    }

    Rectangle { x: 0; y: 0; width: root.width; height: root.holeTop; color: "#85000000" }
    Rectangle { x: 0; y: root.holeBottom; width: root.width; height: root.height - y; color: "#85000000" }
    Rectangle { x: 0; y: root.holeTop; width: root.holeLeft; height: root.holeBottom - y; color: "#85000000" }
    Rectangle { x: root.holeRight; y: root.holeTop; width: root.width - x; height: root.holeBottom - y; color: "#85000000" }

    Rectangle {
        visible: root.activeRect.width > 0 && root.activeRect.height > 0
        x: root.localX
        y: root.localY
        width: root.activeRect.width
        height: root.activeRect.height
        color: "transparent"
        border.color: captureController.selected ? "#ffffff" : "#59bdff"
        border.width: 2
        opacity: captureController.scrolling ? 0 : 1
    }

    Rectangle {
        visible: captureController.scrolling && captureController.scrollMonitor === screenIndex
        x: root.localX - 2
        y: root.localY - 2
        width: captureController.selection.width + 4
        height: captureController.selection.height + 4
        color: "transparent"
        border.color: "#59bdff"
        border.width: 2
    }

    MosaicOverlay {
        id: mosaicOverlay
        objectName: "mosaicOverlay"
        anchors.fill: parent
        controller: captureController
        screenIndex: root.screenIndex
        visible: captureController.selected && !captureController.scrolling
    }

    Rectangle {
        id: mosaicDraftBorder
        objectName: "mosaicDraftBorder"
        readonly property var mark: captureController.draft
        readonly property bool active: mark.type === "mosaic" && mark.start && mark.end
        x: active ? Math.min(mark.start.x, mark.end.x) - screenRect.x : 0
        y: active ? Math.min(mark.start.y, mark.end.y) - screenRect.y : 0
        width: active ? Math.abs(mark.end.x - mark.start.x) : 0
        height: active ? Math.abs(mark.end.y - mark.start.y) : 0
        visible: active && width >= 2 && height >= 2
        color: "transparent"
        border.color: "#ff4b55"
        border.width: 2
    }

    Canvas {
        id: marks
        objectName: "marksCanvas"
        anchors.fill: parent
        visible: captureController.selected && !captureController.scrolling
        antialiasing: true

        function drawShape(context, item) {
            if (!item || !item.type) return
            if (item.type === "mosaic" || item.type === "text") return
            let sx = item.start.x - screenRect.x
            let sy = item.start.y - screenRect.y -
                     (captureController.hasScrollImage ? captureController.scrollOffset : 0)
            let ex = item.end ? item.end.x - screenRect.x : sx
            let ey = item.end ? item.end.y - screenRect.y -
                     (captureController.hasScrollImage ? captureController.scrollOffset : 0) : sy
            context.save()
            context.strokeStyle = item.color || "#ff4b55"
            context.fillStyle = item.color || "#ff4b55"
            context.lineWidth = 3
            context.lineCap = "round"
            context.lineJoin = "round"
            context.beginPath()
            if (item.type === "rect") context.rect(sx, sy, ex - sx, ey - sy)
            else if (item.type === "roundrect") {
                let left = Math.min(sx, ex), top = Math.min(sy, ey)
                let w = Math.abs(ex - sx), h = Math.abs(ey - sy)
                let r = Math.min(10, w / 2, h / 2)
                context.moveTo(left + r, top)
                context.lineTo(left + w - r, top)
                context.quadraticCurveTo(left + w, top, left + w, top + r)
                context.lineTo(left + w, top + h - r)
                context.quadraticCurveTo(left + w, top + h, left + w - r, top + h)
                context.lineTo(left + r, top + h)
                context.quadraticCurveTo(left, top + h, left, top + h - r)
                context.lineTo(left, top + r)
                context.quadraticCurveTo(left, top, left + r, top)
                context.closePath()
            }
            else if (item.type === "fillrect") {
                context.fillRect(Math.min(sx, ex), Math.min(sy, ey),
                                 Math.abs(ex - sx), Math.abs(ey - sy))
                context.restore()
                return
            }
            else if (item.type === "fillellipse") {
                context.ellipse(Math.min(sx, ex), Math.min(sy, ey),
                                Math.abs(ex - sx), Math.abs(ey - sy))
                context.fill()
                context.restore()
                return
            }
            else if (item.type === "ellipse") context.ellipse(Math.min(sx, ex), Math.min(sy, ey),
                Math.abs(ex - sx), Math.abs(ey - sy))
            else if (item.type === "line") {
                context.moveTo(sx, sy)
                context.lineTo(ex, ey)
            }
            else if (item.type === "arrow") {
                context.moveTo(sx, sy)
                context.lineTo(ex, ey)
                let a = Math.atan2(ey - sy, ex - sx)
                context.moveTo(ex, ey)
                context.lineTo(ex - Math.cos(a - 0.55) * 12, ey - Math.sin(a - 0.55) * 12)
                context.moveTo(ex, ey)
                context.lineTo(ex - Math.cos(a + 0.55) * 12, ey - Math.sin(a + 0.55) * 12)
            } else if (item.type === "curvedarrow" || item.type === "doublearrow") {
                let cx = (sx + ex) / 2 - (ey - sy) * 0.22
                let cy = (sy + ey) / 2 + (ex - sx) * 0.22
                context.moveTo(sx, sy)
                context.quadraticCurveTo(cx, cy, ex, ey)
                function head(tx, ty, fx, fy) {
                    let angle = Math.atan2(ty - fy, tx - fx)
                    context.moveTo(tx, ty)
                    context.lineTo(tx - Math.cos(angle - 0.55) * 12,
                                   ty - Math.sin(angle - 0.55) * 12)
                    context.moveTo(tx, ty)
                    context.lineTo(tx - Math.cos(angle + 0.55) * 12,
                                   ty - Math.sin(angle + 0.55) * 12)
                }
                head(ex, ey, cx, cy)
                if (item.type === "doublearrow") head(sx, sy, cx, cy)
            } else if (item.type === "marker") {
                context.arc(sx, sy, 13, 0, Math.PI * 2)
                context.fill()
                context.fillStyle = "white"
                context.font = "bold 14px sans-serif"
                context.textAlign = "center"
                context.textBaseline = "middle"
                context.fillText(item.number || "", sx, sy)
                context.restore()
                return
            } else if (item.type === "pen" || item.type === "highlighter") {
                if (item.type === "highlighter") {
                    context.globalAlpha = 0.34
                    context.lineWidth = 18
                }
                let points = captureController.smoothedFreehandPoints(item.points)
                if (points.length === 0) { context.restore(); return }
                context.moveTo(points[0].x - screenRect.x,
                               points[0].y - screenRect.y - captureController.scrollOffset)
                if (points.length === 2) {
                    context.lineTo(points[1].x - screenRect.x,
                                   points[1].y - screenRect.y - captureController.scrollOffset)
                } else if (points.length > 2) {
                    for (let i = 1; i + 1 < points.length; ++i) {
                        let control = points[i]
                        let next = points[i + 1]
                        context.quadraticCurveTo(control.x - screenRect.x,
                                                 control.y - screenRect.y - captureController.scrollOffset,
                                                 (control.x + next.x) / 2 - screenRect.x,
                                                 (control.y + next.y) / 2 - screenRect.y - captureController.scrollOffset)
                    }
                    let last = points[points.length - 1]
                    context.quadraticCurveTo(last.x - screenRect.x,
                                             last.y - screenRect.y - captureController.scrollOffset,
                                             last.x - screenRect.x,
                                             last.y - screenRect.y - captureController.scrollOffset)
                }
            }
            context.stroke()
            context.restore()
        }

        onPaint: {
            let context = getContext("2d")
            context.clearRect(0, 0, width, height)
            context.save()
            context.beginPath()
            context.rect(root.localX, root.localY,
                         captureController.selection.width, captureController.selection.height)
            context.clip()
            let spotlights = []
            for (let item of captureController.annotations)
                if (item.type === "spotlight") spotlights.push(item)
            if (captureController.draft.type === "spotlight")
                spotlights.push(captureController.draft)
            if (spotlights.length > 0) {
                context.save()
                context.fillStyle = "rgba(0, 0, 0, 0.59)"
                context.fillRect(root.localX, root.localY,
                                 captureController.selection.width,
                                 captureController.selection.height)
                context.globalCompositeOperation = "destination-out"
                for (let item of spotlights) {
                    let sx = item.start.x - screenRect.x
                    let sy = item.start.y - screenRect.y - captureController.scrollOffset
                    let ex = item.end.x - screenRect.x
                    let ey = item.end.y - screenRect.y - captureController.scrollOffset
                    context.beginPath()
                    context.ellipse(Math.min(sx, ex), Math.min(sy, ey),
                                    Math.abs(ex - sx), Math.abs(ey - sy))
                    context.fill()
                }
                context.restore()
            }
            for (let item of captureController.annotations)
                if (item.type !== "spotlight") drawShape(context, item)
            if (captureController.draft.type !== "spotlight")
                drawShape(context, captureController.draft)
            context.restore()
        }
    }

    Item {
        x: root.localX
        y: root.localY
        width: captureController.selection.width
        height: captureController.selection.height
        clip: true
        visible: captureController.selected && !captureController.scrolling
        Repeater {
            id: textRepeater
            objectName: "textRepeater"
            model: captureController.annotations.length
            delegate: Item {
                objectName: "annotationText"
                required property int index
                readonly property var annotation: captureController.annotations[index]
                visible: annotation && annotation.type === "text"
                x: visible ? annotation.start.x - captureController.selection.x : 0
                y: visible ? annotation.start.y - captureController.selection.y
                             - captureController.scrollOffset
                             - root.textLineHeight / 2 : 0
                readonly property string text: visible ? annotation.text : ""
                readonly property var lines: text.split("\n")
                width: childrenRect.width
                height: lines.length * root.textLineHeight
                function lineCenter(lineIndex) {
                    let line = lineRepeater.itemAt(lineIndex)
                    return line !== null ? line.y + line.height / 2 : -1
                }
                Repeater {
                    id: lineRepeater
                    model: parent.lines
                    delegate: Text {
                        required property int index
                        required property string modelData
                        y: index * root.textLineHeight
                        height: root.textLineHeight
                        text: modelData
                        color: annotation.color || "#ff4b55"
                        font.bold: true
                        font.pixelSize: 22
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }

    Repeater {
        id: resizeHandles
        model: [
            {horizontal: 0, vertical: 0},
            {horizontal: 0.5, vertical: 0},
            {horizontal: 1, vertical: 0},
            {horizontal: 0, vertical: 0.5},
            {horizontal: 1, vertical: 0.5},
            {horizontal: 0, vertical: 1},
            {horizontal: 0.5, vertical: 1},
            {horizontal: 1, vertical: 1}
        ]
        delegate: Rectangle {
            required property var modelData
            objectName: "selectionResizeHandle"
            z: 6
            width: 9
            height: 9
            x: root.localX + captureController.selection.width * modelData.horizontal - width / 2
            y: root.localY + captureController.selection.height * modelData.vertical - height / 2
            visible: captureController.selected && !captureController.scrolling &&
                     !captureController.hasScrollImage && captureController.tool === "select"
            color: "white"
            border.color: "#397eb8"
            border.width: 1
        }
    }

    MouseArea {
        id: picker
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: captureController.selected && captureController.tool !== "select"
                     ? Qt.CrossCursor : captureController.hasScrollImage ? Qt.OpenHandCursor : Qt.ArrowCursor
        onWheel: wheel => {
            if (captureController.hasScrollImage && !captureController.scrolling) {
                captureController.scrollPreviewBy(-wheel.angleDelta.y / 120 * 80)
                wheel.accepted = true
            }
        }
        onPositionChanged: mouse => captureController.pointerMove(screenIndex, mouse.x, mouse.y)
        onPressed: mouse => {
            if (mouse.button === Qt.RightButton) {
                captureController.cancel()
                return
            }
            root.forceActiveFocus()
            if (captureController.selected && captureController.tool === "text") {
                let gx = screenRect.x + mouse.x
                let gy = screenRect.y + mouse.y
                let rect = captureController.selection
                if (gx >= rect.x && gy >= rect.y && gx <= rect.x + rect.width && gy <= rect.y + rect.height) {
                    root.textX = mouse.x
                    root.textY = mouse.y
                    editor.visible = true
                    textInput.text = ""
                    textInput.forceActiveFocus()
                }
                return
            }
            captureController.pointerPress(screenIndex, mouse.x, mouse.y)
        }
        onReleased: mouse => captureController.pointerRelease(screenIndex, mouse.x, mouse.y)
    }

    Item {
        id: editor
        objectName: "textEditor"
        visible: false
        readonly property real inset: 6
        x: Math.max(0, Math.min(root.textX - inset, root.width - width))
        y: Math.max(0, Math.min(root.textY - root.textLineHeight / 2 - inset,
                                 root.height - height))
        z: 5
        width: Math.max(120, textInput.contentWidth + inset * 2)
        height: Math.max(1, textInput.lineCount) * root.textLineHeight + inset * 2
        function commitText() {
            if (!visible) return
            let value = textInput.text
            let textStartX = screenRect.x + x + inset
            let textStartY = screenRect.y + y + inset + root.textLineHeight / 2
            visible = false
            if (value.trim().length > 0)
                captureController.addText(textStartX, textStartY, value)
            root.forceActiveFocus()
        }
        function handleReturn(event) {
            if (event.modifiers & Qt.ShiftModifier) {
                textInput.insert(textInput.cursorPosition, "\n")
            } else {
                commitText()
            }
            event.accepted = true
        }

        Canvas {
            anchors.fill: parent
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                let context = getContext("2d")
                context.clearRect(0, 0, width, height)
                context.strokeStyle = captureController.annotationColor
                context.lineWidth = 1
                context.setLineDash([5, 4])
                context.strokeRect(0.5, 0.5, width - 1, height - 1)
            }
        }

        TextEdit {
            id: textInput
            objectName: "textEditorInput"
            x: editor.inset
            y: editor.inset
            width: editor.width - editor.inset * 2
            height: editor.height - editor.inset * 2
            color: captureController.annotationColor
            font.bold: true
            font.pixelSize: 22
            textFormat: TextEdit.PlainText
            wrapMode: TextEdit.NoWrap
            verticalAlignment: TextEdit.AlignVCenter
            selectByMouse: true
            Keys.onReturnPressed: event => editor.handleReturn(event)
            Keys.onEnterPressed: event => editor.handleReturn(event)
            onActiveFocusChanged: {
                if (!activeFocus && editor.visible) editor.commitText()
            }
            Keys.onEscapePressed: event => {
                editor.visible = false
                root.forceActiveFocus()
                event.accepted = true
            }
        }
    }

    Rectangle {
        visible: root.activeRect.width > 0 && root.activeRect.height > 0
        x: Math.max(8, Math.min(root.localX, root.width - width - 8))
        y: Math.max(8, Math.min(root.localY - height - 5, root.height - height - 8))
        width: dimensions.contentWidth + 18
        height: 25
        radius: 4
        color: "#d9272d35"
        Text {
            id: dimensions
            anchors.centerIn: parent
            color: "white"
            font.pixelSize: 12
            text: Math.round(root.activeRect.width) + " × " + Math.round(root.activeRect.height)
        }
    }

    Rectangle {
        id: scrollControls
        visible: captureController.scrolling && captureController.scrollMonitor === screenIndex
        z: 12
        width: 290
        height: 44
        radius: 6
        color: "#f8ffffff"
        border.color: "#cbd4de"
        x: Math.max(8, Math.min(root.localX + captureController.selection.width / 2 - width / 2,
                                root.width - width - 8))
        y: root.localY + captureController.selection.height + height + 8 < root.height
           ? root.localY + captureController.selection.height + 8
           : Math.max(8, root.localY - height - 8)
        Row {
            anchors.centerIn: parent
            spacing: 8
            Rectangle {
                width: 170; height: 32; radius: 4; color: "#dcecff"
                Text { anchors.centerIn: parent; text: "完成长截图 ↵"; color: "#26313d" }
                MouseArea { anchors.fill: parent; onClicked: captureController.finishScrollCapture() }
            }
            Rectangle {
                width: 92; height: 32; radius: 4; color: "#edf3f9"
                Text { anchors.centerIn: parent; text: "取消 Esc"; color: "#26313d" }
                MouseArea { anchors.fill: parent; onClicked: captureController.cancelScrollCapture() }
            }
        }
    }

    Rectangle {
        id: toolbar
        visible: captureController.selected && !captureController.scrolling && captureController.toolbarScreen === screenIndex
        onVisibleChanged: if (!visible) {
            root.hideToolbarTooltip()
            colorPanel.visible = false
        }
        z: 10
        width: toolsRow.width + 12
        height: toolsRow.height + 10
        radius: 6
        color: "#f8ffffff"
        border.color: "#cbd4de"
        x: Math.max(8, Math.min(root.localX + captureController.selection.width - width,
                                root.width - width - 8))
        y: root.localY + captureController.selection.height + height + 8 < root.height
           ? root.localY + captureController.selection.height + 8
           : Math.max(8, root.localY - height - 8)

        Flow {
            id: toolsRow
            anchors.centerIn: parent
            spacing: 2
            width: Math.max(60, Math.min(root.width - 28, (toolbarRepeater.count + 1) * 62))
            height: childrenRect.height
            Rectangle {
                id: colorButton
                objectName: "colorButton"
                width: 60
                height: 36
                radius: 4
                color: "transparent"

                Row {
                    anchors.centerIn: parent
                    spacing: 2
                    Text {
                        text: "Q："
                        width: 25
                        height: 20
                        color: "#26313d"
                        font.pixelSize: 14
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    ToolbarGlyph {
                        action: "color"
                        accentColor: captureController.annotationColor
                    }
                }
                MouseArea {
                    id: colorButtonMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: root.showToolbarTooltip("颜色：点击预设色或拖动调色盘", colorButtonMouse)
                    onExited: root.hideToolbarTooltip()
                    onClicked: {
                        root.hideToolbarTooltip()
                        if (editor.visible) editor.commitText()
                        colorPanel.visible = !colorPanel.visible
                        root.forceActiveFocus()
                    }
                }
            }
            Repeater {
                id: toolbarRepeater
                model: [
                    {key: "V", action: "select", hint: captureController.hasScrollImage ? "长图：拖动画面平移，滚轮浏览" : "选区：拖动方块调整边框；方向键扩展 1px，Shift+方向键收缩 1px"},
                    {key: "R", action: "rect", hint: "矩形：拖动绘制矩形"},
                    {key: "E", action: "ellipse", hint: "椭圆：拖动绘制椭圆"},
                    {key: "A", action: "arrow", hint: "箭头：拖动绘制箭头"},
                    {key: "D", action: "pen", hint: "画笔：自由绘制曲线"},
                    {key: "T", action: "text", hint: "文字：单击输入，Shift+Enter 换行"},
                    {key: "G", action: "mosaic", hint: "马赛克：拖动选择矩形区域"},
                    {key: "Z", action: "undo", hint: "撤销：移除上一项标注"},
                    {key: "C", action: "copy", hint: "复制：将截图复制到剪贴板"},
                    {key: "S", action: "save", hint: "保存：将截图保存为 PNG"},
                    {key: "X", action: "cancel", hint: "关闭：退出截图"},
                    {key: "⇧S", action: "scroll", hint: "纵向长截图：滚动页面后自动拼接"},
                    {key: "⇧G", action: "scroll_horizontal", hint: "横向长截图：横向滚动页面后自动拼接"},
                    {key: "⇧C", action: "scroll_auto", hint: "纵向自动长截图：自动滚动到页面末尾"},
                    {key: "⇧V", action: "scroll_auto_horizontal", hint: "横向自动长截图：自动向右滚动并拼接"},
                    {key: "W", action: "line", hint: "直线：拖动绘制"},
                    {key: "B", action: "highlighter", hint: "荧光笔：半透明自由轨迹"},
                    {key: "⇧B", action: "spotlight", hint: "聚光灯：突出椭圆区域"},
                    {key: "⇧T", action: "marker", hint: "编号标记：单击放置自动编号"},
                    {key: "⇧R", action: "roundrect", hint: "圆角矩形：拖动绘制"},
                    {key: "⇧D", action: "fillrect", hint: "实心矩形：拖动绘制"},
                    {key: "⇧E", action: "fillellipse", hint: "实心椭圆：拖动绘制"},
                    {key: "⇧A", action: "curvedarrow", hint: "弯曲箭头：拖动绘制"},
                    {key: "⇧W", action: "doublearrow", hint: "双向弯曲箭头：拖动绘制"},
                    {key: "⇧Z", action: "redo", hint: "重做：恢复刚撤销的标注"}
                ]
                delegate: Rectangle {
                    required property var modelData
                    width: 60
                    height: 36
                    radius: 4
                    color: "transparent"
                    Rectangle {
                        objectName: modelData.action === "select" ? "selectHighlight" : ""
                        anchors.centerIn: parent
                        width: 56
                        height: 30
                        radius: 5
                        color: captureController.tool === modelData.action ? "#dcecff" :
                               button.containsMouse ? "#edf3f9" : "transparent"
                    }
                    Row {
                        objectName: modelData.action === "select" ? "selectContent" : ""
                        anchors.centerIn: parent
                        spacing: 2
                        Text {
                            text: modelData.key + "："
                            width: 25
                            height: 20
                            color: "#26313d"
                            font.pixelSize: 14
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        ToolbarGlyph {
                            objectName: modelData.action === "select" ? "selectCursorGlyph" : ""
                            action: modelData.action
                            accentColor: captureController.annotationColor
                        }
                    }
                    MouseArea {
                        id: button
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: root.showToolbarTooltip(modelData.hint, button)
                        onExited: root.hideToolbarTooltip()
                        onClicked: {
                            root.hideToolbarTooltip()
                            if (editor.visible) editor.commitText()
                            colorPanel.visible = false
                            switch (modelData.action) {
                            case "undo": captureController.undo(); break
                            case "copy": captureController.copy(); break
                            case "save": captureController.save(); break
                            case "cancel": captureController.cancel(); break
                            case "scroll": captureController.startScrollCapture(false); break
                            case "scroll_horizontal": captureController.startScrollCapture(true); break
                            case "scroll_auto": captureController.startAutoScrollCapture(false); break
                            case "scroll_auto_horizontal": captureController.startAutoScrollCapture(true); break
                            case "redo": captureController.redo(); break
                            default: captureController.tool = modelData.action
                            }
                            root.forceActiveFocus()
                        }
                    }
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        z: 9
        visible: colorPanel.visible
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: colorPanel.visible = false
    }

    Rectangle {
        id: colorPanel
        objectName: "colorPanel"
        property real hue: 0
        property real saturation: 1
        property real brightness: 1
        visible: false
        z: 20
        width: 260
        height: 238
        radius: 7
        color: "#ffffff"
        border.color: "#cbd4de"
        x: Math.max(8, Math.min(toolbar.x, root.width - width - 8))
        y: toolbar.y - height - 8 >= 8
           ? toolbar.y - height - 8
           : toolbar.y + toolbar.height + 8

        function loadColor() {
            let color = captureController.annotationColor
            let r = parseInt(color.slice(1, 3), 16) / 255
            let g = parseInt(color.slice(3, 5), 16) / 255
            let b = parseInt(color.slice(5, 7), 16) / 255
            let highest = Math.max(r, g, b)
            let lowest = Math.min(r, g, b)
            let delta = highest - lowest
            let h = 0
            if (delta > 0) {
                if (highest === r) h = ((g - b) / delta) % 6
                else if (highest === g) h = (b - r) / delta + 2
                else h = (r - g) / delta + 4
                h = (h / 6 + 1) % 1
            }
            hue = h
            saturation = highest === 0 ? 0 : delta / highest
            brightness = highest
        }

        function chooseColor(h, s, v) {
            hue = Math.max(0, Math.min(1, h))
            saturation = Math.max(0, Math.min(1, s))
            brightness = Math.max(0, Math.min(1, v))
            captureController.setAnnotationColorFromHsv(hue, saturation, brightness)
        }

        onVisibleChanged: {
            if (visible) {
                loadColor()
                root.hideToolbarTooltip()
            } else captureController.saveAnnotationColor()
        }
        onHueChanged: saturationValueField.requestPaint()

        Column {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10

            Item {
                width: 238
                height: 18
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: "绘制颜色"
                    color: "#26313d"
                    font.pixelSize: 13
                }
                Rectangle {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 18
                    height: 18
                    radius: 9
                    color: captureController.annotationColor
                    border.color: "#8d98a5"
                }
            }

            Row {
                spacing: 10
                Canvas {
                    id: saturationValueField
                    objectName: "saturationValueField"
                    width: 208
                    height: 160
                    antialiasing: true
                    onPaint: {
                        let context = getContext("2d")
                        context.clearRect(0, 0, width, height)
                        context.fillStyle = Qt.hsla(colorPanel.hue, 1, 0.5, 1)
                        context.fillRect(0, 0, width, height)
                        let white = context.createLinearGradient(0, 0, width, 0)
                        white.addColorStop(0, "#ffffff")
                        white.addColorStop(1, "rgba(255, 255, 255, 0)")
                        context.fillStyle = white
                        context.fillRect(0, 0, width, height)
                        let black = context.createLinearGradient(0, 0, 0, height)
                        black.addColorStop(0, "rgba(0, 0, 0, 0)")
                        black.addColorStop(1, "#000000")
                        context.fillStyle = black
                        context.fillRect(0, 0, width, height)
                    }
                    MouseArea {
                        anchors.fill: parent
                        function pick(mouse) {
                            colorPanel.chooseColor(colorPanel.hue,
                                                   mouse.x / width,
                                                   1 - mouse.y / height)
                        }
                        onPressed: mouse => pick(mouse)
                        onPositionChanged: mouse => { if (pressed) pick(mouse) }
                        onReleased: captureController.saveAnnotationColor()
                    }
                    Rectangle {
                        width: 14
                        height: 14
                        radius: 7
                        x: colorPanel.saturation * saturationValueField.width - width / 2
                        y: (1 - colorPanel.brightness) * saturationValueField.height - height / 2
                        color: "transparent"
                        border.color: "#26313d"
                        border.width: 1
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 2
                            radius: 5
                            color: "transparent"
                            border.color: "white"
                            border.width: 2
                        }
                    }
                }
                Canvas {
                    id: hueField
                    objectName: "hueField"
                    width: 20
                    height: 160
                    onPaint: {
                        let context = getContext("2d")
                        let gradient = context.createLinearGradient(0, 0, 0, height)
                        gradient.addColorStop(0, "#ff0000")
                        gradient.addColorStop(1 / 6, "#ffff00")
                        gradient.addColorStop(2 / 6, "#00ff00")
                        gradient.addColorStop(3 / 6, "#00ffff")
                        gradient.addColorStop(4 / 6, "#0000ff")
                        gradient.addColorStop(5 / 6, "#ff00ff")
                        gradient.addColorStop(1, "#ff0000")
                        context.fillStyle = gradient
                        context.fillRect(0, 0, width, height)
                    }
                    MouseArea {
                        anchors.fill: parent
                        function pick(mouse) {
                            colorPanel.chooseColor(mouse.y / height,
                                                   colorPanel.saturation,
                                                   colorPanel.brightness)
                        }
                        onPressed: mouse => pick(mouse)
                        onPositionChanged: mouse => { if (pressed) pick(mouse) }
                        onReleased: captureController.saveAnnotationColor()
                    }
                    Rectangle {
                        x: -2
                        y: colorPanel.hue * hueField.height - height / 2
                        width: 24
                        height: 6
                        radius: 2
                        color: "transparent"
                        border.color: "#26313d"
                        border.width: 1
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 1
                            radius: 1
                            color: "transparent"
                            border.color: "white"
                            border.width: 1
                        }
                    }
                }
            }

            Row {
                spacing: 5
                Repeater {
                    id: colorPresetRepeater
                    model: ["#ff4b55", "#ff9d42", "#ffd84d", "#5bd686", "#42c8d8",
                            "#4d94ff", "#a77bff", "#f774ba", "#ffffff", "#202020"]
                    delegate: Rectangle {
                        required property string modelData
                        width: 18
                        height: 18
                        radius: 4
                        color: modelData
                        border.color: captureController.annotationColor === modelData
                                      ? "#27313d" : "#a8b1bb"
                        border.width: captureController.annotationColor === modelData ? 2 : 1
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                captureController.annotationColor = modelData
                                colorPanel.visible = false
                                root.forceActiveFocus()
                            }
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: toolbarTooltip
        visible: root.toolbarTooltipVisible && toolbar.visible
        z: 30
        width: tooltipLabel.implicitWidth + 20
        height: tooltipLabel.implicitHeight + 12
        x: Math.max(8, Math.min(root.toolbarTooltipX - width / 2,
                                root.width - width - 8))
        y: root.toolbarTooltipY - height - 8 >= 8
           ? root.toolbarTooltipY - height - 8
           : root.toolbarTooltipY + 40
        radius: 5
        color: "#f0202734"
        border.color: "#748090"
        Text {
            id: tooltipLabel
            anchors.centerIn: parent
            text: root.toolbarTooltipText
            color: "white"
            font.pixelSize: 13
        }
    }

    Rectangle {
        visible: captureController.status.length > 0 &&
                 (!captureController.scrolling || root.localY >= 56)
        z: 20
        anchors.horizontalCenter: parent.horizontalCenter
        y: 8
        width: statusText.contentWidth + 24
        height: 32
        radius: 5
        color: "#db9d2735"
        Text {
            id: statusText
            anchors.centerIn: parent
            text: captureController.status
            color: "white"
        }
    }

    Connections {
        target: captureController
        function onSelectionChanged() { marks.requestPaint() }
        function onAnnotationsChanged() { marks.requestPaint() }
        function onDraftChanged() { marks.requestPaint() }
        function onSelectedChanged() { marks.requestPaint() }
        function onScrollOffsetChanged() { marks.requestPaint() }
    }
}
