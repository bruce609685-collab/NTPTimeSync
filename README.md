# NTPTimeSync　NTP 时间同步校准工具

NTPTimeSync 是一个在 Windows 电脑上校准系统时间的小工具，主要解决**时间偏差过大导致同步失败**的问题：系统时间错了几年，也能一次校准。

- 单个 exe，免安装，双击即用
- Windows 7 / 8 / 10 / 11 通用，32 位、64 位系统通用
- 除系统自带组件外，不需要任何运行库或外部依赖
默认语言为英语，可切换简体中文 / 繁體中文 / Français / Italiano / Русский / 日本語

![预览图](https://raw.githubusercontent.com/bruce609685-collab/NTPTimeSync/refs/heads/main/%E9%A2%84%E8%A7%88%E5%9B%BE.jpg)

## 使用

1. 双击 `NTPTimeSync.exe` 即可运行（免安装）。程序会弹出 UAC 提示，因为修改系统时间需要管理员权限。
2. 点 **一键自动同步**：并发检测全部服务器，选出延迟最低且时间可信的一台来校时。
3. 想指定服务器：在列表里选中一行，点 **同步所选服务器**（双击该行也可以）。
4. **检测全部服务器** 只测可用性和延迟，不改系统时间。点列表表头可排序。
5. **添加… / 删除 / 恢复内置列表** 用来管理服务器。地址可写域名或 IP，需要时写成 `域名:端口`。

## 运行时生成的文件

运行 `NTPTimeSync.exe` 后，会在程序所在目录自动生成配置文件和运行日志：

| 文件 | 说明 |
| --- | --- |
| `NTPSync.ini` | 配置和服务器列表，可用记事本编辑，请以 UTF-8 保存 |
| `NTPSync.log` | 运行日志，超过 256 KB 自动轮换为 `.old` |

**卸载**：取消勾选"开机自动启动"（或在计划任务里删除 `NTPTimeSyncTool`），再删除 exe 和上面两个文件即可。程序不写注册表。

## 注意

- 开机自启依赖 exe 的路径。移动 exe 后再打开一次程序，会自动把启动任务改到新路径。
- 需要放行出站 UDP 123 端口；公司网络若封了 123，会显示"无响应"。
- 内置的 24 台服务器里，个别（如部分高校、国家授时中心域名）在某些网络下会无响应，属正常现象，一键同步会自动跳过。

## 目录结构

```
NTPTimeSync-2.0/
│
├── README.md                      6,296 B   使用说明 v2.0（构建/功能/语言包）
├── LICENSE                       11,357 B   Apache-2.0 许可证全文
│
├── src/                                    ★ 源代码（20 个文件）
│   ├── main.cpp                  49,391 B   主程序：窗口、按钮、托盘、列表、定时同步、语言切换
│   ├── ntp.h                      2,139 B   NTP 协议层接口
│   ├── ntp.cpp                   12,911 B   协议实现：时间戳换算、收发校验、测延迟、设置系统时间
│   ├── engine.h                   1,919 B   同步引擎接口
│   ├── engine.cpp                 6,513 B   引擎：并发探测、多数派择优、大偏差校正+复核、备选切换
│   ├── i18n.h                     2,316 B   多语言接口
│   ├── i18n.cpp                  20,726 B   多语言：186 个英文默认键、语言包从 exe 资源加载、模板导出
│   ├── config.h                   1,161 B   配置与服务器列表接口
│   ├── config.cpp                 9,171 B   配置读写、36 台内置服务器、地址校验
│   ├── autostart.h                  501 B   开机自启接口
│   ├── autostart.cpp              5,438 B   计划任务创建/删除/查询
│   ├── util.h                       646 B   通用工具接口（字符串、路径、日志）
│   ├── util.cpp                   3,525 B   通用工具实现
│   ├── variant.h                    823 B   公网版/内网版编译期开关
│   ├── selftest.cpp              14,100 B   自检（假 NTP 服务器 + 虚拟时钟，test.bat 用，不进主程序）
│   └── resource/                            资源（5 个）
│       ├── app.rc                 2,203 B   资源脚本：图标、版本、语言包 RCDATA、添加服务器对话框
│       ├── resource.h               554 B   资源编号、版本号 2.0.0、语言包资源 ID
│       ├── app.manifest            1,401 B  发布版清单（要求管理员、视觉样式、Win7~11、DPI）
│       ├── app_dev.manifest          950 B  测试版清单（asInvoker，供 build.bat dev）
│       └── app.ico                40,849 B  程序图标
│
├── lang/                                     语言包源文件（编译进 exe）
│   ├── zh-CN.ini                  8,137 B   简体中文（186 键）
│   ├── zh-TW.ini                  8,144 B   繁體中文（186 键）
│   ├── fr-FR.ini                  8,708 B   法语（186 键）
│   ├── it-IT.ini                  8,614 B   意大利语（186 键）
│   ├── ru-RU.ini                 12,297 B   俄语（186 键）
│   ├── ja-JP.ini                  9,741 B   日语（186 键）
│   └── README.md                  4,385 B   语言包格式 + 新增语言三步注册说明
│
└── build/                                    构建脚本（3 个）
    ├── build.bat                  2,355 B   编译（参数：intranet / dev）
    ├── test.bat                     769 B   编译并运行自检
    └── make_icon.py               4,262 B   生成 app.ico（纯标准库，无第三方依赖）
```

## 从源码构建

需要 MinGW-w64 32 位工具链（`C:\msys64\mingw32`）和 Python（仅用于生成图标）。

```
build\build.bat              ->  dist\NTPTimeSync.exe（公网版，要求管理员权限）
build\build.bat intranet     ->  dist\NTPTimeSync_Intranet.exe（内网版，内置 6 台内网服务器）
build\build.bat dev          ->  build\dev\NTPTimeSync_dev.exe（不要求管理员，仅供测试）
build\build.bat intranet dev ->  build\dev\NTPTimeSync_Intranet_dev.exe
build\test.bat               ->  编译并运行自检（本机回环上的假 NTP 服务器 + 虚拟时钟，不动真实系统时间）
```

`build` 脚本必须保持纯 ASCII + CRLF 换行。

## 许可证

本项目采用 **Apache-2.0** 许可证，详见 [LICENSE](LICENSE)。

Copyright (C) 2026 bruce609685-collab
