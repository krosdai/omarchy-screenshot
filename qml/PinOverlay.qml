// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick

Item {
    id: root
    property rect screenRect: Qt.rect(0, 0, 0, 0)
    clip: true

    Repeater {
        model: pinnedImages
        delegate: Item {
            id: pin
            required property int imageId
            required property rect imageRect
            required property string imageSource
            required property int stackingOrder
            objectName: "pin_" + imageId
            x: imageRect.x - root.screenRect.x
            y: imageRect.y - root.screenRect.y
            width: imageRect.width
            height: imageRect.height
            z: stackingOrder
            readonly property bool onScreen: x < root.width && y < root.height &&
                                              x + width > 0 && y + height > 0

            Image {
                objectName: "pinImage_" + pin.imageId
                anchors.fill: parent
                source: pin.onScreen ? pin.imageSource : ""
                cache: false
                smooth: true
                mipmap: true
            }
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: 1
                border.color: "#99758291"
            }
            MouseArea {
                id: dragArea
                objectName: "pinDrag_" + pin.imageId
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                hoverEnabled: true
                preventStealing: true
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                function desktopPoint(mouse) {
                    const point = mapToItem(root, mouse.x, mouse.y)
                    return Qt.point(point.x + root.screenRect.x, point.y + root.screenRect.y)
                }
                onPressed: mouse => pinnedImages.beginDrag(pin.imageId, desktopPoint(mouse))
                onPositionChanged: mouse => {
                    if (pressed) pinnedImages.moveDrag(desktopPoint(mouse))
                }
                onReleased: pinnedImages.endDrag()
                onCanceled: pinnedImages.endDrag()
            }
            Rectangle {
                objectName: "pinClose_" + pin.imageId
                x: pin.width - width
                y: 0
                width: 28
                height: 28
                radius: 4
                color: closeArea.containsMouse ? "#e0d94a4a" : "#c026313d"
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Close pinned screenshot")
                Accessible.onPressAction: pinnedImages.close(pin.imageId)
                Text {
                    anchors.centerIn: parent
                    text: "×"
                    color: "#ffffff"
                    font.pixelSize: 22
                }
                MouseArea {
                    id: closeArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: pinnedImages.close(pin.imageId)
                }
            }
        }
    }
}
