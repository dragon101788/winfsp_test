# CLAUDE.md — WinFSP C 最小验证项目

## 目标
用 C 语言 + WinFSP 原生 API，将 `D:\test` 挂载为虚拟文件系统。
验证成功标准：在 `D:\test` 目录下能看到 `hello.txt`，双击能用记事本打开，内容为 `Hello from WinFSP!`。
所有文件数据全部在内存中，不访问真实磁盘。

---

## 前置条件（执行前人工确认）

1. 已安装 WinFSP：`C:\Program Files (x86)\WinFsp\` 目录存在
   - 若未安装：https://github.com/winfsp/winfsp/releases 下载 winfsp-x.x.xxxxx.msi
2. 已安装 - MSVC：Visual Studio 安装了 "C++ 桌面开发" 工作负载
3. `D:\test` 目录**不存在**（WinFSP 挂载点要求目录为空或不存在）
   - 如果存在且非空，换一个路径，同步修改下面 MOUNT_POINT

---

## 项目结构

```
winfsp_hello/
├── CLAUDE.md
├── build.bat          ← 编译脚本（自动检测 MSVC/MinGW）
└── hello_fs.c         ← 全部逻辑，单文件
```

---

## 实现任务

### 1. 创建 `hello_fs.c`

实现一个只读内存文件系统，包含：
- 根目录 `/`
- 一个文件 `/hello.txt`，内容为 `Hello from WinFSP!\r\n`

需要实现的 FSP_FILE_SYSTEM_INTERFACE 回调（**只实现这几个，其余填 NULL**）：

```
GetVolumeInfo      — 返回卷名 "HelloFS"，固定大小
GetSecurityByName  — 对根目录和 hello.txt 返回 FILE_ATTRIBUTE_DIRECTORY/NORMAL
Open               — 匹配路径，返回自定义 context（用简单整数区分 root/file）
Close              — 释放 context（本例无需操作）
Read               — 从内存 buffer 返回文件内容
ReadDirectory      — 根目录下枚举出 hello.txt 的 DirInfo
GetFileInfo        — 返回文件大小、属性、时间戳
```

关键代码结构示例：

```c
#include <winfsp/winfsp.h>
#include <string.h>

#define MOUNT_POINT L"D:\\test"

static const char *FILE_CONTENT = "Hello from WinFSP!\r\n";
static const WCHAR *FILE_NAME   = L"hello.txt";

// context 标记
#define CTX_ROOT 1
#define CTX_FILE 2

static NTSTATUS GetVolumeInfo(FSP_FILE_SYSTEM *fs,
    FSP_FSCTL_VOLUME_INFO *info)
{
    info->TotalSize    = 64 * 1024 * 1024;
    info->FreeSize     = 64 * 1024 * 1024;
    info->VolumeLabelLength = 14; // L"HelloFS" = 7 wchars * 2
    wcscpy_s(info->VolumeLabel, 32, L"HelloFS");
    return STATUS_SUCCESS;
}

