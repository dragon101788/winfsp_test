/**
 * hello_fs.rs — WinFSP 最小验证项目 (Rust 版本)
 *
 * 挂载 D:\test 为虚拟只读文件系统。
 * 包含一个文件 \hello.txt，内容为 "Hello from WinFSP!\r\n"
 * 所有数据在内存中，不访问真实磁盘。
 *
 * 日志同时输出到控制台和 hello_fs.log 文件（对齐 C 版本）。
 *
 * 参考：C 版本 (winfsp_c/hello_fs.c) 和 Python 版本 (winfspy_py/hello_fs.py)
 */

use std::ffi::c_void;
use std::fmt::Arguments;
use std::fs::File;
use std::io::{BufWriter, Write};
use std::sync::Mutex;

use winfsp::filesystem::{
    DirInfo, DirMarker, FileInfo, FileSecurity, FileSystemContext,
    ModificationDescriptor, OpenFileInfo, VolumeInfo, WideNameInfo,
};
use winfsp::host::{FileSystemHost, VolumeParams};
use winfsp::{FspError, Result, U16CStr};
use winfsp_sys::{FILE_ACCESS_RIGHTS, FILE_FLAGS_AND_ATTRIBUTES};

// ══════════════════════════════════════════════════════════════════════
// 日志系统（文件 + 控制台，对齐 C 版本的 log_write）
// ══════════════════════════════════════════════════════════════════════

static LOGGER: Mutex<Option<BufWriter<File>>> = Mutex::new(None);

fn log_open() {
    match File::create("hello_fs.log") {
        Ok(f) => {
            let mut writer = BufWriter::new(f);
            let _ = writeln!(writer, "=== hello_fs log start ===");
            let _ = writer.flush();
            *LOGGER.lock().unwrap() = Some(writer);
        }
        Err(e) => {
            // 如果当前目录不可写，尝试写到临时目录
            let path = std::env::temp_dir().join("hello_fs.log");
            if let Ok(f) = File::create(&path) {
                let mut writer = BufWriter::new(f);
                let _ = writeln!(writer, "=== hello_fs log start (temp) ===");
                let _ = writer.flush();
                *LOGGER.lock().unwrap() = Some(writer);
            } else {
                eprintln!("log_open FAILED: {}", e);
            }
        }
    }
}

fn log_close() {
    if let Ok(mut guard) = LOGGER.lock() {
        if let Some(ref mut writer) = *guard {
            let _ = writeln!(writer, "=== log end ===");
            let _ = writer.flush();
        }
        *guard = None;
    }
}

/// 写日志 —— 同时输出到控制台和文件，立即 flush
fn log(args: Arguments) {
    // 控制台
    print!("{}", args);
    let _ = std::io::stdout().flush();

    // 文件
    if let Ok(mut guard) = LOGGER.lock() {
        if let Some(ref mut writer) = *guard {
            let _ = writer.write_fmt(args);
            let _ = writer.flush();
        }
    }
}

macro_rules! logf {
    ($($arg:tt)*) => { log(format_args!($($arg)*)) };
}

// ══════════════════════════════════════════════════════════════════════
// 常量
// ══════════════════════════════════════════════════════════════════════

fn err(status: i32) -> FspError {
    FspError::NTSTATUS(status)
}

const STATUS_ACCESS_DENIED: i32         = 0xC0000022_u32 as i32;
const STATUS_OBJECT_NAME_NOT_FOUND: i32 = 0xC0000034_u32 as i32;
const STATUS_NOT_A_REPARSE_POINT: i32   = 0xC0000275_u32 as i32;

const FILE_ATTRIBUTE_DIRECTORY: u32 = 0x00000010;
const FILE_ATTRIBUTE_NORMAL: u32    = 0x00000080;

const MOUNT_POINT: &str   = "D:\\test";
const VOLUME_LABEL: &str  = "HelloFS";
const FILE_NAME: &str     = "hello.txt";
const FILE_CONTENT: &[u8] = b"Hello from WinFSP!\r\n";

fn alloc_size(size: u64) -> u64 {
    (size + 4095) & !4095
}

fn is_hello_txt(name: &U16CStr) -> bool {
    name.to_os_string()
        .to_string_lossy()
        .eq_ignore_ascii_case("\\hello.txt")
}

// ══════════════════════════════════════════════════════════════════════
// 文件系统
// ══════════════════════════════════════════════════════════════════════

#[derive(Debug, Clone, Copy)]
enum Ctx {
    Root,
    File,
}

struct HelloFS;

impl FileSystemContext for HelloFS {
    type FileContext = Ctx;

