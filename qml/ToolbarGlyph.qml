import QtQuick

Canvas {
    id: glyph
    required property string action
    property string accentColor: "#ff4b55"
    width: 20
    height: 20
    antialiasing: true
    onActionChanged: requestPaint()
    onAccentColorChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        ctx.clearRect(0, 0, width, height)
        ctx.strokeStyle = "#26313d"
        ctx.lineWidth = 1.8
        ctx.lineCap = "round"
        ctx.lineJoin = "round"
        ctx.beginPath()

        switch (action) {
        case "color":
            ctx.arc(10, 10, 7, 0, Math.PI * 2)
            ctx.fillStyle = accentColor
            ctx.fill()
            break
        case "select":
            ctx.moveTo(2.5, 2.5)
            ctx.lineTo(2.5, 16)
            ctx.lineTo(6, 12.5)
            ctx.lineTo(9.2, 17.5)
            ctx.lineTo(11.5, 16)
            ctx.lineTo(8.5, 10.8)
            ctx.lineTo(15.5, 10.8)
            ctx.closePath()
            ctx.fillStyle = "#ffffff"
            ctx.fill()
            break
        case "rect":
            ctx.rect(3, 3, 14, 14)
            break
        case "roundrect":
            ctx.moveTo(7, 3)
            ctx.lineTo(13, 3)
            ctx.quadraticCurveTo(17, 3, 17, 7)
            ctx.lineTo(17, 13)
            ctx.quadraticCurveTo(17, 17, 13, 17)
            ctx.lineTo(7, 17)
            ctx.quadraticCurveTo(3, 17, 3, 13)
            ctx.lineTo(3, 7)
            ctx.quadraticCurveTo(3, 3, 7, 3)
            break
        case "fillrect":
            ctx.fillStyle = accentColor
            ctx.fillRect(3, 3, 14, 14)
            break
        case "fillellipse":
            ctx.fillStyle = accentColor
            ctx.arc(10, 10, 7, 0, Math.PI * 2)
            ctx.fill()
            break
        case "ellipse":
            ctx.arc(10, 10, 7, 0, Math.PI * 2)
            break
        case "arrow":
            ctx.moveTo(3.2, 16.8)
            ctx.lineTo(16.8, 3.2)
            ctx.moveTo(10, 3.2)
            ctx.lineTo(16.8, 3.2)
            ctx.lineTo(16.8, 10)
            break
        case "curvedarrow":
        case "doublearrow":
            ctx.moveTo(3, 16)
            ctx.quadraticCurveTo(4, 3, 17, 4)
            ctx.moveTo(11, 3)
            ctx.lineTo(17, 4)
            ctx.lineTo(15, 10)
            if (action === "doublearrow") {
                ctx.moveTo(3, 16)
                ctx.lineTo(3, 10)
                ctx.moveTo(3, 16)
                ctx.lineTo(9, 16)
            }
            break
        case "line":
            ctx.moveTo(3, 17)
            ctx.lineTo(17, 3)
            break
        case "highlighter":
            ctx.lineWidth = 5
            ctx.strokeStyle = accentColor
            ctx.moveTo(3, 15)
            ctx.lineTo(17, 5)
            break
        case "spotlight":
            ctx.arc(10, 10, 5, 0, Math.PI * 2)
            ctx.rect(2, 2, 16, 16)
            break
        case "marker":
            ctx.arc(10, 10, 7, 0, Math.PI * 2)
            ctx.fillStyle = accentColor
            ctx.fill()
            ctx.fillStyle = "#ffffff"
            ctx.font = "bold 12px sans-serif"
            ctx.textAlign = "center"
            ctx.textBaseline = "middle"
            ctx.fillText("1", 10, 10)
            break
        case "pen":
            ctx.moveTo(3.2, 17)
            ctx.lineTo(4.2, 13.5)
            ctx.lineTo(13.1, 4.6)
            ctx.quadraticCurveTo(14.2, 3.5, 15.3, 4.6)
            ctx.lineTo(15.9, 5.2)
            ctx.quadraticCurveTo(17, 6.3, 15.9, 7.4)
            ctx.lineTo(7, 16.3)
            ctx.closePath()
            ctx.moveTo(4.2, 13.5)
            ctx.lineTo(7, 16.3)
            ctx.moveTo(13.1, 4.6)
            ctx.lineTo(15.9, 7.4)
            break
        case "text":
            ctx.moveTo(3.5, 17)
            ctx.lineTo(10, 3)
            ctx.lineTo(16.5, 17)
            ctx.moveTo(5.8, 12)
            ctx.lineTo(14.2, 12)
            break
        case "mosaic":
            ctx.fillStyle = "#4aa3ff"
            ctx.fillRect(3, 3, 7, 7)
            ctx.fillRect(10, 10, 7, 7)
            ctx.rect(3, 3, 14, 14)
            ctx.moveTo(10, 3)
            ctx.lineTo(10, 17)
            ctx.moveTo(3, 10)
            ctx.lineTo(17, 10)
            break
        case "undo":
            ctx.moveTo(4.5, 7)
            ctx.lineTo(10, 7)
            ctx.bezierCurveTo(15.2, 7, 17.4, 11.2, 15.2, 16)
            ctx.moveTo(8, 3.5)
            ctx.lineTo(4.5, 7)
            ctx.lineTo(8, 10.5)
            break
        case "redo":
            ctx.moveTo(15.5, 7)
            ctx.lineTo(10, 7)
            ctx.bezierCurveTo(4.8, 7, 2.6, 11.2, 4.8, 16)
            ctx.moveTo(12, 3.5)
            ctx.lineTo(15.5, 7)
            ctx.lineTo(12, 10.5)
            break
        case "copy":
            ctx.moveTo(6, 14)
            ctx.lineTo(3.5, 14)
            ctx.lineTo(3.5, 3.5)
            ctx.lineTo(14, 3.5)
            ctx.lineTo(14, 6)
            ctx.moveTo(7, 6)
            ctx.lineTo(16.5, 6)
            ctx.lineTo(16.5, 16.5)
            ctx.lineTo(7, 16.5)
            ctx.closePath()
            break
        case "save":
            ctx.moveTo(10, 3)
            ctx.lineTo(10, 13)
            ctx.moveTo(6.2, 9.4)
            ctx.lineTo(10, 13.2)
            ctx.lineTo(13.8, 9.4)
            ctx.moveTo(3.5, 15)
            ctx.lineTo(3.5, 17)
            ctx.lineTo(16.5, 17)
            ctx.lineTo(16.5, 15)
            break
        case "cancel":
            ctx.moveTo(4, 4)
            ctx.lineTo(16, 16)
            ctx.moveTo(16, 4)
            ctx.lineTo(4, 16)
            break
        }
        ctx.stroke()
    }
}
