// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick

Item {
    id: root
    property rect screenRect: Qt.rect(0, 0, 0, 0)
    property bool frameHidden: false
    readonly property rect captureBox: Qt.rect(
        captureController.scrollRegion.x - screenRect.x,
        captureController.scrollRegion.y - screenRect.y,
        captureController.scrollRegion.width,
        captureController.scrollRegion.height)
    readonly property rect toolbarRect: Qt.rect(toolbar.x, toolbar.y,
                                               toolbar.width, toolbar.height)
    readonly property bool toolbarOutsideCapture:
        toolbarRect.x + toolbarRect.width <= captureBox.x ||
        toolbarRect.x >= captureBox.x + captureBox.width ||
        toolbarRect.y + toolbarRect.height <= captureBox.y ||
        toolbarRect.y >= captureBox.y + captureBox.height
    readonly property color ink: captureController.darkToolbar ? "#e9f0f6" : "#26313d"

    function toolbarPosition(w, h) {
        const gap = 8
        const box = captureBox
        const rightAligned = Math.max(gap, Math.min(box.x + box.width - w,
                                                    root.width - w - gap))
        if (box.y + box.height + gap + h <= root.height - gap)
            return Qt.point(rightAligned, box.y + box.height + gap)
        if (box.y - gap - h >= gap)
            return Qt.point(rightAligned, box.y - gap - h)
        const sideY = Math.max(gap, Math.min(box.y, root.height - h - gap))
        if (box.x - gap - w >= gap)
            return Qt.point(box.x - gap - w, sideY)
        if (box.x + box.width + gap + w <= root.width - gap)
            return Qt.point(box.x + box.width + gap, sideY)
        // Keep an overlapping hint at the top, above the new rows appended
        // by the stitcher. The first frame is captured before showing it.
        return Qt.point(rightAligned, Math.max(gap, box.y - gap - h))
    }

    Rectangle {
        x: root.captureBox.x - 2
        y: root.captureBox.y - 2
        width: root.captureBox.width + 4
        height: root.captureBox.height + 4
        color: "transparent"
        border.width: 2
        border.color: "#54a8f8"
        visible: !root.frameHidden
    }

    MouseArea {
        objectName: "scrollStopArea"
        x: root.captureBox.x
        y: root.captureBox.y
        width: root.captureBox.width
        height: root.captureBox.height
        acceptedButtons: Qt.LeftButton
        enabled: !captureController.scrollStopping
        onClicked: mouse => {
            if (captureController.scrollAwaitingPane)
                captureController.selectScrollPane(
                    root.screenRect.x + x + mouse.x,
                    root.screenRect.y + y + mouse.y)
            else captureController.pauseScroll()
        }
    }

    Rectangle {
        id: toolbar
        objectName: "scrollToolbar"
        width: content.implicitWidth + 24
        height: 44
        radius: 6
        color: captureController.darkToolbar ? "#f0222b36" : "#f8ffffff"
        border.color: captureController.darkToolbar ? "#526171" : "#cbd4de"
        readonly property point position: root.toolbarPosition(width, height)
        x: position.x
        y: position.y
        // Take a clean initial frame for a full-screen region, then leave the
        // hint visible. Later frames only contribute rows below the hint.
        visible: root.toolbarOutsideCapture || captureController.scrollHeight > 0
        Row {
            id: content
            anchors.centerIn: parent
            spacing: 8
            ToolbarGlyph {
                action: "scroll"
                darkMode: captureController.darkToolbar
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                objectName: "scrollStopHint"
                text: captureController.scrollAwaitingPane
                      ? qsTr("Click the area to scroll") : captureController.scrollStopping
                      ? qsTr("Creating long image…") : qsTr("Click to stop capturing")
                color: root.ink
                font.pixelSize: 13
                anchors.verticalCenter: parent.verticalCenter
            }
            Rectangle {
                visible: captureController.scrollAwaitingPane
                width: visible ? 82 : 0
                height: 30
                radius: 4
                color: captureController.darkToolbar ? "#315577" : "#dcecff"
                opacity: captureController.scrollPaneSelected ? 1 : .45
                Row {
                    anchors.centerIn: parent
                    spacing: 4
                    ToolbarGlyph {
                        width: 18; height: 18
                        action: "resume"
                        darkMode: captureController.darkToolbar
                    }
                    Text { text: qsTr("Continue scrolling"); color: root.ink; font.pixelSize: 12 }
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: captureController.scrollPaneSelected
                    onClicked: captureController.continueAfterPane()
                }
            }
        }
    }

    Connections {
        target: captureController
        function onScrollFrameAboutToCapture() { root.frameHidden = true }
        function onScrollFrameCaptured() { root.frameHidden = false }
        function onScrollStateChanged() { root.frameHidden = false }
    }
}
