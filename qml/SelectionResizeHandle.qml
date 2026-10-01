// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick

Rectangle {
    // Keep the handle in screen coordinates; only its position follows the image.
    readonly property int hitRadius: 15
    width: 9
    height: 9
    color: "white"
    border.color: "#397eb8"
    border.width: 1
}
