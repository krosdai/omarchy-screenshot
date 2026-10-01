// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import ScreenshotInternals

Item {
    id: root
    focus: true
    property real zoom: 1
    property real panX: 0
    property real panY: 0
    readonly property real imageWidth: Math.max(1, captureController.scrollWidth)
    readonly property real imageHeight: Math.max(1, captureController.scrollHeight)
    readonly property real fitScale: Math.min(1, (width - 60) / imageWidth,
                                               (height * 0.8) / imageHeight,
                                               Math.max(80, height - annotationToolbar.toolbarHeight - 64) / imageHeight)
    readonly property real imageScale: fitScale * zoom
    readonly property real maxPanX: Math.max(0, imageWidth * imageScale - viewport.width)
    readonly property real maxPanY: Math.max(0, imageHeight * imageScale - viewport.height)
    readonly property bool canPan: maxPanX > 0 || maxPanY > 0
    readonly property rect sourceRect: Qt.rect(panX / imageScale, panY / imageScale,
                                                viewport.width / imageScale,
                                                viewport.height / imageScale)
    property bool movingImage: false
    property real lastMouseX: 0
    property real lastMouseY: 0

    function clampPan() {
        panX = Math.max(0, Math.min(maxPanX, panX))
        panY = Math.max(0, Math.min(maxPanY, panY))
    }
    function pointInImage(x, y) {
        return {x: sourceRect.x + x / imageScale,
                y: sourceRect.y + y / imageScale}
    }
    function selectionBorderAt(x, y) {
        const p = pointInImage(x, y)
        const crop = captureController.selection
        const right = crop.x + crop.width
        const bottom = crop.y + crop.height
        // Match the controller's border tolerance in image coordinates.
        return p.x >= crop.x - 7 && p.x <= right + 7 &&
               p.y >= crop.y - 7 && p.y <= bottom + 7 &&
               (Math.abs(p.x - crop.x) <= 7 || Math.abs(p.x - right) <= 7 ||
                Math.abs(p.y - crop.y) <= 7 || Math.abs(p.y - bottom) <= 7)
    }
    function beginPan(x, y) {
        movingImage = true
        lastMouseX = x
        lastMouseY = y
    }
    function movePan(x, y) {
        panX -= x - lastMouseX
        panY -= y - lastMouseY
        clampPan()
        lastMouseX = x
        lastMouseY = y
    }
    function setZoom(next) {
        let centerX = sourceRect.x + sourceRect.width / 2
        let centerY = sourceRect.y + sourceRect.height / 2
        zoom = Math.max(1, Math.min(12, next))
        panX = centerX * imageScale - viewport.width / 2
        panY = centerY * imageScale - viewport.height / 2
        clampPan()
    }
    function commitText() {
        if (!textEditor.visible) return
        let value = textInput.text
        textEditor.visible = false
        if (value.trim().length > 0) {
            let p = pointInImage(textEditor.x, textEditor.y + 12)
            captureController.addText(p.x, p.y, value)
        }
        forceActiveFocus()
    }
    function copySelectionAt(x, y) {
        const p = pointInImage(x, y)
        const crop = captureController.selection
        if (!captureController.selected ||
            p.x <= crop.x || p.y <= crop.y ||
            p.x >= crop.x + crop.width || p.y >= crop.y + crop.height ||
            selectionBorderAt(x, y))
            return false
        // The first click of a double-click must not leave a new marker.
        const removeMarker = captureController.tool === "marker" && picker.lastMarkerClickAdded
        picker.lastMarkerClickAdded = false
        movingImage = false
        commitText()
        forceActiveFocus()
        captureController.cancelPointerAction()
        if (removeMarker) captureController.undo()
        captureController.copy()
        return true
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape) {
            if (textEditor.visible) commitText()
            else captureController.cancel()
        } else if (event.key === Qt.Key_Alt && captureController.tool === "text") {
            commitText()
            captureController.restoreToolBeforeText()
        } else { annotationToolbar.handleShortcut(event); return }
        event.accepted = true
    }

    Rectangle { anchors.fill: parent; color: "#db111820" }

    Rectangle {
        id: viewport
        objectName: "longViewport"
        x: (root.width - width) / 2
        y: 12
        width: Math.min(root.width - 60, root.imageWidth * root.imageScale)
        height: Math.min(root.height * 0.8, root.imageHeight * root.imageScale)
        clip: true
        color: "#ffffff"
        border.width: 1
        border.color: "#a4bad0"

        LongImageItem {
            anchors.fill: parent
            controller: captureController
            sourceRect: root.sourceRect
        }

        Rectangle {
            readonly property rect crop: captureController.selection
            x: (crop.x - root.sourceRect.x) * root.imageScale
            y: (crop.y - root.sourceRect.y) * root.imageScale
            width: crop.width * root.imageScale
            height: crop.height * root.imageScale
            color: "transparent"
            border.color: "#ffffff"
            border.width: 2
        }

        MouseArea {
            id: picker
            objectName: "longImagePicker"
            property bool lastMarkerClickAdded: false
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.MiddleButton
            hoverEnabled: true
            cursorShape: root.movingImage ? Qt.ClosedHandCursor :
                         captureController.tool !== "select" ? Qt.CrossCursor :
                         root.canPan && !root.selectionBorderAt(mouseX, mouseY)
                         ? Qt.OpenHandCursor : Qt.ArrowCursor
            onPressed: mouse => {
                annotationToolbar.variantPanelVisible = false
                root.forceActiveFocus()
                if (mouse.button === Qt.MiddleButton ||
                    (captureController.tool === "select" && root.canPan &&
                     !root.selectionBorderAt(mouse.x, mouse.y))) {
                    root.beginPan(mouse.x, mouse.y)
                    return
                }
                if (captureController.tool === "text") {
                    if (textEditor.visible) root.commitText()
                    let p = root.pointInImage(mouse.x, mouse.y)
                    let crop = captureController.selection
                    if (p.x < crop.x || p.y < crop.y ||
                        p.x > crop.x + crop.width || p.y > crop.y + crop.height)
                        return
                    textEditor.x = mouse.x
                    textEditor.y = mouse.y
                    textInput.text = ""
                    textEditor.visible = true
                    textInput.forceActiveFocus()
                    return
                }
                let p = root.pointInImage(mouse.x, mouse.y)
                captureController.pointerPress(-1, p.x, p.y)
            }
            onPositionChanged: mouse => {
                if (root.movingImage) {
                    root.movePan(mouse.x, mouse.y)
                } else if (pressed && captureController.tool !== "text") {
                    let p = root.pointInImage(mouse.x, mouse.y)
                    captureController.pointerMove(-1, p.x, p.y)
                }
            }
            onReleased: mouse => {
                if (root.movingImage) {
                    root.movePan(mouse.x, mouse.y)
                    root.movingImage = false
                    lastMarkerClickAdded = false
                    return
                }
                if (captureController.tool === "text") {
                    lastMarkerClickAdded = false
                    return
                }
                let p = root.pointInImage(mouse.x, mouse.y)
                const wasMarker = captureController.tool === "marker"
                const before = captureController.annotations.length
                captureController.pointerRelease(-1, p.x, p.y)
                lastMarkerClickAdded = wasMarker && captureController.annotations.length > before
            }
            onDoubleClicked: mouse => {
                if (mouse.button === Qt.LeftButton)
                    root.copySelectionAt(mouse.x, mouse.y)
            }
            onCanceled: {
                if (root.movingImage) root.movingImage = false
                else captureController.cancelPointerAction()
            }
            onWheel: wheel => {
                if (wheel.modifiers & Qt.ControlModifier)
                    root.setZoom(root.zoom * (wheel.angleDelta.y > 0 ? 1.2 : 1 / 1.2))
                else if (wheel.modifiers & Qt.ShiftModifier)
                    root.panX -= wheel.angleDelta.y / 2
                else root.panY -= wheel.angleDelta.y / 2
                root.clampPan()
                wheel.accepted = true
            }
        }

        Rectangle {
            id: textEditor
            objectName: "longTextEditor"
            visible: false
            width: Math.max(140, textInput.contentWidth + 12)
            height: Math.max(34, textInput.contentHeight + 12)
            color: "#eeffffff"
            border.color: captureController.annotationColor
            border.width: 1
            TextEdit {
                id: textInput
                objectName: "longTextInput"
                x: 6; y: 6
                width: Math.max(128, contentWidth)
                color: captureController.annotationColor
                font.pixelSize: 22
                font.bold: true
                wrapMode: TextEdit.NoWrap
                Keys.onReturnPressed: event => {
                    if (event.modifiers & Qt.ShiftModifier)
                        insert(cursorPosition, "\n")
                    else root.commitText()
                    event.accepted = true
                }
                Keys.onEscapePressed: event => {
                    root.commitText()
                    event.accepted = true
                }
            }
            MouseArea {
                anchors.fill: parent
                visible: textEditor.visible && textInput.text.length === 0
                acceptedButtons: Qt.LeftButton
                onDoubleClicked: mouse => {
                    if (mouse.button === Qt.LeftButton)
                        root.copySelectionAt(textEditor.x + mouse.x, textEditor.y + mouse.y)
                }
            }
        }
    }

    Repeater {
        id: resizeHandles
        model: [
            {x: 0, y: 0}, {x: .5, y: 0}, {x: 1, y: 0},
            {x: 0, y: .5}, {x: 1, y: .5},
            {x: 0, y: 1}, {x: .5, y: 1}, {x: 1, y: 1}
        ]
        delegate: SelectionResizeHandle {
            required property var modelData
            objectName: "longResizeHandle"
            z: 6
            readonly property rect crop: captureController.selection
            readonly property real sourceX: crop.x + crop.width * modelData.x
            readonly property real sourceY: crop.y + crop.height * modelData.y
            readonly property real viewportX: (sourceX - root.sourceRect.x) * root.imageScale
            readonly property real viewportY: (sourceY - root.sourceRect.y) * root.imageScale
            x: viewport.x + viewportX - width / 2
            y: viewport.y + viewportY - height / 2
            visible: captureController.selected && captureController.tool === "select" &&
                     (resizeHitArea.pressed ||
                      (viewportX >= -0.5 && viewportX <= viewport.width + 0.5 &&
                       viewportY >= -0.5 && viewportY <= viewport.height + 0.5))
            MouseArea {
                id: resizeHitArea
                objectName: "longResizeHitArea"
                anchors.centerIn: parent
                width: parent.hitRadius * 2 + 1
                height: width
                property point pressPosition
                property point pressSource
                property real pressScale: 1
                function sourcePoint(mouse) {
                    const position = mapToItem(viewport, mouse.x, mouse.y)
                    return Qt.point(pressSource.x + (position.x - pressPosition.x) / pressScale,
                                    pressSource.y + (position.y - pressPosition.y) / pressScale)
                }
                onPressed: mouse => {
                    // Anchor the drag to stable viewport coordinates and keep
                    // the initial grab offset when pressing beside the square.
                    pressPosition = mapToItem(viewport, mouse.x, mouse.y)
                    pressSource = Qt.point(parent.sourceX, parent.sourceY)
                    pressScale = root.imageScale
                    annotationToolbar.variantPanelVisible = false
                    root.forceActiveFocus()
                    captureController.pointerPress(-1, pressSource.x, pressSource.y)
                }
                onPositionChanged: mouse => {
                    if (!pressed) return
                    const p = sourcePoint(mouse)
                    captureController.pointerMove(-1, p.x, p.y)
                }
                onReleased: mouse => {
                    const p = sourcePoint(mouse)
                    captureController.pointerRelease(-1, p.x, p.y)
                }
                onCanceled: captureController.cancelPointerAction()
            }
        }
    }

    Rectangle {
        x: (root.width - width) / 2
        y: viewport.y + viewport.height + 8
        width: info.implicitWidth + 20
        height: 28
        radius: 5
        color: "#d9212b36"
        Text {
            id: info
            anchors.centerIn: parent
            // The percentage is part of the message so locales can place the sign.
            text: root.imageWidth + " × " + root.imageHeight + " · " +
                  qsTr("%1% · Drag to view · Double-click to copy · Scroll to pan · Ctrl+scroll to zoom")
                      .arg(Math.round(root.zoom * 100))
            font.pixelSize: 12
            color: "white"
        }
    }

    AnnotationToolbar {
        id: annotationToolbar
        objectName: "longAnnotationToolbar"
        anchors.fill: parent
        z: 10
        longImage: true
        onCommitRequested: root.commitText()
        onFocusRequested: root.forceActiveFocus()
        onZoomRequested: factor => root.setZoom(root.zoom * factor)
    }

    Rectangle {
        objectName: "longStatus"
        visible: captureController.status.length > 0
        anchors.horizontalCenter: parent.horizontalCenter
        y: 8
        width: Math.min(statusText.implicitWidth + 24, root.width - 16)
        height: statusText.implicitHeight + 14
        radius: 5
        color: annotationToolbar.toolbarSurface
        border.color: annotationToolbar.panelBorder
        Text {
            id: statusText
            anchors.centerIn: parent
            width: parent.width - 24
            text: captureController.status
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            horizontalAlignment: Qt.application.layoutDirection === Qt.RightToLeft
                                 ? Text.AlignRight : Text.AlignLeft
            color: annotationToolbar.toolbarInk
        }
    }

    Connections {
        target: captureController
        function onScrollStateChanged() {
            if (captureController.scrollState === 3) {
                root.panY = root.maxPanY
                root.clampPan()
            }
        }
    }
}
