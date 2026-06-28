# CLAUDE.md — Python + winfspy 最小验证项目

## 目标
用 Python + winfspy 库，将 `D:\test` 挂载为虚拟文件系统。
验证成功标准：在 `D:\test` 目录下能看到 `hello.txt`，双击能用记事本打开，内容为 `Hello from winfspy!`。
所有文件数据全部在内存中，不访问真实磁盘。

---

## 前置条件（执行前人工确认）

1. 已安装 WinFSP：`C:\Program Files (x86)\WinFsp\` 目录存在
   - 若未安装：https://github.com/winfsp/winfsp/releases 下载安装
2. Python 3.8+（`python --version` 能输出版本）
3. `D:\test` 目录**不存在**或为空目录
   - 如果路径冲突，修改下面 MOUNT_POINT

---

## 项目结构

```
winfspy_hello/
├── CLAUDE.md
├── requirements.txt
├── setup.bat          ← 一键安装依赖
├── run_admin.bat      ← 右键→管理员运行
└── hello_fs.py        ← 全部逻辑，单文件
```

---

## 实现任务

### 1. 创建 `requirements.txt`

```
winfspy
```

### 2. 创建 `setup.bat`

```bat
@echo off
pip install -r requirements.txt
echo [OK] 依赖安装完成
```

### 3. 创建 `hello_fs.py`

完整实现如下（直接可运行）：

```python
"""
最小 WinFSP 内存文件系统 — Python/winfspy 版本
挂载 D:\test，提供一个只读 hello.txt

已验证：winfspy 0.8.4 + Python 3.12 + WinFSP 2.1
"""

import threading
from datetime import datetime, timezone

from winfspy import (
    FileSystem,
    BaseFileSystemOperations,
    NTStatusObjectNameNotFound,
    NTStatusEndOfFile,
    NTStatusAccessDenied,
)
from winfspy.plumbing import FILE_ATTRIBUTE

MOUNT_POINT = "D:\\test"
FILE_CONTENT = b"Hello from winfspy!\r\n"


def now_ft():
    """返回当前时间的 Windows FILETIME（100ns 单位，从1601年起）"""
    epoch = datetime(1601, 1, 1, tzinfo=timezone.utc)
    delta = datetime.now(timezone.utc) - epoch
    return int(delta.total_seconds() * 10_000_000)


CREATION_TIME = now_ft()


