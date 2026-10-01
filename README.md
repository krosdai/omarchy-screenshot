# Omarchy Screenshot

为 Hyprland / Omarchy 编写的 Qt 6 截图工具。截图前抓取每块显示器的画面，然后在所有显示器上显示冻结的覆盖层。选区使用 Hyprland 全局坐标，因此可以从一块屏幕拖到另一块屏幕。

## 功能

- 鼠标悬停时识别当前工作区的窗口及所在显示器，单击吸附到对应区域。
- 拖动选择任意矩形，支持跨显示器；选中后可以拖动选区或拖动边缘调整大小。
- 选区工具 V 显示八个方形调整点，普通截图与长图共用 9×9 的白底蓝边方块，四角和四边中点都有 31×31 的鼠标响应区域。长图方块不随图片缩放，也不会被图片容器裁掉；方向键向对应方向扩展 1px，Shift+方向键向内收缩 1px，长按可连续调整；切换工具后调整点隐藏。
- 矩形、椭圆、箭头、画笔、文字、矩形马赛克、直线、荧光笔、聚光灯、编号标记、圆角/实心图形及弯曲/双向箭头标注，支持撤销与重做。马赛克拖动时显示临时边框，松开后边框消失。
- 矩形、圆形、箭头、画笔各占一个主按钮；直线归入箭头组，聚光灯归入圆形组。点击主按钮后在只显示图标的二级工具栏中选择样式；也可直接在画面空白处开始标注，二级工具栏会自动关闭。主按钮显示本组当前图形，键盘快捷键可恢复该样式。
- 画笔会对鼠标采样点去抖，并用平滑的二次贝塞尔曲线连接；预览和导出使用相同的轨迹。
- 文字编辑使用透明背景和虚线边框；每行垂直居中，支持 Shift+Enter 换行。
- 工具栏最左侧可选择绘制颜色；可点选预设色，或拖动调色盘的色相条和颜色区域选择任意颜色。颜色自动保存，下次启动时恢复。新画的图形、画笔和文字及马赛克拖拽时的临时边框使用新颜色，已有标注保留原色。
- 工具栏采用 QQ 截图风格的细线图标；调色盘顶部可切换深色或浅色工具栏，选择会保存。工具选中背景与快捷键、图标之间留有空隙。
- 标注快捷键位于 QWERTY 键盘左手区域；滚动截图与继续截图使用 H，长图放大、缩小使用 =、-。工具栏按钮显示快捷键和图标，鼠标悬停可查看功能说明。
- 普通截图和滚动长截图均不包含鼠标光标。可复制 PNG 到 Wayland 剪贴板，或保存到图片目录。设置 `OMARCHY_SCREENSHOT_DIR` 可更改保存目录。
- 尚未标注时可点击工具栏的滚动截图按钮：一个窗口直接开始，多个窗口先选择目标。选择时，选区附近提示“点击一个窗口，开始长截图”，各窗口蒙版标明“点击开始长截图”，悬停会高亮目标；单击窗口直接开始，Esc 取消。软件只在该窗口与选区的交集内取图，使用虚拟滚轮分段滚动并自动拼接。固定页眉和页脚会尽量去重，无法可靠拼接时保留已有长图。
- 滚动中不显示截图预览，工具栏提示“点击鼠标停止截图”；在截图区域空白处单击即可停止，移动鼠标不会停止。完成后长图在屏幕内适配显示，高度不超过屏幕的 4/5；放大后，选区工具 V 下可左键拖动长图内部上下查看，拖动边框或八个调整点仍调整选区，标注工具仍用于绘制。也可滚轮平移、Ctrl+滚轮缩放、中键拖动，继续标注、复制与保存。长图工具栏提供“继续截图”，原有标注保留；若窗口未响应，先点击实际滚动区域，再点“继续滚动”。
- 普通截图与长图共用工具栏样式、分组图标和调色盘；普通截图的滚动按钮位于编号标记右侧，长图的缩小、放大和“继续截图”依次位于编号标记与撤销之间。滚动中的停止提示持续显示，完成后的顶部提示使用工具栏的中性背景。
- 多语言界面：工具提示、工具名称、主题名称和错误信息跟随系统语言；支持长文本换行和希伯来语右向左显示。
- 处理负坐标、旋转显示器和不同缩放比例；导出时按各屏图像拼接。

