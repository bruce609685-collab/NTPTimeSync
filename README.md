# NTPTimeSync　NTP 时间同步校准工具

NTPTimeSync 是一个在 Windows 电脑上校准系统时间的小工具，主要解决**时间偏差过大导致同步失败**的问题：系统时间错了几年，也能一次校准。

- 单个 exe，免安装，双击即用
- Windows 7 / 8 / 10 / 11 通用，32 位、64 位系统通用
- 除系统自带组件外，不需要任何运行库或外部依赖

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
NTPTimeSync待发布/
├── NTPTimeSync.exe            编译好的程序
├── README.md                  本文件
├── LICENSE                    Apache-2.0 许可证全文
├── build/                     构建脚本
│   ├── build.bat              编译脚本
│   ├── test.bat               自检脚本
│   └── make_icon.py           生成程序图标
└── src/                       源代码
    ├── main.cpp               主程序：窗口界面、托盘图标、按钮响应、定时与启动同步流程
    ├── ntp.h                  NTP 协议层接口
    ├── ntp.cpp                NTP 协议：时间戳换算、收发并校验应答、测延迟、设置系统时间
    ├── engine.h               同步引擎接口
    ├── engine.cpp             同步引擎：并发检测、择优、大偏差校正与复核、备选切换
    ├── config.h               配置与服务器列表接口
    ├── config.cpp             配置文件读写、24 台内置服务器、服务器地址校验
    ├── autostart.h            开机自启接口
    ├── autostart.cpp          开机自启：创建和删除登录时运行的计划任务
    ├── util.h                 通用工具接口
    ├── util.cpp               通用工具：字符串转换、程序路径、日志
    ├── selftest.cpp           自检程序：用本机回环上的假 NTP 服务器测试（仅 test.bat 使用）
    └── resource/              资源文件
        ├── app.rc             资源脚本：图标、版本信息、"添加服务器"对话框
        ├── resource.h         资源编号与版本号
        ├── app.ico            程序图标
        ├── app.manifest       发布版清单：要求管理员权限、启用视觉样式、声明系统兼容性
        └── app_dev.manifest   测试版清单：不要求管理员权限（仅 build.bat dev 使用）
```

## 从源码构建

需要 MinGW-w64 32 位工具链（`C:\msys64\mingw32`）和 Python（仅用于生成图标）。

```
build\build.bat        ->  dist\NTPTimeSync.exe（要求管理员权限）
build\build.bat dev    ->  build\dev\NTPTimeSync_dev.exe（不要求管理员，仅供测试）
build\test.bat         ->  编译并运行自检（本机回环上的假 NTP 服务器 + 虚拟时钟，不动真实系统时间）
```

`build` 脚本必须保持纯 ASCII + CRLF 换行。

## 许可证

本项目采用 **Apache-2.0** 许可证，详见 [LICENSE](LICENSE)。

Copyright (C) 2026 bruce609685-collab
