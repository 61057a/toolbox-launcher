# Toolkit Launcher v1.0.0

图吧工具箱风格的 Windows 工具启动器：左侧分组列表 + 搜索，右侧工具详情，点一下启动外部工具。

## 特点

- 原生 Win32 + GDI+ 全自绘，单窗口，无第三方运行时，双击即用
- 两套低饱和配色：**石墨**（暖炭灰 + 铜色）/ **宣纸**（米白 + 松绿），标题栏圆环按钮或 `T` 键切换并自动记忆
- 自动提取工具 exe 的真实系统图标
- DPI 自适应，全部直角设计
- 工具清单由 `tools.ini` 驱动，改完按 `F5` 即时生效，加工具不用重新编译

## 快捷键

| 操作 | 说明 |
| --- | --- |
| 单击 / 双击 | 选中 / 启动 |
| 数字 1–9 | 快速选中 |
| Enter | 启动当前工具 |
| ↑ / ↓ | 切换工具 |
| `/` 或 `Ctrl+F` | 聚焦搜索 |
| `Esc` | 清空搜索 |
| `T` | 切换主题 |
| `F5` | 重新读取 tools.ini |

## 本包内置工具（9 个）

| 分组 | 工具 |
| --- | --- |
| 文件与磁盘 | Everything 文件搜索 |
| 硬盘工具 | WizTree、DiskGenius、MyDiskTest、魔方数据恢复、FinalData |
| 系统工具 | Geek Uninstaller、Dism++、DirectX 修复工具 |

未内置（体积原因）：Visual Studio Code、Dev-C++、FluxDown。需要的话把程序放进 `tools\` 对应目录，再在 `tools.ini` 里加一节即可。

## 添加自己的工具

1. 程序放到 `tools\<名称>\`（也可以写绝对路径）
2. 在 `tools.ini` 末尾追加：

```ini
[Notepad]
name=记事本
category=常用工具
icon=N
desc=系统自带文本编辑器
path=tools\Notepad\notepad.exe
args=
```

3. 按 `F5` 或点标题栏刷新按钮

## 说明

- 部分工具（DiskGenius、DirectX 修复、数据恢复类）的部分功能需要管理员权限，点击启动时可能弹 UAC，属正常现象
- 设置文件 `settings.ini` 会在首次切换主题时自动生成
- 本包为第三方工具的整合分发，各工具版权归其原作者所有