    fn get_volume_info(&self, volume_info: &mut VolumeInfo) -> Result<()> {
        logf!("[GetVolumeInfo]\n");
        volume_info.total_size = 64 * 1024 * 1024;
        volume_info.free_size  = 64 * 1024 * 1024;
        volume_info.set_volume_label(VOLUME_LABEL);
        Ok(())
    }

    fn get_security_by_name(
        &self,
        file_name: &U16CStr,
        _security_descriptor: Option<&mut [c_void]>,
        _resolve: impl FnOnce(&U16CStr) -> Option<FileSecurity>,
    ) -> Result<FileSecurity> {
        let os = file_name.to_os_string();
        logf!("[GetSecurityByName] {:?}\n", os);

        let attributes = if file_name.as_slice() == [b'\\' as u16] {
            FILE_ATTRIBUTE_DIRECTORY
        } else if is_hello_txt(file_name) {
            FILE_ATTRIBUTE_NORMAL
        } else {
            logf!("  -> NOT FOUND\n");
            return Err(err(STATUS_OBJECT_NAME_NOT_FOUND));
        };

        logf!("  -> OK\n");
        Ok(FileSecurity {
            reparse: false,
            sz_security_descriptor: 0,
            attributes,
        })
    }

    fn open(
        &self,
        file_name: &U16CStr,
        _create_options: u32,
        _granted_access: FILE_ACCESS_RIGHTS,
        file_info: &mut OpenFileInfo,
    ) -> Result<Self::FileContext> {
        logf!("[Open] {:?}\n", file_name.to_os_string());

        if file_name.as_slice() == [b'\\' as u16] {
            let fi: &mut FileInfo = file_info.as_mut();
            fi.file_attributes = FILE_ATTRIBUTE_DIRECTORY;
            logf!("  -> CTX_ROOT\n");
            return Ok(Ctx::Root);
        }

        if is_hello_txt(file_name) {
            let fi: &mut FileInfo = file_info.as_mut();
            fi.file_attributes = FILE_ATTRIBUTE_NORMAL;
            fi.file_size       = FILE_CONTENT.len() as u64;
            fi.allocation_size = alloc_size(FILE_CONTENT.len() as u64);
            logf!("  -> CTX_FILE\n");
            return Ok(Ctx::File);
        }

        logf!("  -> NOT FOUND\n");
        Err(err(STATUS_OBJECT_NAME_NOT_FOUND))
    }

    fn close(&self, context: Self::FileContext) {
        logf!("[Close] {:?}\n", context);
    }

    fn cleanup(&self, _context: &Self::FileContext, file_name: Option<&U16CStr>, _flags: u32) {
        logf!("[Cleanup] {:?}\n", file_name.map(|n| n.to_os_string()));
    }

    fn get_file_info(&self, context: &Self::FileContext, file_info: &mut FileInfo) -> Result<()> {
        logf!("[GetFileInfo] {:?}\n", context);
        match context {
            Ctx::Root => {
                file_info.file_attributes = FILE_ATTRIBUTE_DIRECTORY;
                file_info.allocation_size = 0;
                file_info.file_size       = 0;
                file_info.index_number    = 1;
                logf!("  -> ROOT\n");
                Ok(())
            }
            Ctx::File => {
                file_info.file_attributes = FILE_ATTRIBUTE_NORMAL;
                file_info.file_size       = FILE_CONTENT.len() as u64;
                file_info.allocation_size = alloc_size(FILE_CONTENT.len() as u64);
                file_info.index_number    = 2;
                logf!("  -> hello.txt\n");
                Ok(())
            }
        }
    }

    fn get_security(
        &self,
        _context: &Self::FileContext,
        _sd: Option<&mut [c_void]>,
    ) -> Result<u64> {
        logf!("[GetSecurity]\n");
        Ok(0)
    }

    fn read(&self, context: &Self::FileContext, buffer: &mut [u8], offset: u64) -> Result<u32> {
        logf!("[Read] {:?} off={} len={}\n", context, offset, buffer.len());
        match context {
            Ctx::Root => Err(err(STATUS_ACCESS_DENIED)),
            Ctx::File => {
                let off = offset as usize;
                if off >= FILE_CONTENT.len() {
                    logf!("  -> EOF\n");
                    return Ok(0);
                }
                let to_read = buffer.len().min(FILE_CONTENT.len() - off);
                buffer[..to_read].copy_from_slice(&FILE_CONTENT[off..off + to_read]);
                logf!("  -> {} bytes\n", to_read);
                Ok(to_read as u32)
            }
        }
    }

