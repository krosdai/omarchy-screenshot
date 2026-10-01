// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

import QtQuick
import "../qml" as Screenshot

Item {
    width: 64
    height: 64
    Screenshot.ToolbarGlyph {
        objectName: "cursorGlyph"
        action: "select"
    }
}