class HelloFileSystemOperations(BaseFileSystemOperations):

    def get_volume_info(self):
        return {
            "total_size": 64 * 1024 * 1024,
            "free_size":  64 * 1024 * 1024,
            "volume_label": "HelloFS",
        }

    def get_security_by_name(self, file_name):
        if file_name == "\\":
            return FILE_ATTRIBUTE.FILE_ATTRIBUTE_DIRECTORY, b"", 0
        elif file_name == "\\hello.txt":
            return FILE_ATTRIBUTE.FILE_ATTRIBUTE_NORMAL, b"", 0
        else:
            raise NTStatusObjectNameNotFound()

    def get_security(self, file_context):
        return b"", 0

    def open(self, file_name, create_options, granted_access):
        if file_name == "\\":
            return {"type": "dir"}
        elif file_name == "\\hello.txt":
            return {"type": "file"}
        else:
            raise NTStatusObjectNameNotFound()

    def close(self, file_context):
        pass

    def cleanup(self, file_context, file_name, flags):
        pass

    def get_file_info(self, file_context):
        if file_context["type"] == "dir":
            return {
                "file_attributes": FILE_ATTRIBUTE.FILE_ATTRIBUTE_DIRECTORY,
                "allocation_size": 0,
                "file_size": 0,
                "creation_time": CREATION_TIME,
                "last_access_time": CREATION_TIME,
                "last_write_time": CREATION_TIME,
                "change_time": CREATION_TIME,
                "index_number": 1,
            }
        else:
            size = len(FILE_CONTENT)
            return {
                "file_attributes": FILE_ATTRIBUTE.FILE_ATTRIBUTE_NORMAL,
                "allocation_size": (size + 4095) & ~4095,
                "file_size": size,
                "creation_time": CREATION_TIME,
                "last_access_time": CREATION_TIME,
                "last_write_time": CREATION_TIME,
                "change_time": CREATION_TIME,
                "index_number": 2,
            }

    def read(self, file_context, offset, length):
        if file_context["type"] != "file":
            raise NTStatusAccessDenied()
        data = FILE_CONTENT[offset: offset + length]
        if not data:
            raise NTStatusEndOfFile()
        return data

    def read_directory(self, file_context, marker):
        """枚举根目录内容"""
        if file_context["type"] != "dir":
            return []

        size = len(FILE_CONTENT)
        entries = [
            {
                "file_name": "hello.txt",
                "file_attributes": FILE_ATTRIBUTE.FILE_ATTRIBUTE_NORMAL,
                "allocation_size": (size + 4095) & ~4095,
                "file_size": size,
                "creation_time": CREATION_TIME,
                "last_access_time": CREATION_TIME,
                "last_write_time": CREATION_TIME,
                "change_time": CREATION_TIME,
                "index_number": 2,
            }
        ]

        if marker is not None:
            entries = [e for e in entries if e["file_name"] > marker]

        return entries

    # 只读文件系统：拒绝所有写操作
    def create(self, *args, **kwargs):
        raise NTStatusAccessDenied()

    def write(self, *args, **kwargs):
        raise NTStatusAccessDenied()

    def set_file_size(self, *args, **kwargs):
        raise NTStatusAccessDenied()

    def delete(self, *args, **kwargs):
        raise NTStatusAccessDenied()

    def overwrite(self, *args, **kwargs):
        raise NTStatusAccessDenied()

    def rename(self, *args, **kwargs):
        raise NTStatusAccessDenied()

    def set_security(self, *args, **kwargs):
        raise NTStatusAccessDenied()

    def get_reparse_point(self, *args, **kwargs):
        raise NTStatusObjectNameNotFound()

    def get_stream_info(self, *args, **kwargs):
        return []


def main():
    print(f"[*] 正在挂载到 {MOUNT_POINT} ...")

    operations = HelloFileSystemOperations()

    fs = FileSystem(
        MOUNT_POINT,
        operations,
        sector_size=512,
        sectors_per_allocation_unit=1,
        volume_creation_time=CREATION_TIME,
        volume_serial_number=0xDEADBEEF,
        file_info_timeout=1000,
        case_sensitive_search=False,
        case_preserved_names=True,
        unicode_on_disk=True,
        persistent_acls=False,
        um_file_context_is_user_context2=True,
        debug=False,
    )

    try:
        fs.start()
        print(f"[OK] 已挂载：{MOUNT_POINT}")
        print("[*] 用资源管理器打开 D:\\test，双击 hello.txt 验证")
        print("[*] 按 Ctrl+C 卸载...")
        threading.Event().wait()  # 永久等待
    except KeyboardInterrupt:
        print("\n[*] 正在卸载...")
    finally:
        fs.stop()
        print("[OK] 已卸载")


if __name__ == "__main__":
    main()