    fn read_directory(
        &self,
        context: &Self::FileContext,
        _pattern: Option<&U16CStr>,
        marker: DirMarker,
        buffer: &mut [u8],
    ) -> Result<u32> {
        logf!(
            "[ReadDirectory] {:?} marker={:?}\n",
            context,
            marker.inner_as_cstr().map(|m| m.to_os_string())
        );
        match context {
            Ctx::Root => {
                let mut cursor: u32 = 0;
                if marker.is_none() {
                    let mut di = DirInfo::<255>::new();
                    di.set_name(FILE_NAME)?;
                    let fi = di.file_info_mut();
                    fi.file_attributes = FILE_ATTRIBUTE_NORMAL;
                    fi.file_size       = FILE_CONTENT.len() as u64;
                    fi.allocation_size = alloc_size(FILE_CONTENT.len() as u64);
                    fi.index_number    = 2;
                    logf!("  -> add hello.txt\n");
                    if !di.append_to_buffer(buffer, &mut cursor) {
                        logf!("  -> buffer full\n");
                        return Ok(cursor);
                    }
                }
                DirInfo::<255>::finalize_buffer(buffer, &mut cursor);
                logf!("  -> done\n");
                Ok(cursor)
            }
            Ctx::File => Err(err(STATUS_ACCESS_DENIED)),
        }
    }

    // ══ 只读：拒绝所有写操作 ══

    fn create(&self, file_name: &U16CStr, _co: u32, _ga: FILE_ACCESS_RIGHTS,
              _fa: FILE_FLAGS_AND_ATTRIBUTES, _sd: Option<&[c_void]>,
              _as: u64, _eb: Option<&[u8]>, _rp: bool,
              _fi: &mut OpenFileInfo) -> Result<Self::FileContext> {
        logf!("[Create] {:?} -> ACCESS_DENIED\n", file_name.to_os_string());
        Err(err(STATUS_ACCESS_DENIED))
    }

    fn overwrite(&self, _c: &Self::FileContext, _fa: FILE_FLAGS_AND_ATTRIBUTES,
                 _rf: bool, _as: u64, _eb: Option<&[u8]>,
                 _fi: &mut FileInfo) -> Result<()> {
        logf!("[Overwrite] -> ACCESS_DENIED\n");
        Err(err(STATUS_ACCESS_DENIED))
    }

    fn write(&self, _c: &Self::FileContext, _b: &[u8], _o: u64,
             _we: bool, _ci: bool, _fi: &mut FileInfo) -> Result<u32> {
        logf!("[Write] -> ACCESS_DENIED\n");
        Err(err(STATUS_ACCESS_DENIED))
    }

    fn set_file_size(&self, _c: &Self::FileContext, _ns: u64,
                     _sa: bool, _fi: &mut FileInfo) -> Result<()> {
        logf!("[SetFileSize] -> ACCESS_DENIED\n");
        Err(err(STATUS_ACCESS_DENIED))
    }

    fn set_delete(&self, _c: &Self::FileContext, _fn: &U16CStr, _df: bool) -> Result<()> {
        logf!("[CanDelete] -> ACCESS_DENIED\n");
        Err(err(STATUS_ACCESS_DENIED))
    }

    fn rename(&self, _c: &Self::FileContext, _fn: &U16CStr,
              _nf: &U16CStr, _ri: bool) -> Result<()> {
        logf!("[Rename] -> ACCESS_DENIED\n");
        Err(err(STATUS_ACCESS_DENIED))
    }

    fn set_security(&self, _c: &Self::FileContext, _si: u32,
                    _md: ModificationDescriptor) -> Result<()> {
        logf!("[SetSecurity] -> ACCESS_DENIED\n");
        Err(err(STATUS_ACCESS_DENIED))
    }

    fn get_reparse_point(&self, _c: &Self::FileContext, _fn: &U16CStr,
                         _b: &mut [u8]) -> Result<u64> {
        logf!("[GetReparsePoint] -> NOT_A_REPARSE_POINT\n");
        Err(err(STATUS_NOT_A_REPARSE_POINT))
    }

    fn get_reparse_point_by_name(&self, _fn: &U16CStr, _id: bool,
                                  _b: &mut [u8]) -> Result<u64> {
        Err(err(STATUS_NOT_A_REPARSE_POINT))
    }

    fn get_stream_info(&self, _c: &Self::FileContext, _b: &mut [u8]) -> Result<u32> {
        logf!("[GetStreamInfo]\n");
        Ok(0)
    }
}

