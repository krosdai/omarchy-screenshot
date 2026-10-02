# Omarchy Screenshot

[English](README.md) | 简体中文

为 Hyprland / Omarchy 编写的 Qt 6 截图工具。截图前抓取每块显示器的画面，然后在所有显示器上显示冻结的覆盖层。选区使用 Hyprland 全局坐标，因此可以从一块屏幕拖到另一块屏幕。

## 功能

- 快速选中窗口、显示器或任意区域，支持跨屏截图和选区调整。
- 用图形、箭头、画笔、文字和编号标注截图，用马赛克遮挡隐私，支持撤销与重做。
- 滚动截取长页面，自动拼接长图，支持继续截图和标注。
- 一键复制或保存截图，也可双击选区快速复制。
- 自定义标注颜色和深浅主题，自动记住偏好。
- 支持 42 种语言／地区模式，适配多显示器、不同缩放和旋转屏幕。

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

在 Arch / Omarchy 上需要 `cmake`、`gcc`、`pkgconf`、`qt6-base`、`qt6-declarative`、`qt6-wayland`、`qt6-tools`、`layer-shell-qt`、`wayland`、`wayland-protocols`、`grim`、`wl-clipboard` 和 `hyprland`。程序通过 `ext-image-copy-capture-v1` 协议直接抓取显示器画面；合成器不支持该协议或显示器经过旋转时改用 `grim`。滚动截图使用 Hyprland 暴露的 wlr 虚拟指针协议。默认构建自动化测试还需要 `python`；只构建程序时可传入 `-DBUILD_TESTING=OFF`。

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

运行 `ctest --test-dir build --output-on-failure`，无需桌面会话即可检查所有翻译的完整性、占位符、内嵌加载，以及语言优先级／回退；同时离线检查 1、1.25、1.5 和 2 倍缩放下的光标图标抓取和逻辑坐标像素采样。缩放测试使用独立进程和软件渲染，不修改桌面缩放。`--ui-self-test` 中的光标、马赛克和画笔预览检查也会把抓取图像转换为逻辑尺寸后采样，可直接在 2 倍缩放的 Wayland 会话中运行。

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
| S | 保存截图到图片目录（设置 `OMARCHY_SCREENSHOT_DIR` 时保存到该目录）并退出 |
| Esc / 右键 | 退出 |

窗口识别用于选择屏幕上的几何区域，截图内容为当时显示在该区域内的像素。被其他窗口遮挡的部分仍会显示遮挡窗口；这一点与单独导出窗口缓冲区的功能不同。

## 许可证

Copyright (C) 2026 Andy Stewart。本项目源码按 GNU 通用公共许可证第 3 版（`GPL-3.0-only`）发布，许可证全文见 [LICENSE](LICENSE)。随项目提供的[虚拟指针协议定义](protocols/wlr-virtual-pointer-unstable-v1.xml)使用 MIT 许可证，许可证文本包含在该文件中。
