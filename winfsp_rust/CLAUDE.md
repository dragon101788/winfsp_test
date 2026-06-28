# CLAUDE.md — WinFSP Rust 最小验证项目

## 目标
用 Rust + winfsp crate，将 `D:\test` 挂载为虚拟文件系统。
验证成功标准：在 `D:\test` 目录下能看到 `hello.txt`，双击能用记事本打开，内容为 `Hello from WinFSP!`。
所有文件数据全部在内存中，不访问真实磁盘。

---

## 前置条件（执行前人工确认）

1. 已安装 WinFSP：`C:\Program Files (x86)\WinFsp\` 目录存在
   - 若未安装：https://github.com/winfsp/winfsp/releases 下载安装
2. Rust 工具链（`rustc` + `cargo`）
3. `D:\test` 目录**不存在**或为空目录

---

## 项目结构

```
winfsp_rust/
├── CLAUDE.md
├── Cargo.toml          ← 依赖声明 (winfsp = "0.13")
├── build.rs            ← delay-load 链接配置
├── build.bat           ← 一键编译（cargo build --release）
├── run_admin.bat       ← 右键→管理员运行
└── src/
    └── main.rs         ← 全部逻辑，单文件
```

---

## 编译与执行

```
1. cd winfsp_rust
2. cargo build          （debug 版本）
   或 build.bat         （release 版本）
3. 以管理员身份运行：target\debug\hello_fs.exe
   或右键 run_admin.bat → 以管理员身份运行
4. 打开资源管理器，导航到 D:\test
5. 确认能看到 hello.txt
6. 双击 hello.txt，确认记事本显示 "Hello from WinFSP!"
7. 回到控制台，按 Enter 卸载
```

---

## 实现说明

### 依赖
- `winfsp = "0.13"` — WinFSP 安全 Rust 绑定
- `winfsp-sys = "0.12"` — WinFSP 原始 FFI 绑定（类型定义）

### 核心 API
实现 `FileSystemContext` trait：
- `get_volume_info` — 返回卷名 "HelloFS"、总大小 64MB
- `get_security_by_name` — 按路径查找文件属性
- `open` / `close` — 打开/关闭文件
- `get_file_info` — 返回文件大小、属性
- `read` — 从内存 buffer 读取文件内容
- `read_directory` — 枚举根目录（hello.txt）
- 所有写操作 → `STATUS_ACCESS_DENIED`

### 对齐 C/Python 版本
- 相同挂载点：`D:\test`
- 相同卷序列号：`0xDEADBEEF`
- 相同扇区配置：sector_size=512, sectors_per_allocation_unit=1
- 相同文件内容：`Hello from WinFSP!\r\n`
- 只读文件系统

### 并发策略
使用默认 `FineGuard` 策略，文件系统回调可跨多线程并发执行。
`HelloFS`（无状态）和 `Ctx`（Copy enum）均满足 `Sync`。

### build.rs
WinFSP 仅支持 delay-load 方式加载 DLL。build.rs 调用
`winfsp::build::winfsp_link_delayload()` 生成正确的链接器标志。
运行时 `winfsp_init_or_die()` 负责预加载 DLL。

---

## 常见错误排查

| 错误 | 原因 | 解决 |
|------|------|------|
| `FspFileSystemCreate FAILED` | WinFSP 驱动未运行 | 启动 WinFsp 服务或重装 |
| Mount 失败 | 挂载点路径问题 | 确认 `D:\test` 不存在或为空 |
| 编译错误 `cannot find module winfsp` | build.rs 中缺少 build-dependencies | Cargo.toml 中需同时声明 `[dependencies]` 和 `[build-dependencies]` |
| `host.start()` 歧义错误 | HelloFS 同时满足 Send + Sync | 使用 UFCS: `FileSystemHost::<HelloFS>::start(&mut host)` |

---

## 参考
- [winfsp-rs GitHub](https://github.com/SnowflakePowered/winfsp-rs)
- C 版本：`../winfsp_c/hello_fs.c`
- Python 版本：`../winfspy_py/hello_fs.py`