// ══════════════════════════════════════════════════════════════════════
// 入口
// ══════════════════════════════════════════════════════════════════════

fn main() {
    // 第一时间打开日志文件
    log_open();

    // 设置 panic hook，确保 panic 信息写入日志
    let default_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |info| {
        logf!("!!! PANIC: {}\n", info);
        if let Some(loc) = info.location() {
            logf!("!!! at {}:{}:{}\n", loc.file(), loc.line(), loc.column());
        }
        log_close();
        default_hook(info);
    }));

    logf!("hello_fs starting...\n");
    logf!("cwd={:?}\n", std::env::current_dir().ok());

    // 诊断信息
    logf!("WinFSP install dir exists: {}\n",
        std::path::Path::new("C:\\Program Files (x86)\\WinFsp").exists());
    logf!("WinFSP bin exists: {}\n",
        std::path::Path::new("C:\\Program Files (x86)\\WinFsp\\bin").exists());

    // 初始化 WinFSP —— 用 winfsp_init 替代 winfsp_init_or_die，捕获错误
    logf!("Calling winfsp_init...\n");
    match winfsp::winfsp_init() {
        Ok(_init) => {
            logf!("winfsp_init OK\n");
        }
        Err(e) => {
            logf!("winfsp_init FAILED: {:?}\n", e);
            logf!("Make sure WinFSP is installed.\n");
            logf!("Download: https://github.com/winfsp/winfsp/releases\n");
            logf!("Press Enter to exit.\n");
            log_close();
            let _ = std::io::stdin().read_line(&mut String::new());
            return;
        }
    }

    // 卷参数
    let mut volume_params = VolumeParams::new();
    volume_params
        .sector_size(512)
        .sectors_per_allocation_unit(1)
        .max_component_length(255)
        .volume_creation_time(0)
        .volume_serial_number(0xDEADBEEF)
        .file_info_timeout(1000)
        .case_sensitive_search(false)
        .case_preserved_names(true)
        .unicode_on_disk(true)
        .persistent_acls(false)
        .reparse_points(false)
        .named_streams(false)
        .read_only_volume(true)
        .post_cleanup_when_modified_only(true)
        .post_disposition_only_when_necessary(true)
        .flush_and_purge_on_cleanup(false)
        .allow_open_in_kernel_mode(false);

    logf!("VolumeParams: sector=512 au=1 serial=0xDEADBEEF\n");

    // 检查挂载点
    let mount_path = std::path::Path::new("D:\\test");
    logf!("Mount point exists: {} (is_dir: {})\n",
        mount_path.exists(),
        mount_path.is_dir());

    // 创建文件系统
    logf!("Calling FileSystemHost::new...\n");
    match FileSystemHost::new(volume_params, HelloFS) {
        Ok(mut host) => {
            logf!("FileSystemHost created OK\n");

            // 挂载
            logf!("Calling mount(\"{}\")...\n", MOUNT_POINT);
            match host.mount(MOUNT_POINT) {
                Ok(()) => logf!("SetMountPoint OK\n"),
                Err(e) => {
                    logf!("SetMountPoint FAILED: {:?}\n", e);
                    logf!("Is D:\\test already in use? Run 'net use' or check Explorer.\n");
                    logf!("Press Enter to exit.\n");
                    log_close();
                    let _ = std::io::stdin().read_line(&mut String::new());
                    return;
                }
            }

            // 启动分发器
            logf!("Calling start...\n");
            match FileSystemHost::<HelloFS>::start(&mut host) {
                Ok(()) => logf!("Dispatcher started\n"),
                Err(e) => {
                    logf!("StartDispatcher FAILED: {:?}\n", e);
                    logf!("Press Enter to exit.\n");
                    log_close();
                    let _ = std::io::stdin().read_line(&mut String::new());
                    return;
                }
            }

            logf!("========================================\n");
            logf!("Mounted at {}\n", MOUNT_POINT);
            logf!("Open D:\\test in Explorer -> hello.txt\n");
            logf!("Press Enter to unmount.\n");
            logf!("========================================\n");

            let _ = std::io::stdin().read_line(&mut String::new());

            logf!("Shutting down...\n");
            drop(host);
            logf!("Unmounted. Goodbye.\n");
        }
        Err(e) => {
            logf!("FspFileSystemCreate FAILED: {:?}\n", e);
            logf!("Is WinFSP service running? Check services.msc for 'WinFsp'\n");
            logf!("Press Enter to exit.\n");
            log_close();
            let _ = std::io::stdin().read_line(&mut String::new());
            return;
        }
    }

    log_close();
}
