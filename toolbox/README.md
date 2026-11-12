# 工具启动器 (Toolkit Launcher)

一个图吧工具箱风格的 Windows 工具启动器：左侧分组列表 + 搜索，右侧工具详情，点击即可启动外部工具。

> 直接下载打包好的版本：见 [Releases](https://github.com/61057a/toolbox-launcher/releases)。

- 原生 Win32 + GDI+ 自绘，单窗口、无第三方运行时
- 深色主题、DPI 自适应、键盘操作
- 两套低饱和配色（石墨 / 宣纸），标题栏按钮或 `T` 一键切换并记住
- 工具全部放在程序目录下的 `tools\` 中，通过 `tools.ini` 登记
- 新增工具不需要改代码
- 运行中检测：已在运行的工具显示绿色圆点，再次启动直接切到已有窗口
- 右键菜单：管理员运行、打开目录、复制路径、隐藏条目
- 使用统计与「最近使用」置顶分组
- 内嵌本地代理面板：查看 / 启动 / 停止 ProxyPool，启动 OpenCode 时自动接入代理

## 目录结构

```
toolbox/
├─ src/                 源码
├─ build/               编译中间文件
├─ dist/                成品目录（可直接拷走使用）
│  ├─ Toolkit.exe
│  ├─ tools.ini         工具清单
│  ├─ settings.ini      界面设置（主题 / 隐藏条目 / 自动代理，程序自动生成）
│  ├─ stats.ini         使用统计（启动次数 / 最后启动时间，程序自动生成）
│  └─ tools/
│     ├─ Everything/    Everything 文件搜索
│     ├─ 7-Zip/         7-Zip 压缩工具
│     ├─ WizTree/       WizTree 磁盘空间分析
│     ├─ DiskGenius/    DiskGenius 分区工具
│     ├─ MyDiskTest/    MyDiskTest 扩容检测
│     ├─ MofangRecovery/魔方数据恢复
│     ├─ FinalData/     FinalData 数据恢复
│     ├─ Geek/          Geek Uninstaller
│     ├─ Dism++/        Dism++ 系统维护
│     ├─ DirectXRepair/ DirectX 修复工具
│     ├─ FluxDown/      FluxDown 下载器
│     ├─ VSCode/        Visual Studio Code
│     ├─ DevCpp/        Dev-C++
│     ├─ OpenCode/      OpenCode 便携版
│     └─ ProxyPool/     ProxyPool 免费代理池
└─ build.bat            编译脚本
```

## 使用

双击 `dist\Toolkit.exe`。

| 操作 | 说明 |
| --- | --- |
| 单击工具 | 选中并在右侧显示详情 |
| 双击工具 / 回车 | 启动 |
| 数字键 1–9 | 快速选中对应工具 |
| ↑ / ↓ | 切换工具 |
| `/` 或 `Ctrl+F` | 聚焦搜索框 |
| `Esc` | 清空 / 退出搜索 |
| `T` 或标题栏圆环按钮 | 切换主题（石墨 / 宣纸），自动记住 |
| `F5` | 重新读取 `tools.ini` |
| 鼠标滚轮 | 滚动左侧列表 |
| 右键 / `Shift+F10` | 打开工具菜单（启动、管理员运行、打开目录、复制、隐藏等） |
| 标题栏拖动 | 移动窗口，四边/四角拖动可缩放 |

## 添加工具

1. 把工具程序放到 `dist\tools\<名称>\` 目录下（也可以放任意位置，用绝对路径）。
2. 在 `dist\tools.ini` 末尾追加一节：

```ini
[Notepad]
name=记事本
category=常用工具
icon=N
desc=系统自带文本编辑器
path=tools\Notepad\notepad.exe
args=
```

字段说明：

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `name` | 否 | 显示名称，缺省用节名 |
| `category` | 否 | 分组名，缺省为「其他」 |
| `icon` | 否 | 列表里的单字图标，缺省取名称首字 |
| `desc` | 否 | 右侧详情里的描述 |
| `path` | 是 | 程序路径；相对路径基于 `Toolkit.exe` 所在目录，也支持绝对路径、`https://` 链接、`.lnk` |
| `args` | 否 | 启动参数 |

3. 按 `F5` 或点标题栏的刷新按钮即可看到新工具，无需重启。

## 右键菜单

在左侧列表、右侧详情或标题栏上点右键（键盘可用 `Shift+F10` / 菜单键）：

| 菜单项 | 说明 |
| --- | --- |
| 启动 / 切换到窗口 | 未运行时启动；已在运行时把已有窗口切到前台 |
| 以管理员身份运行 | 通过 UAC 提权启动（网页链接除外） |
| 再启动一个 | 明确要求再开一个实例，跳过「切到已有窗口」 |
| 打开所在目录 | 在资源管理器中定位该文件 |
| 复制路径 / 复制名称 | 复制到剪贴板 |
| 隐藏该条目 | 从列表移除，但不修改 `tools.ini` |
| 刷新列表 | 等价于 `F5` |
| 取消隐藏全部 | 恢复所有被隐藏的条目 |
| 显示隐藏的条目 | 在标题栏 / 空白处右键的菜单里，临时显示隐藏项 |
| 切换主题 | 等价于 `T` |

菜单是自绘的，跟随石墨 / 宣纸主题配色。

## 运行中检测

启动器每 1.5 秒扫描一次进程：

- 已运行的工具在列表右侧显示绿色圆点，详情页状态显示「运行中」，按钮变成「切换到窗口」
- 此时再按回车 / 双击 / 点启动，会尝试把已有窗口切到前台，而不是再开一个
- 找不到可切换窗口时（例如控制台程序跑在 Windows Terminal 里）只提示「已在运行」，不会重复启动
- 需要刻意多开时，用右键菜单的「再启动一个」

`.exe` 按完整路径匹配；`.bat` / `.cmd` 按「进程位于该脚本所在目录下」匹配，
因此 OpenCode 这类通过批处理启动的工具也能识别到它目录里的 `opencode.exe`。

## 本地代理面板

窗口右下角固定有一张「本地代理」卡片，把工具箱里的 ProxyPool 收进来：

| 元素 | 说明 |
| --- | --- |
| 状态 | 「在线」= `127.0.0.1:8080` 正在监听；「启动中」= 已拉起进程、正在等端口；「未启动」 |
| 启动代理池 / 打开代理池窗口 | 未启动时拉起 `tools\ProxyPool\proxypool-gui.exe`；已在线时把它的窗口切到前台 |
| 停止 | 结束 `proxypool-gui.exe` / `proxypool.exe`，并等 8080 释放 |
| 自动代理开关 | 开启时，启动 OpenCode 前若 8080 没在监听就先拉起代理池 |

开关状态保存在 `dist\settings.ini`：

```ini
[proxy]
auto=1
port=8080
```

- `auto=0` 时启动器不再自动拉起代理池；但 8080 已经在监听时，`start-opencode.bat` 仍会给 OpenCode 设置代理环境变量
- `port` 可选，缺省 8080，只影响面板的探测与启停判断
- 面板只负责「拉起进程」，不阻塞等待端口；OpenCode 的批处理自己轮询 8080，最多等约 12 秒
- 代理池没在运行时启动 OpenCode，会把它自己的窗口显示出来，方便看抓取 / 校验日志

### 固定出口 + 额度用尽自动换出口

从启动器打开 OpenCode 时，启动器会让代理池**固定一个出口节点**，而不是按请求轮换：

- 代理池就绪后调用本机控制接口 `/fixed`，把出口钉在一个健康节点上；整个会话所有请求
  都走同一个出口 IP，免费额度也就按这个 IP 稳定累计，不会因为轮换而跳来跳去
- 启动器每 2 秒读取便携版数据库
  `dist\tools\OpenCode\.local\share\opencode\opencode.db`（只读，走系统自带的
  `winsqlite3.dll`），只看 `session_message` 里 `type='idle'` 的失败记录
- 出现 `provider.free-limit`（OpenCode 会提示
  `You've used today's free messages. They reset at ...`）时：
  1. 把当前出口按错误里的 `retryAfterMs`（约 12 小时）记为「额度用尽」，期间不再选它
  2. 调用 `/rotate` 换到下一个健康节点，面板显示
     `固定出口 1.2.3.4:8080 · 额度用尽自动换（已换 N 次）`
  3. 在 OpenCode 里**再发一条消息**即可继续（不会自动重发）
- 代理池刷新后如果固定节点还在列表里就继续固定；被刷掉、或者你在代理池窗口里切回了
  自动轮换，启动器发现后会重新固定
- 没有可用备用节点时只提示，不会把出口换回已用尽的节点

控制接口只绑 `127.0.0.1`，端口默认是代理端口 + 1（8080 → 8081）：

| 接口 | 作用 |
| --- | --- |
| `GET /status` | 查看固定节点、池大小、转发计数、冷却节点数 |
| `GET /fixed` | 固定到一个健康节点（已固定则保持不变） |
| `GET /rotate?dead=IP:端口&ttl=秒` | 换下一个节点，并把 `dead` 冷却 `ttl` 秒 |
| `GET /auto` | 恢复自动轮换 |

接口只在代理池 GUI / 命令行启动时自动开启；端口被占用时会在日志里提示，固定出口不可用。

## 使用统计与最近使用

每次成功启动会写入 `dist\stats.ini`：

```ini
[OpenCode]
count=3
last=134388495878417289
```

- `count` 是累计启动次数，`last` 是最后一次启动时间（UTC FILETIME）
- 详情页显示「启动 N 次 · 上次 YYYY-MM-DD HH:mm」
- 非搜索状态下，列表顶部会有「最近使用」分组，按时间倒序显示最近 5 个工具
- 删除 `stats.ini` 即可清空统计

## 隐藏工具

右键工具选择「隐藏该条目」后，该工具会从列表中消失，但 `tools.ini` 保持原样。
隐藏状态保存在 `dist\settings.ini`：

```ini
[ui]
hidden=OpenCode,MofangRecovery
```

在标题栏或搜索框区域右键，勾选「显示隐藏的条目」可临时查看；
选择「取消隐藏全部」会清空隐藏列表。

## 编译

需要 `C:\Program Files (x86)\Dev-Cpp\MinGW64\bin\g++.exe`（g++ 4.9.2，C++11）。

```bat
build.bat
```

输出：`dist\Toolkit.exe`。

## 当前已配置

- **Everything 文件搜索** — 毫秒级本地文件名搜索（`dist\tools\Everything\`）
- **WizTree 磁盘空间分析** — 直接读 NTFS 索引，秒级扫描占用（`dist\tools\WizTree\`）
- **DiskGenius 分区工具** — 分区管理 / 数据恢复 / 坏道检测（`dist\tools\DiskGenius\`）
- **MyDiskTest 扩容检测** — U 盘 / 存储卡扩容盘检测（`dist\tools\MyDiskTest\`）
- **魔方数据恢复** — 误删文件恢复（`dist\tools\MofangRecovery\`）
- **FinalData 数据恢复** — 老牌数据恢复工具（`dist\tools\FinalData\`）
- **Geek Uninstaller** — 轻量软件卸载 / 残留清理（`dist\tools\Geek\`）
- **Dism++ 系统维护** — 系统映像管理 / 清理 / 驱动（`dist\tools\Dism++\`，含 x64 / x86 / ARM64 三个版本）
- **DirectX 修复工具** — 检测修复 DirectX / VC 运行库（`dist\tools\DirectXRepair\`）
- **FluxDown 下载器** — 多线程下载器（`dist\tools\FluxDown\`）
- **Visual Studio Code** — 代码编辑器（`dist\tools\VSCode\`）
- **Dev-C++ 编译器** — 经典 C/C++ IDE，内置 MinGW64（`dist\tools\DevCpp\`）
- **OpenCode 便携版** — AI 编程终端 v2.2.7，配置 / skills / 会话数据全在包内（`dist\tools\OpenCode\`）
- **ProxyPool 免费代理池** — 本地 8080 代理池，启动器面板可启停，OpenCode 自动接入（`dist\tools\ProxyPool\`）

> OpenCode 便携版的后台服务固定用 `127.0.0.1:4010`，避开系统里 npm 版占用的 28894。
> 端口保存在 `dist\tools\OpenCode\.config\opencode\service.json`；该文件丢失时，`start-opencode.bat` 会在启动前自动重建为 4010。
> 升级方式：下载新的 `opencode-windows-x64.zip`，用其中的 `opencode.exe` 覆盖同名文件，其余文件保留。

> 需要什么工具告诉我，我会把程序放进 `dist\tools\` 并在 `tools.ini` 里加好。

## 图标

启动器会自动从 `path` 指向的 exe / lnk / 文件夹中提取真实系统图标（优先 64px 内嵌图标），
在左侧列表和右侧详情页中显示。没有可用图标时，回退到 `icon` 字段里的单字图标。

如果 `path` 指向 `.bat` / `.cmd` 这类没有内嵌图标的脚本，可以额外写一行 `iconfile` 指定图标来源：

```ini
[OpenCode]
path=tools\OpenCode\start-opencode.bat
iconfile=tools\OpenCode\opencode.exe
```

`iconfile` 规则与 `path` 相同（相对 `Toolkit.exe` 所在目录或绝对路径），提取失败时仍会退回 `path` 的图标。

程序自身的图标内嵌在 `Toolkit.exe` 里，来源是 `src\app.ico`（16–256 共 9 个尺寸），
由 `src\app.rc` 编译进资源（`build.bat` 里用 `windres` 处理），窗口类同时设置了 `hIcon` / `hIconSm`。