## 界面语言

截至 2026 年 9 月 30 日，Omarchy 上游的[安装选项](https://github.com/omacom/omarchy/blob/8b4eae66da2938ba9559f103b18dbf85cdf28a70/install/provisioning/setup-form.sh#L32-L102)提供的是 48 种**键盘布局**，不是界面语言；安装器默认系统语言为英语。本工具覆盖这些布局对应的所有语言，并保留简体和繁体中文支持，共提供 42 种语言／地区模式（英语原文及 41 份翻译）。键盘布局不会改变界面语言。

默认按 Qt 的系统界面语言偏好选择翻译，遵循 `LANGUAGE`、`LC_ALL`、`LC_MESSAGES` 和 `LANG`。也可以只为本工具指定语言，不修改系统设置：

```sh
omarchy-screenshot --language zh_CN
omarchy-screenshot --language de
OMARCHY_SCREENSHOT_LANGUAGE=ja omarchy-screenshot
```

优先级为 `--language` > `OMARCHY_SCREENSHOT_LANGUAGE` > 系统语言偏好。语言在启动时选定；下次启动时读取新的设置。支持 `pt-BR`、`zh-Hant` 等 Qt 语言标签；没有对应翻译时回退到英语。快捷键不随翻译改变。

| 语言 | 代码 | 语言 | 代码 |
| --- | --- | --- | --- |
| 英语 | `en` | 阿塞拜疆语 | `az` |
| 白俄罗斯语 | `be` | 保加利亚语 | `bg` |
| 克罗地亚语 | `hr` | 捷克语 | `cs` |
| 丹麦语 | `da` | 荷兰语 | `nl` |
| 爱沙尼亚语 | `et` | 芬兰语 | `fi` |
| 法语 | `fr` | 格鲁吉亚语 | `ka` |
| 德语 | `de` | 希腊语 | `el` |
| 希伯来语 | `he` | 匈牙利语 | `hu` |
| 冰岛语 | `is` | 爱尔兰语 | `ga` |
| 意大利语 | `it` | 日语 | `ja` |
| 哈萨克语 | `kk` | 吉尔吉斯语 | `ky` |
| 老挝语 | `lo` | 拉脱维亚语 | `lv` |
| 立陶宛语 | `lt` | 马其顿语 | `mk` |
| 挪威语（博克马尔） | `nb` | 波兰语 | `pl` |
| 葡萄牙语（葡萄牙） | `pt_PT` | 葡萄牙语（巴西） | `pt_BR` |
| 罗马尼亚语 | `ro` | 俄语 | `ru` |
| 塞尔维亚语（西里尔字母） | `sr` | 斯洛伐克语 | `sk` |
| 斯洛文尼亚语 | `sl` | 西班牙语 | `es` |
| 瑞典语 | `sv` | 塔吉克语 | `tg` |
| 土耳其语 | `tr` | 乌克兰语 | `uk` |
| 简体中文 | `zh_CN` | 繁体中文 | `zh_TW` |

英国英语使用英语原文；加拿大／瑞士法语、瑞士德语和拉丁美洲西班牙语分别使用法语、德语和西班牙语翻译。葡萄牙语和中文区分地区／文字形式。不同文字的字体由系统提供，建议安装 `noto-fonts` 和 `noto-fonts-cjk`。

翻译使用 Qt Linguist 的 `.ts` 格式，位于 `translations/`；构建时编译为 `.qm` 并内嵌到程序，安装时不需要另外复制翻译目录。添加或修改界面文字后，运行 `cmake --build build --target update_translations` 更新目录，再补齐每份翻译；不要翻译 `%1` 占位符、程序名或快捷键。

## 构建与运行

在 Arch / Omarchy 上需要 `cmake`、`gcc`、`pkgconf`、`wayland`、`wayland-protocols`、`qt6-base`、`qt6-declarative`、`qt6-wayland`、`qt6-tools`、`layer-shell-qt`、`grim`、`wl-clipboard` 和 `hyprland`。滚动截图使用 Hyprland 暴露的 wlr 虚拟指针协议。程序通过 `ext-image-copy-capture-v1` 协议直接抓取显示器画面；合成器不支持该协议或显示器经过旋转时改用 `grim`。默认构建自动化测试还需要 `python`；只构建程序时可传入 `-DBUILD_TESTING=OFF`。

支持 `x86_64` 和 `aarch64`（64 位 ARM）架构。在 ARM 机器上使用相同的构建命令，CMake 会使用本机工具链生成 ARM 可执行文件；`PKGBUILD` 也声明了这两种架构，无需通过 `makepkg --ignorearch` 跳过架构检查。

可在 Omarchy 上从 AUR 安装：

```sh
omarchy pkg aur add omarchy-screenshot
```

AUR 软件包会从对应的 GitHub 版本标签下载源码、编译并安装 `omarchy-screenshot`。

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

运行 `ctest --test-dir build --output-on-failure`，无需桌面会话即可检查所有翻译的完整性、占位符、内嵌加载和语言优先级／回退。

连接至少两块显示器时，可运行 `./build/omarchy-screenshot --self-test`，在内存中检查跨屏选区、撤销与重做及编号标记，不显示覆盖层或保存图片。

可运行 `./build/omarchy-screenshot --ui-self-test --language he`，短暂打开覆盖层，模拟工具栏与调色盘操作、深浅主题切换、拖动矩形马赛克和画笔，检查设置保存、选中留白、预览、导出结果、文字输入和窄屏翻译排版，然后自动退出。自检使用临时设置目录，不会改动平时保存的颜色和主题。可设置 `OMARCHY_SCREENSHOT_TEST_ARTIFACT_DIR` 导出深浅主题的界面局部截图；截图不包含桌面捕获像素。

滚动拼接和长图界面可分别运行 `./build/omarchy-screenshot --scroll-stitch-test` 与 `./build/omarchy-screenshot --scroll-ui-self-test` 检查。

工具栏的离线交互检查可运行 `QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= QT_QUICK_BACKEND=software /usr/lib/qt6/bin/qmltestrunner -input tests`，覆盖分组菜单、调色盘、主题一致性和按钮位置。

也可执行 `cmake --install build --prefix ~/.local`，安装到 `~/.local/bin/omarchy-screenshot`。需要在 Hyprland Wayland 会话中运行。

### AUR 发布

仓库根目录的 [PKGBUILD](PKGBUILD) 定义了 AUR 包。`.github/workflows/publish-aur.yml` 沿用 lazycat-terminal 的发布方式：推送 `v主版本.次版本.修订号` 标签后，GitHub Actions 更新软件包版本和源码校验和，并提交到 AUR。首次发布前，需要在 GitHub 仓库中配置 `AUR_USERNAME`、`AUR_EMAIL` 和 `AUR_SSH_PRIVATE_KEY` 三个 Actions Secrets，并确保对应的公钥已添加到 AUR 账户。

## 操作

| 操作 | 结果 |
| --- | --- |
| 悬停后单击 | 选中窗口或显示器 |
| 未标注时按 H / 点击滚动截图图标 | 开始滚动截图；多个窗口时先选择目标 |
| 滚动中单击截图区域空白处 | 停止并打开长图；仍可点击“继续截图”延长 |
| 长图中按 H / 点击继续截图图标 | 继续向下截图，保留已有标注 |
| 长图中按 = / - | 放大 / 缩小 |
| 放大后的长图中，用 V 工具左键拖动内部空白区域 | 平移查看长图内容；边框和八个调整点仍用于调整选区 |
| 长图中滚轮 / Ctrl+滚轮 / 中键拖动 | 平移 / 缩放 / 拖动画面 |
| 拖动 | 自由选区；选中后可移动或调整选区 |
| 在普通截图或长图的选区空白处双击 | 将当前选区和已有标注复制到剪贴板并关闭界面；长图按原始分辨率导出，不受预览缩放或滚动位置影响 |
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
| Esc / 右键 | 退出 |

窗口识别用于选择屏幕上的几何区域，截图内容为当时显示在该区域内的像素。被其他窗口遮挡的部分仍会显示遮挡窗口；这一点与单独导出窗口缓冲区的功能不同。

## 许可证

Copyright (C) 2026 Andy Stewart。本项目源码按 GNU 通用公共许可证第 3 版（`GPL-3.0-only`）发布，许可证全文见 [LICENSE](LICENSE)。随项目提供的 [虚拟指针协议定义](protocols/wlr-virtual-pointer-unstable-v1.xml) 使用 MIT 许可证，许可证文本包含在该文件中。
