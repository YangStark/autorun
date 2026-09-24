# 白色相簿 2 存档失败：My Documents 路径解析缺项

日期：2026-09-24。基线：`9b71705`。独立分支：`bugfix/my-documents-save-path`。

## 症状与已验证原因

从 Autorun 列表和 HOME 转发器启动后，点击存档会停住画面，音乐继续播放。失败日志显示游戏弹出了“无法打开文件”的模态对话框，目标为：

```text
::{450d8fba-ad25-11d0-98a8-0800361b1103}\Leaf\WHITE ALBUM2\save_00.sav
```

游戏调用 `SHGetSpecialFolderLocation` / `SHGetPathFromIDListA`。`shellpath.c` 的 `SHGetFolderLocation(CSIDL_PERSONAL)` 创建 My Documents 的虚拟 PIDL；`shfldr_desktop.c` 的路径解析检查以下注册表值是否存在：

```text
HKLM\Software\Classes\CLSID\{450d8fba-ad25-11d0-98a8-0800361b1103}\ShellFolder
WantsForParsing = "" (REG_SZ)
```

缺少该值时，解析返回 `::{GUID}`。Wine 的 `set_folder_attributes()` 正常会写入该值，但本次设备的有效注册表中没有它。

仅补充此值后，保留相同官方 NRO、shell32.dll 和游戏 EXE，用户确认可以正常存档。回读结果：

- 最新日志不再出现 GUID 存档路径错误或相应 MessageBox 错误。
- `C:\users\steamuser\Documents\Leaf\WHITE ALBUM2` 出现 `save_00.sav`、`save_01.sav`、`save_Q.sav`，各 348112 字节，以及 2528384 字节的 `Sys.sav`。
- 注册表回读确认该项已保留。

这证明缺失注册表项是当前环境中存档故障的直接原因，不代表所有 9b71705 环境都缺少它。

## 诊断过程及纠正

1. 旧 test-build-3 包的 NRO 与用户可用的 build 218 备份一致；其 shell32.dll 与当前设备 DLL 一致，DLL 字节不匹配的猜测被排除。
2. 最初怀疑 `5e1f183` 统一用户目录为 steamuser 后触发不同路径；没有完成受控对照，因此不能把该提交认定为唯一原因。修复无需撤回它。
3. 最初修改了根目录 `switch/wine/user.reg`，随后恢复；后来第一次补充 WantsForParsing 也误改了根目录 `system.reg`。这些测试没有修改新版运行时实际读取的 hive，不能作为否定路径或注册表修复的证据。
4. `50f949b` 已把 hive 迁到 `switch/wine/registry/`。代码在新目录已有 hive 时不会覆盖它。找到真正的 `registry/system.reg` 并补项后，存档恢复。
5. 曾编译直接修改 shell32 PIDL 转换的实验 DLL，未安装。该方案可能截断子 PIDL 路径，已放弃。撤回 5e1f183 的诊断 NRO 也未安装。

## 本分支的长期修复

在 `horizon_registry_init()` 的 machine defaults 中增加该 REG_SZ 默认值，先于 `config/classes.reg` 和持久化 hive 加载。因此 NRO 自身可以补齐缺项，已有注册表值仍优先；无需发布或覆盖用户完整 system.reg，也不硬编码用户的存档目录。

不修改用户目录选择，不迁移旧存档，不包含鼠标、转发器或启动计时功能。

## 验证范围

- 实机：官方 9b71705 NRO 加等效单项注册表修复，通过新建存档验证。
- 主机：扩展现有 `registry_server.c` 测试，未修复基线在缺少 Documents key 的断言失败；修复后通过。覆盖默认空字符串、持久化已有值优先、已有 hive 缺少该项时重新补齐，以及既有注册表保存/恢复测试。
- Clang 使用 `-Wall -Wextra -Werror`、ASan 和 UBSan；`ASAN_OPTIONS=detect_leaks=0`（此轮未做泄漏检查）。
- 后续独立 NRO 实机验证已完成，见下文；与此前手工注册表测试分别记录。

## 设备现状与恢复

实际生效文件是 `sdmc:/switch/wine/registry/system.reg`；修复前的 hive 已在电脑备份，并在设备同目录保存为 `system-before-wantsparsing-20260924.reg`。根目录误改的 system.reg 已恢复。恢复前须退出游戏和 Autorun，保留当前 hive，再还原该备份。

旧存档仍在 `C:\Leaf\WHITE ALBUM2` 且有备份，没有自动合并到新位置，以避免覆盖成功测试产生的新档位。仓库不包含设备完整注册表、游戏二进制或用户存档。


## b843738 NRO 实机存读档验证

用户确认新 NRO 的存档和读取均成功。测试前备份可用环境，仅从有效 `registry/system.reg` 移除手工 `WantsForParsing` 值，回读确认该值不存在，然后安装本分支编译的 NRO。

- 测试提交：`b84373857658883ecb6fbb7957a4cb863ff57e3f`。
- NRO SHA256：`7EB8D02A290C9B86501F606898B50E932F4E330CC16C28869BD23DE274B1003F`；测试后设备回读一致。
- 最新日志没有 GUID 存档路径错误、MessageBox 错误或未处理异常；正常退出，最终记录为 0 个线程、0 个自身遗留的借用区域。
- 有效注册表中重新出现 `WantsForParsing=""`，证明本次补项来自新运行时。
- Documents 下的 `save_Q.sav` 相比上一轮测试哈希改变；普通存档仍存在。游戏内存档/读档成功由用户实测确认，不把文件存在单独当成读档成功的证据。
- 日志仍有 `commdlg` activation context 错误 14001，此轮未阻止存读档；其原因没有在本次修复中调查。

此结论覆盖当前设备、现有游戏 EXE 与本次存读档流程，不等同于所有游戏或所有升级路径的完整验证。
