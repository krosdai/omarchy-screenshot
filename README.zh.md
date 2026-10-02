# Omarchy Screenshot

[English](README.md) | 简体中文

为 Hyprland / Omarchy 编写的 Qt 6 截图与标注工具，支持冻结画面和跨屏选区。

## 功能

- 快速选中窗口、显示器或任意区域，支持跨屏截图和选区调整。
- 用图形、箭头、画笔、文字和编号标注截图，用马赛克遮挡隐私，支持撤销与重做。
- 滚动截取长页面，自动拼接长图，支持继续截图和标注。
- 一键复制或保存截图，也可双击选区快速复制。
- 自定义标注颜色和深浅主题，自动记住偏好。
- 支持 42 种语言／地区模式，适配多显示器、不同缩放和旋转屏幕。

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

窗口截图包含选区内当时可见的像素，包括其他窗口的遮挡部分。

## 界面语言

支持 42 种语言／地区模式，默认跟随系统界面语言；键盘布局不影响界面语言。可单独指定：

```sh
omarchy-screenshot --language zh_CN
OMARCHY_SCREENSHOT_LANGUAGE=ja omarchy-screenshot
```

优先级：`--language` > `OMARCHY_SCREENSHOT_LANGUAGE` > 系统语言（`LANGUAGE`、`LC_ALL`、`LC_MESSAGES`、`LANG`）。启动时生效，支持 `pt-BR`、`zh-Hant` 等标签，无匹配翻译时回退到英语；快捷键不变。完整语言列表见 [translations/](translations/)，字体建议安装 `noto-fonts` 和 `noto-fonts-cjk`。

翻译为 Qt Linguist `.ts` 文件，构建时内嵌到程序。修改界面文字后，运行 `cmake --build build --target update_translations` 并补齐翻译；保留 `%1` 占位符、程序名和快捷键。

## 构建与运行

需要 Hyprland Wayland 会话，支持 `x86_64` 和 `aarch64`。在 Omarchy 上从 AUR 安装：

```sh
omarchy pkg aur add omarchy-screenshot
```

在 `~/.config/hypr/bindings.lua` 中设置截图快捷键：

```lua
hl.unbind("CTRL + ALT + A")
o.bind("CTRL + ALT + A", "Omarchy Screenshot", "omarchy-screenshot")
```

从源码构建需要 `cmake`、`gcc`、`pkgconf`、`qt6-base`、`qt6-declarative`、`qt6-wayland`、`qt6-tools`、`layer-shell-qt`、`wayland`、`wayland-protocols`、`grim`、`wl-clipboard` 和 `hyprland`。默认测试还需要 `python`，可用 `-DBUILD_TESTING=OFF` 关闭。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/omarchy-screenshot
```

本地安装（同时配置常驻模式所需的 service 路径）：

```sh
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=$HOME/.local -DSYSTEMD_USER_UNIT_DIR=$HOME/.local/share/systemd/user
cmake --build build -j
cmake --install build
```

### 测试

`ctest --test-dir build --output-on-failure` 无需桌面会话，可检查翻译、滚动拼接、常驻进程及不同缩放下的图像采样。以下自检可用 `./build/omarchy-screenshot` 加对应参数运行：

| 参数 | 检查内容 |
| --- | --- |
| `--self-test` | 跨屏选区、撤销／重做和编号；需至少两块显示器 |
| `--ui-self-test --language he` | 工具栏、主题、标注、导出和翻译排版；使用临时设置 |
| `--scroll-stitch-test` | 滚动拼接 |
| `--scroll-ui-self-test` | 长图界面 |

### 常驻模式

默认关闭。开启后复用 Qt 和 GPU 初始化，在 5K 屏幕上将覆盖层出现时间从约 230 ms 缩短到 60–90 ms；首次截图仍需正常启动。空闲 10 分钟后自动退出，运行时约占 100 MB 内存和 320 MB 显存。

```sh
systemctl --user enable --now omarchy-screenshot.socket   # 开启
systemctl --user disable --now omarchy-screenshot.socket  # 关闭并停止后台进程
```

无 systemd 时可将 `omarchy-screenshot --daemon` 加入会话自启动。快捷键无需修改；仅无参数启动转交后台进程，`--language` 和自测独立运行。

运行 `systemctl --user edit omarchy-screenshot.service` 可修改空闲超时（秒，`0` 表示不退出），保留实际安装路径：

```ini
[Service]
ExecStart=
ExecStart=/usr/bin/omarchy-screenshot --daemon --idle-timeout 1800
```

Omarchy 的覆盖层淡入可能额外延迟最多 400 ms。要立即显示，在 `~/.config/hypr/hyprland.lua` 中加入：

```lua
hl.layer_rule({ match = { namespace = "^omarchy-screenshot$" }, no_anim = true, animation = "none" })
```

### AUR 发布

[PKGBUILD](PKGBUILD) 定义软件包；推送 `v主版本.次版本.修订号` 标签后，[GitHub Actions](.github/workflows/publish-aur.yml) 自动更新版本、校验和并提交到 AUR。首次发布需配置 `AUR_USERNAME`、`AUR_EMAIL`、`AUR_SSH_PRIVATE_KEY` Secrets，并将公钥添加到 AUR 账户。

## 许可证

Copyright (C) 2026 Andy Stewart。源码使用 [GPL-3.0-only](LICENSE)；随附的[虚拟指针协议](protocols/wlr-virtual-pointer-unstable-v1.xml)使用 MIT 许可证。
