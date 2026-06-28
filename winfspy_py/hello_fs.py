"""
最小 WinFSP 内存文件系统 — Python/winfspy 版本
挂载 D:\test，提供一个只读 hello.txt
"""

import stat
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

        entries = []

        # hello.txt
        size = len(FILE_CONTENT)
        entries.append({
            "file_name": "hello.txt",
            "file_attributes": FILE_ATTRIBUTE.FILE_ATTRIBUTE_NORMAL,
            "allocation_size": (size + 4095) & ~4095,
            "file_size": size,
            "creation_time": CREATION_TIME,
            "last_access_time": CREATION_TIME,
            "last_write_time": CREATION_TIME,
            "change_time": CREATION_TIME,
            "index_number": 2,
        })

        # 处理 marker
        if marker is not None:
            entries = [e for e in entries if e["file_name"] > marker]

        return entries

    # 只读文件系统：拒绝写操作
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
