# Omarchy Screenshot

为 Hyprland / Omarchy 编写的 Qt 6 截图工具。截图前抓取每块显示器的画面，然后在所有显示器上显示冻结的覆盖层。选区使用 Hyprland 全局坐标，因此可以从一块屏幕拖到另一块屏幕。

## 功能

- 鼠标悬停时识别当前工作区的窗口及所在显示器，单击吸附到对应区域。
- 拖动选择任意矩形，支持跨显示器；选中后可以拖动选区或拖动边缘调整大小。
- 选区工具 V 显示八个方形调整点，四角和四边中点都可用较大的鼠标响应区域调整选区；方向键向对应方向扩展 1px，Shift+方向键向内收缩 1px，长按可连续调整；切换工具后调整点隐藏。
- 矩形、椭圆、箭头、画笔、文字、矩形马赛克、直线、荧光笔、聚光灯、编号标记、圆角/实心图形及弯曲/双向箭头标注，支持撤销与重做。马赛克拖动时显示临时边框，松开后边框消失。
- 矩形、圆形、箭头、画笔各占一个主按钮；直线归入箭头组，聚光灯归入圆形组。点击主按钮后在只显示图标的二级工具栏中选择样式；主按钮显示本组当前图形，键盘快捷键可恢复该样式。
- 画笔会对鼠标采样点去抖，并用平滑的二次贝塞尔曲线连接；预览和导出使用相同的轨迹。
- 文字编辑使用透明背景和虚线边框；每行垂直居中，支持 Shift+Enter 换行。
- 工具栏最左侧可选择绘制颜色；可点选预设色，或拖动调色盘的色相条和颜色区域选择任意颜色。颜色自动保存，下次启动时恢复。新画的图形、画笔和文字及马赛克拖拽时的临时边框使用新颜色，已有标注保留原色。
- 工具栏采用 QQ 截图风格的细线图标；调色盘顶部可切换深色或浅色工具栏，选择会保存。工具选中背景与快捷键、图标之间留有空隙。
- 操作快捷键位于 QWERTY 键盘左手区域；鼠标悬停工具栏图标可查看功能说明。识字保留 F 快捷键，不在工具栏显示。
- 复制 PNG 到 Wayland 剪贴板，或保存到图片目录。设置 `OMARCHY_SCREENSHOT_DIR` 可更改保存目录。
- 可选 OCR：安装 `tesseract` 后将识别文本复制到剪贴板；安装 `tesseract-data-chi_sim` 后自动识别中英文。
- 处理负坐标、旋转显示器和不同缩放比例；导出时按各屏图像拼接。

## 构建与运行

在 Arch / Omarchy 上需要 `cmake`、`gcc`、`qt6-base`、`qt6-declarative`、`qt6-wayland`、`layer-shell-qt`、`grim`、`wl-clipboard` 和 `hyprland`。

可在 Omarchy 上从 AUR 安装：

```sh
omarchy pkg aur add omarchy-screenshot
```

AUR 软件包会从对应的 GitHub 版本标签下载源码、编译并安装 `omarchy-screenshot`。可选安装 `tesseract` 和 `tesseract-data-chi_sim` 来使用 OCR。

安装后，可在 `~/.config/hypr/bindings.lua` 中把 `Ctrl+Alt+A` 绑定为截图快捷键；如果已有同键绑定，先取消旧绑定：

```lua
hl.unbind("CTRL + ALT + A")
o.bind("CTRL + ALT + A", "Omarchy Screenshot", "omarchy-screenshot")
```

如果从当前源码手动构建：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/omarchy-screenshot
```

连接至少两块显示器时，可运行 `./build/omarchy-screenshot --self-test`，在内存中检查跨屏选区、撤销与重做及编号标记，不显示覆盖层或保存图片。

可运行 `./build/omarchy-screenshot --ui-self-test`，短暂打开覆盖层，模拟工具栏与调色盘操作、深浅主题切换、拖动矩形马赛克和画笔，检查设置保存、选中留白、预览、导出结果及文字输入，然后自动退出。自检使用临时设置目录，不会改动平时保存的颜色和主题。

也可执行 `cmake --install build --prefix ~/.local`，安装到 `~/.local/bin/omarchy-screenshot`。需要在 Hyprland Wayland 会话中运行。

### AUR 发布

仓库根目录的 [PKGBUILD](PKGBUILD) 定义了 AUR 包。`.github/workflows/publish-aur.yml` 沿用 lazycat-terminal 的发布方式：推送 `v主版本.次版本.修订号` 标签后，GitHub Actions 更新软件包版本和源码校验和，并提交到 AUR。首次发布前，需要在 GitHub 仓库中配置 `AUR_USERNAME`、`AUR_EMAIL` 和 `AUR_SSH_PRIVATE_KEY` 三个 Actions Secrets，并确保对应的公钥已添加到 AUR 账户。

## 操作

| 操作 | 结果 |
| --- | --- |
| 悬停后单击 | 选中窗口或显示器 |
| 拖动 | 自由选区；选中后可移动或调整选区 |
| 在选区空白处双击 | 将当前截图复制到剪贴板并关闭界面 |
| V / T / G / B / W | 选区 / 文字 / 矩形马赛克 / 编号标记 / 直线 |
| R / E / A / D | 激活上次选择的矩形 / 圆形 / 箭头（含直线） / 画笔（含荧光笔）样式；点击主按钮展开该组图标栏 |
| Shift+B | 聚光灯 |
| Shift+R / Shift+D / Shift+E | 圆角矩形 / 实心矩形 / 实心椭圆 |
| Shift+A / Shift+W | 弯曲箭头 / 双向弯曲箭头 |
| Q / 工具栏左侧颜色按钮 | 选择预设色，或拖动调色盘选色；自动保存 |
| 调色盘顶部太阳 / 月亮图标 | 切换浅色 / 深色工具栏；自动保存 |
| V 模式下方向键 / Shift+方向键 | 对应边框扩展 / 收缩 1px；长按连续调整 |
| 文字工具内单击 | 输入文字；Shift+Enter 换行，Enter 或点到别处确认 |
| 文字工具内按 Alt | 确认已输入文字，返回上一个工具 |
| Z | 撤销上一步标注 |
| X | 重做已撤销的标注 |
| C | 复制截图并退出 |
| S | 保存截图并退出 |
| F | 识别选区文字并复制，然后退出 |
| Esc / 右键 | 退出 |

窗口识别用于选择屏幕上的几何区域，截图内容为当时显示在该区域内的像素。被其他窗口遮挡的部分仍会显示遮挡窗口；这一点与单独导出窗口缓冲区的功能不同。

## 许可证

Copyright (C) 2026 Andy Stewart。本项目源码按 GNU 通用公共许可证第 3 版（`GPL-3.0-only`）发布，许可证全文见 [LICENSE](LICENSE)。