```

---

## 执行步骤

```
1. cd winfspy_hello
2. setup.bat              （安装 winfspy）
3. 右键 run_admin.bat → 以管理员身份运行
4. 打开资源管理器，导航到 D:\test
5. 确认能看到 hello.txt
6. 双击 hello.txt，确认记事本显示 "Hello from winfspy!"
7. Ctrl+C 卸载
```

---

## 常见错误排查

| 错误 | 原因 | 解决 |
|------|------|------|
| `ImportError: cannot import name 'FileSystem'` | winfspy 版本不匹配 | `pip install --upgrade winfspy` |
| `ModuleNotFoundError: 'winfspy.plumbing.winstuff'` | winfspy 0.8.x 中无此模块 | 改为 `from winfspy.plumbing import FILE_ATTRIBUTE` |
| `TypeError: '>' not supported between 'NoneType' and 'int'` | `get_security_by_name` 返回了 `None` 作为 sd_size | 第三个返回值改为 `0` |
| `TypeError: a bytes-like object is required` | `get_security_by_name` 返回了 `None` 作为 sd | 第二个返回值改为 `b""`（空 bytes） |
| `NotImplementedError` in `cleanup` | 未实现 `cleanup` 方法 | 添加 `def cleanup(self, ...): pass` |
| `NotImplementedError` in `get_security` | 未实现 `get_security` 方法 | 添加 `def get_security(self, ...): return b"", 0` |
| `TypeError: tuple indices must be integers` in `read_directory` | 返回了元组列表 `[(name, dict)]` | 返回 dict 列表 `[{"file_name": name, ...}]` |
| `OSError: [WinError ...]` 挂载失败 | WinFSP 驱动未运行 | 以管理员身份运行；检查 WinFsp 服务是否启动 |
| 双击报错"无法访问" | `get_file_info` 的 file_size 为 0 | 检查 `len(FILE_CONTENT)` 是否正确填入 |
| 文件夹显示快捷方式小箭头 | 正常现象 | WinFSP 使用重解析点实现，不影响功能 |

---

## winfspy API 版本差异（实际踩坑记录）

当前环境：**winfspy 0.8.4**（`pip install winfspy` 默认版本）

以下差异已体现在上方 `hello_fs.py` 代码中，新项目直接复制即可。

### 1. FILE_ATTRIBUTE 导入路径

| 来源 | 代码 |
|------|------|
| 旧文档（错误） | `from winfspy.plumbing.winstuff import FILE_ATTRIBUTE_NORMAL, FILE_ATTRIBUTE_DIRECTORY` |
| **0.8.4 实际** | `from winfspy.plumbing import FILE_ATTRIBUTE` |

`FILE_ATTRIBUTE` 是 IntEnum，使用时需 `FILE_ATTRIBUTE.FILE_ATTRIBUTE_NORMAL`（值为 128）和 `FILE_ATTRIBUTE.FILE_ATTRIBUTE_DIRECTORY`（值为 16）。

### 2. get_security_by_name 返回值

| 来源 | 代码 |
|------|------|
| 旧文档（错误） | `return FILE_ATTRIBUTE_DIRECTORY, None, None` |
| **0.8.4 实际** | `return FILE_ATTRIBUTE.FILE_ATTRIBUTE_DIRECTORY, b"", 0` |

第三个值必须是 `int`（不能是 `None`），否则 winfspy 内部 `None > int` 比较报错。
第二个值必须是 `bytes`（不能是 `None`），否则 `ffi.memmove` 报错。

### 3. 必须实现的额外方法

0.8.4 的 `BaseFileSystemOperations` 有更多抽象方法，必须全部实现（即使是空操作）：

```python
def cleanup(self, file_context, file_name, flags): pass
def get_security(self, file_context): return b"", 0
def overwrite(self, *args, **kwargs): raise NTStatusAccessDenied()
def rename(self, *args, **kwargs): raise NTStatusAccessDenied()
def set_security(self, *args, **kwargs): raise NTStatusAccessDenied()
def get_reparse_point(self, *args, **kwargs): raise NTStatusObjectNameNotFound()
def get_stream_info(self, *args, **kwargs): return []
```

### 4. read_directory 返回格式

| 来源 | 格式 |
|------|------|
| 旧文档（错误） | `[("name", {"file_attributes": ...}), ...]` — 元组列表 |
| **0.8.4 实际** | `[{"file_name": "name", "file_attributes": ...}, ...]` — dict 列表 |

`file_name` 必须作为 dict 的键之一，与文件属性平级。
根目录不应包含 `.` 和 `..` 条目。

---

## 成功后下一步

验证通过后：
- `read()` 方法替换为 `httpx.get(webdav_url).content`
- `read_directory()` 替换为 WebDAV PROPFIND 解析结果
- 加上异步支持（`asyncio` + `httpx.AsyncClient`）即为完整 WebDAV 客户端