static NTSTATUS GetSecurityByName(FSP_FILE_SYSTEM *fs,
    PWSTR FileName, PUINT32 PFileAttributes,
    PSECURITY_DESCRIPTOR *SecurityDescriptor, SIZE_T *SecurityDescriptorSize)
{
    if (0 == wcscmp(FileName, L"\\")) {
        *PFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    } else if (0 == wcscmp(FileName, L"\\hello.txt")) {
        *PFileAttributes = FILE_ATTRIBUTE_NORMAL;
    } else {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    return STATUS_SUCCESS;
}

// ... 其余回调参考 WinFSP memfs 示例实现
```

完整实现参考 WinFSP 官方 memfs：
`C:\Program Files (x86)\WinFsp\opt\fsext\memfs\`（如果已安装）
或 GitHub：https://github.com/winfsp/winfsp/blob/master/tst/memfs/memfs.cpp

**注意**：用 C 而非 C++，不使用 C++ 类，所有回调都是普通函数指针。

main 函数结构：

```c
int wmain(void) {
    FSP_FILE_SYSTEM *fs = NULL;
    FSP_FILE_SYSTEM_INTERFACE iface = { 0 };

    // 填入已实现的回调
    iface.GetVolumeInfo      = GetVolumeInfo;
    iface.GetSecurityByName  = GetSecurityByName;
    iface.Open               = Open;
    iface.Close              = Close;
    iface.Read               = Read;
    iface.ReadDirectory      = ReadDirectory;
    iface.GetFileInfo        = GetFileInfo;

    NTSTATUS st = FspFileSystemCreate(
        L"" FSP_FSCTL_DISK_DEVICE_NAME,   // DevicePath
        NULL,                              // VolumeParams (用默认)
        &iface,
        &fs);

    if (!NT_SUCCESS(st)) {
        wprintf(L"FspFileSystemCreate failed: %08lx\n", st);
        return 1;
    }

    st = FspFileSystemSetMountPoint(fs, MOUNT_POINT);
    if (!NT_SUCCESS(st)) {
        wprintf(L"Mount failed: %08lx\n", st);
        FspFileSystemDelete(fs);
        return 1;
    }

    st = FspFileSystemStartDispatcher(fs, 0);
    if (!NT_SUCCESS(st)) {
        wprintf(L"Dispatcher failed: %08lx\n", st);
        FspFileSystemDelete(fs);
        return 1;
    }

    wprintf(L"Mounted at %s — press Enter to unmount\n", MOUNT_POINT);
    getwchar();

    FspFileSystemStopDispatcher(fs);
    FspFileSystemDelete(fs);
    return 0;
}
```

### 2. 创建 `build.bat`

```bat
@echo off
setlocal

:: WinFSP 路径
set WINFSP_INC=C:\Program Files (x86)\WinFsp\inc
set WINFSP_LIB=C:\Program Files (x86)\WinFsp\lib

:: 优先尝试 MSVC
where cl >nul 2>&1
if %ERRORLEVEL%==0 (
    echo [MSVC] 编译中...
    cl /nologo /W3 /O2 ^
       /I"%WINFSP_INC%" ^
       hello_fs.c ^
       /link /LIBPATH:"%WINFSP_LIB%" winfsp-x64.lib ^
       /SUBSYSTEM:CONSOLE /OUT:hello_fs.exe
    goto :done
)

:: 否则尝试 MinGW gcc
where gcc >nul 2>&1
if %ERRORLEVEL%==0 (
    echo [MinGW] 编译中...
    gcc -O2 ^
        -I"%WINFSP_INC%" ^
        hello_fs.c ^
        -L"%WINFSP_LIB%" -lwinfsp-x64 ^
        -o hello_fs.exe ^
        -municode
    goto :done
)

echo [ERROR] 未找到 cl 或 gcc，请先配置编译器环境
exit /b 1

:done
if exist hello_fs.exe (
    echo [OK] 编译成功：hello_fs.exe
) else (
    echo [ERROR] 编译失败
    exit /b 1
)
```

---

## 执行步骤

```
1. cd winfsp_hello
2. build.bat
3. 以管理员身份运行：hello_fs.exe
4. 打开资源管理器，导航到 D:\test
5. 确认能看到 hello.txt
6. 双击 hello.txt，确认记事本显示 "Hello from WinFSP!"
7. 回到控制台，按 Enter 卸载
```

---

## 常见错误排查

| 错误 | 原因 | 解决 |
|------|------|------|
| `FspFileSystemCreate` 返回 `0xC000034` | WinFSP 驱动未运行 | 服务里启动 `WinFsp` 服务，或重装 WinFSP |
| Mount 失败 `0xC0000034` | 挂载点路径问题 | 确认 `D:\test` 不存在或为空目录 |
| 链接错误 `winfsp-x64.lib not found` | lib 路径错误 | 检查 WinFSP 安装路径，调整 build.bat |
| 盘符出现但目录为空 | `ReadDirectory` 未正确填充 DirInfo | 检查 `FspFileSystemAddDirInfo` 调用 |
| 记事本打开显示乱码 | `Read` 返回的 buffer offset 计算错误 | 注意 `Offset` 参数，从正确位置读取 |

---

## 成功后下一步

验证通过后，可以把 `Read` / `ReadDirectory` 的内存 buffer 替换为 WebDAV 远端请求，
就是完整的 WebDAV 虚拟驱动核心逻辑。
