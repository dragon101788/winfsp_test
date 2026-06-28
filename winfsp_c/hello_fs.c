/**
 * hello_fs.c -- WinFSP Minimal Verification Project
 *
 * Mounts D:\test as a virtual read-only filesystem.
 * Contains a single file: \hello.txt with content "Hello from WinFSP!\r\n"
 * All data is in memory; no real disk access.
 *
 * Aligned with working Python/winfspy reference implementation.
 * Logs to both console and hello_fs.log.
 */

#include <windows.h>
#include <winfsp/winfsp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <delayimp.h>

/* -- Delay-load hook: find winfsp-x64.dll in SxS directory -------- */

static HMODULE g_wfsp_dll = NULL;

FARPROC WINAPI delay_load_hook(unsigned dliNotify, PDelayLoadInfo pdli)
{
    if (dliNotify == dliFailLoadLib)
    {
        /* Already found it? Return cached */
        if (g_wfsp_dll)
            return (FARPROC)g_wfsp_dll;

        /* Compatible DLL names in SxS */
        static const WCHAR *candidates[] = {
            L"winfsp-x64.dll",
            NULL
        };

        /* Search in WinFSP SxS directory */
        WCHAR pattern[MAX_PATH];
        wcscpy_s(pattern, _countof(pattern),
                 L"C:\\Program Files (x86)\\WinFsp\\SxS\\sxs.*");
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(pattern, &fd);
        while (INVALID_HANDLE_VALUE != hFind)
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY &&
                0 == wcsncmp(fd.cFileName, L"sxs.", 4))
            {
                for (const WCHAR **c = candidates; *c; c++)
                {
                    WCHAR dll_path[MAX_PATH];
                    swprintf_s(dll_path, _countof(dll_path),
                        L"C:\\Program Files (x86)\\WinFsp\\SxS\\%s\\bin\\%s",
                        fd.cFileName, *c);

                    HMODULE mod = LoadLibraryExW(dll_path, NULL,
                        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                        LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
                    if (mod)
                    {
                        g_wfsp_dll = mod;
                        FindClose(hFind);
                        return (FARPROC)mod;
                    }
                }
            }
            if (!FindNextFileW(hFind, &fd))
                break;
        }
        if (INVALID_HANDLE_VALUE != hFind)
            FindClose(hFind);
    }
    return NULL;
}

const PfnDliHook __pfnDliFailureHook2 = delay_load_hook;

/* -- Mount point -------------------------------------------------- */

#define MOUNT_POINT             L"D:\\test"
#define LOG_PATH                L"hello_fs.log"

/* -- File system constants ---------------------------------------- */

static const WCHAR *VOLUME_LABEL  = L"HelloFS";
#define VOLUME_LABEL_LENGTH        14

static const WCHAR *FILE_NAME      = L"hello.txt";
#define FILE_NAME_SIZE             (10 * sizeof(WCHAR))

static const char *FILE_CONTENT    = "Hello from WinFSP!\r\n";
#define FILE_CONTENT_SIZE          20

/* -- Context values (file nodes) ---------------------------------- */

enum { CTX_ROOT = 1, CTX_FILE = 2 };

#define ALLOC_SIZE(s)   (((s) + 4095) & ~4095)  /* align to 4K */

/* -- Logger (thread-safe) ----------------------------------------- */

static FILE *g_log = NULL;
static CRITICAL_SECTION g_log_cs;

static void log_open(void)
{
    InitializeCriticalSection(&g_log_cs);
    g_log = _wfopen(LOG_PATH, L"w");
    if (g_log)
        fwprintf(g_log, L"=== hello_fs log start ===\n");
}

static void log_close(void)
{
    if (g_log)
    {
        fwprintf(g_log, L"=== log end ===\n");
        fclose(g_log);
        g_log = NULL;
    }
    DeleteCriticalSection(&g_log_cs);
}

static void log_write(const WCHAR *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vwprintf(fmt, args);
    va_end(args);

    EnterCriticalSection(&g_log_cs);
    if (g_log)
    {
        va_start(args, fmt);
        vfwprintf(g_log, fmt, args);
        fflush(g_log);
        va_end(args);
    }
    LeaveCriticalSection(&g_log_cs);
}

/* ==================================================================
 * FSP_FILE_SYSTEM_INTERFACE callbacks
 * ================================================================== */

static NTSTATUS
GetVolumeInfo(FSP_FILE_SYSTEM *FileSystem, FSP_FSCTL_VOLUME_INFO *VolumeInfo)
{
    (void)FileSystem;
    log_write(L"[GetVolumeInfo]\n");

    VolumeInfo->TotalSize         = 64 * 1024 * 1024;
    VolumeInfo->FreeSize          = 64 * 1024 * 1024;
    VolumeInfo->VolumeLabelLength = VOLUME_LABEL_LENGTH;
    memcpy(VolumeInfo->VolumeLabel, VOLUME_LABEL, VOLUME_LABEL_LENGTH);
    return STATUS_SUCCESS;
}

static NTSTATUS
GetSecurityByName(FSP_FILE_SYSTEM      *FileSystem,
                  PWSTR                 FileName,
                  PUINT32               PFileAttributes,
                  PSECURITY_DESCRIPTOR  SecurityDescriptor,
                  SIZE_T               *PSecurityDescriptorSize)
{
    (void)FileSystem;
    (void)SecurityDescriptor;

    log_write(L"[GetSecurityByName] %s\n", FileName);

    if (0 == wcscmp(FileName, L"\\"))
    {
        if (PFileAttributes) *PFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        if (PSecurityDescriptorSize) *PSecurityDescriptorSize = 0;
        log_write(L"  -> ROOT OK\n");
        return STATUS_SUCCESS;
    }

    if (0 == _wcsicmp(FileName, L"\\hello.txt"))
    {
        if (PFileAttributes) *PFileAttributes = FILE_ATTRIBUTE_NORMAL;
        if (PSecurityDescriptorSize) *PSecurityDescriptorSize = 0;
        log_write(L"  -> hello.txt OK\n");
        return STATUS_SUCCESS;
    }

    log_write(L"  -> NOT FOUND\n");
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

static NTSTATUS
GetSecurity(FSP_FILE_SYSTEM     *FileSystem,
            PVOID                FileContext,
            PSECURITY_DESCRIPTOR SecurityDescriptor,
            SIZE_T              *PSecurityDescriptorSize)
{
    (void)FileSystem;
    (void)FileContext;
    (void)SecurityDescriptor;

    log_write(L"[GetSecurity] ctx=%lu\n", (ULONG)(UINT_PTR)FileContext);
    if (PSecurityDescriptorSize) *PSecurityDescriptorSize = 0;
    return STATUS_SUCCESS;
}

static VOID
Cleanup(FSP_FILE_SYSTEM *FileSystem,
        PVOID            FileContext,
        PWSTR            FileName,
        ULONG            Flags)
{
    (void)FileSystem; (void)FileContext; (void)FileName; (void)Flags;
    log_write(L"[Cleanup] %s\n", FileName);
}

static NTSTATUS
Open(FSP_FILE_SYSTEM     *FileSystem,
     PWSTR                FileName,
     UINT32               CreateOptions,
     UINT32               GrantedAccess,
     PVOID               *PFileContext,
     FSP_FSCTL_FILE_INFO *FileInfo)
{
    (void)FileSystem; (void)CreateOptions; (void)GrantedAccess;

    log_write(L"[Open] %s\n", FileName);

    if (0 == wcscmp(FileName, L"\\"))
    {
        *PFileContext = (PVOID)(UINT_PTR)CTX_ROOT;
        memset(FileInfo, 0, sizeof(*FileInfo));
        FileInfo->FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        log_write(L"  -> CTX_ROOT\n");
        return STATUS_SUCCESS;
    }

    if (0 == _wcsicmp(FileName, L"\\hello.txt"))
    {
        *PFileContext = (PVOID)(UINT_PTR)CTX_FILE;
        memset(FileInfo, 0, sizeof(*FileInfo));
        FileInfo->FileAttributes = FILE_ATTRIBUTE_NORMAL;
        FileInfo->FileSize       = FILE_CONTENT_SIZE;
        FileInfo->AllocationSize = ALLOC_SIZE(FILE_CONTENT_SIZE);
        FileInfo->HardLinks      = 1;
        log_write(L"  -> CTX_FILE\n");
        return STATUS_SUCCESS;
    }

    log_write(L"  -> NOT FOUND\n");
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

static VOID
Close(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext)
{
    (void)FileSystem;
    log_write(L"[Close] ctx=%lu\n", (ULONG)(UINT_PTR)FileContext);
}

static NTSTATUS
Read(FSP_FILE_SYSTEM *FileSystem,
     PVOID            FileContext,
     PVOID            Buffer,
     UINT64           Offset,
     ULONG            Length,
     PULONG           PBytesTransferred)
{
    (void)FileSystem;

    log_write(L"[Read] ctx=%lu off=%llu len=%lu\n",
              (ULONG)(UINT_PTR)FileContext, Offset, Length);

    if (CTX_FILE != (UINT_PTR)FileContext)
        return STATUS_INVALID_DEVICE_REQUEST;

    if (Offset >= FILE_CONTENT_SIZE)
    {
        *PBytesTransferred = 0;
        log_write(L"  -> EOF\n");
        return STATUS_SUCCESS;
    }

    ULONG todo = Length;
    if (Offset + Length > FILE_CONTENT_SIZE)
        todo = (ULONG)(FILE_CONTENT_SIZE - Offset);

    memcpy(Buffer, FILE_CONTENT + Offset, todo);
    *PBytesTransferred = todo;
    log_write(L"  -> %lu bytes\n", todo);
    return STATUS_SUCCESS;
}

static NTSTATUS
ReadDirectory(FSP_FILE_SYSTEM *FileSystem,
              PVOID            FileContext,
              PWSTR            Pattern,
              PWSTR            Marker,
              PVOID            Buffer,
              ULONG            Length,
              PULONG           PBytesTransferred)
{
    (void)FileSystem; (void)Pattern;
    log_write(L"[ReadDirectory] ctx=%lu Marker=%s\n",
              (ULONG)(UINT_PTR)FileContext,
              Marker ? Marker : L"(null)");

    if (CTX_ROOT != (UINT_PTR)FileContext)
        return STATUS_INVALID_DEVICE_REQUEST;

    if (0 == Marker)
    {
        UINT8 Buf[sizeof(FSP_FSCTL_DIR_INFO) + FILE_NAME_SIZE];
        FSP_FSCTL_DIR_INFO *DirInfo = (FSP_FSCTL_DIR_INFO *)Buf;

        memset(DirInfo, 0, sizeof(Buf));
        DirInfo->Size = (UINT16)(sizeof(FSP_FSCTL_DIR_INFO) + FILE_NAME_SIZE);
        DirInfo->FileInfo.FileAttributes = FILE_ATTRIBUTE_NORMAL;
        DirInfo->FileInfo.FileSize       = FILE_CONTENT_SIZE;
        DirInfo->FileInfo.AllocationSize = ALLOC_SIZE(FILE_CONTENT_SIZE);
        DirInfo->FileInfo.HardLinks      = 1;
        /* IndexNumber: 2 = file entry (1 = root) */
        DirInfo->FileInfo.IndexNumber    = 2;
        DirInfo->FileInfo.CreationTime   = 0;
        DirInfo->FileInfo.LastAccessTime = 0;
        DirInfo->FileInfo.LastWriteTime  = 0;
        DirInfo->FileInfo.ChangeTime     = 0;
        memcpy(DirInfo->FileNameBuf, FILE_NAME, FILE_NAME_SIZE);

        log_write(L"  -> add hello.txt\n");
        if (!FspFileSystemAddDirInfo(DirInfo, Buffer, Length, PBytesTransferred))
        {
            log_write(L"  -> buffer full\n");
            return STATUS_SUCCESS;
        }
    }

    FspFileSystemAddDirInfo(0, Buffer, Length, PBytesTransferred);
    log_write(L"  -> done\n");
    return STATUS_SUCCESS;
}

static NTSTATUS
GetFileInfo(FSP_FILE_SYSTEM     *FileSystem,
            PVOID                FileContext,
            FSP_FSCTL_FILE_INFO *FileInfo)
{
    (void)FileSystem;
    log_write(L"[GetFileInfo] ctx=%lu\n", (ULONG)(UINT_PTR)FileContext);

    memset(FileInfo, 0, sizeof(*FileInfo));

    switch ((UINT_PTR)FileContext)
    {
    case CTX_ROOT:
        FileInfo->FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        FileInfo->AllocationSize = 0;
        FileInfo->FileSize       = 0;
        FileInfo->HardLinks      = 1;
        FileInfo->IndexNumber    = 1;
        log_write(L"  -> ROOT\n");
        return STATUS_SUCCESS;

    case CTX_FILE:
        FileInfo->FileAttributes = FILE_ATTRIBUTE_NORMAL;
        FileInfo->FileSize       = FILE_CONTENT_SIZE;
        FileInfo->AllocationSize = ALLOC_SIZE(FILE_CONTENT_SIZE);
        FileInfo->HardLinks      = 1;
        FileInfo->IndexNumber    = 2;
        log_write(L"  -> hello.txt\n");
        return STATUS_SUCCESS;

    default:
        return STATUS_INVALID_HANDLE;
    }
}

/* -- Read-only enforcement ---------------------------------------- */

static NTSTATUS
Create(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName,
       UINT32 CreateOptions, UINT32 GrantedAccess,
       UINT32 FileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor,
       UINT64 AllocationSize, PVOID *PFileContext,
       FSP_FSCTL_FILE_INFO *FileInfo)
{
    (void)FileSystem; (void)FileName; (void)CreateOptions;
    (void)GrantedAccess; (void)FileAttributes;
    (void)SecurityDescriptor; (void)AllocationSize;
    (void)PFileContext; (void)FileInfo;
    log_write(L"[Create] %s -> ACCESS_DENIED\n", FileName);
    return STATUS_ACCESS_DENIED;
}

static NTSTATUS
Overwrite(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext,
          UINT32 FileAttributes, BOOLEAN ReplaceFileAttributes,
          UINT64 AllocationSize, FSP_FSCTL_FILE_INFO *FileInfo)
{
    (void)FileSystem; (void)FileContext; (void)FileAttributes;
    (void)ReplaceFileAttributes; (void)AllocationSize; (void)FileInfo;
    log_write(L"[Overwrite] -> ACCESS_DENIED\n");
    return STATUS_ACCESS_DENIED;
}

static NTSTATUS
Write(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext,
      PVOID Buffer, UINT64 Offset, ULONG Length,
      BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo,
      PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO *FileInfo)
{
    (void)FileSystem; (void)FileContext; (void)Buffer;
    (void)Offset; (void)Length; (void)WriteToEndOfFile;
    (void)ConstrainedIo; (void)PBytesTransferred; (void)FileInfo;
    log_write(L"[Write] -> ACCESS_DENIED\n");
    return STATUS_ACCESS_DENIED;
}

static NTSTATUS
SetFileSize(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext,
            UINT64 NewSize, BOOLEAN SetAllocationSize,
            FSP_FSCTL_FILE_INFO *FileInfo)
{
    (void)FileSystem; (void)FileContext; (void)NewSize;
    (void)SetAllocationSize; (void)FileInfo;
    log_write(L"[SetFileSize] -> ACCESS_DENIED\n");
    return STATUS_ACCESS_DENIED;
}

static NTSTATUS
CanDelete(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PWSTR FileName)
{
    (void)FileSystem; (void)FileContext; (void)FileName;
    log_write(L"[CanDelete] -> ACCESS_DENIED\n");
    return STATUS_ACCESS_DENIED;
}

static NTSTATUS
Rename(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext,
       PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists)
{
    (void)FileSystem; (void)FileContext; (void)FileName;
    (void)NewFileName; (void)ReplaceIfExists;
    log_write(L"[Rename] -> ACCESS_DENIED\n");
    return STATUS_ACCESS_DENIED;
}

static NTSTATUS
SetSecurity(FSP_FILE_SYSTEM     *FileSystem,
            PVOID                FileContext,
            SECURITY_INFORMATION SecurityInformation,
            PSECURITY_DESCRIPTOR ModificationDescriptor)
{
    (void)FileSystem; (void)FileContext;
    (void)SecurityInformation; (void)ModificationDescriptor;
    log_write(L"[SetSecurity] -> ACCESS_DENIED\n");
    return STATUS_ACCESS_DENIED;
}

static NTSTATUS
GetReparsePoint(FSP_FILE_SYSTEM *FileSystem,
                PVOID            FileContext,
                PWSTR            FileName,
                PVOID            Buffer,
                PSIZE_T          PSize)
{
    (void)FileSystem; (void)FileContext; (void)FileName;
    (void)Buffer; (void)PSize;
    log_write(L"[GetReparsePoint] -> NOT_FOUND\n");
    return STATUS_NOT_A_REPARSE_POINT;
}

static NTSTATUS
GetStreamInfo(FSP_FILE_SYSTEM *FileSystem,
              PVOID            FileContext,
              PVOID            Buffer,
              ULONG            Length,
              PULONG           PBytesTransferred)
{
    (void)FileSystem; (void)FileContext;
    (void)Buffer; (void)Length;
    log_write(L"[GetStreamInfo]\n");
    *PBytesTransferred = 0;
    return STATUS_SUCCESS;
}

/* ==================================================================
 * main
 * ================================================================== */

int wmain(void)
{
    NTSTATUS Status;
    FSP_FILE_SYSTEM *FileSystem = NULL;
    FSP_FILE_SYSTEM_INTERFACE Iface;

    log_open();
    log_write(L"hello_fs starting...\n");

    /* Build interface — align with working Python winfspy impl */
    memset(&Iface, 0, sizeof(Iface));
    Iface.GetVolumeInfo     = GetVolumeInfo;
    Iface.GetSecurityByName = GetSecurityByName;
    Iface.GetSecurity       = GetSecurity;
    Iface.Cleanup           = Cleanup;
    Iface.Open              = Open;
    Iface.Close             = Close;
    Iface.Read              = Read;
    Iface.ReadDirectory     = ReadDirectory;
    Iface.GetFileInfo       = GetFileInfo;
    /* Read-only: reject all write operations */
    Iface.Create            = Create;
    Iface.Overwrite         = Overwrite;
    Iface.Write             = Write;
    Iface.SetFileSize       = SetFileSize;
    Iface.CanDelete         = CanDelete;
    Iface.Rename            = Rename;
    Iface.SetSecurity       = SetSecurity;
    /* Reparse + streams: return empty */
    Iface.GetReparsePoint   = GetReparsePoint;
    Iface.GetStreamInfo     = GetStreamInfo;

    log_write(L"Interface: 18 callbacks set\n");

    /* Build volume params — match Python winfspy defaults */
    FSP_FSCTL_VOLUME_PARAMS VolumeParams;
    memset(&VolumeParams, 0, sizeof(VolumeParams));
    VolumeParams.Version                = sizeof(FSP_FSCTL_VOLUME_PARAMS);
    VolumeParams.SectorSize             = 512;
    VolumeParams.SectorsPerAllocationUnit = 1;       /* matches Python */
    VolumeParams.MaxComponentLength     = 255;
    VolumeParams.VolumeCreationTime     = 0;
    VolumeParams.VolumeSerialNumber     = 0xDEADBEEF; /* matches Python */
    VolumeParams.FileInfoTimeout        = 1000;
    VolumeParams.CaseSensitiveSearch    = 0;
    VolumeParams.CasePreservedNames     = 1;
    VolumeParams.UnicodeOnDisk          = 1;
    VolumeParams.PersistentAcls         = 0;         /* matches Python */
    VolumeParams.ReparsePoints          = 0;
    VolumeParams.NamedStreams           = 0;
    VolumeParams.HardLinks              = 0;
    VolumeParams.ExtendedAttributes     = 0;
    VolumeParams.ReadOnlyVolume         = 1;
    VolumeParams.PostCleanupWhenModifiedOnly   = 1;
    VolumeParams.PostDispositionWhenNecessaryOnly = 1;
    VolumeParams.FlushAndPurgeOnCleanup  = 0;
    VolumeParams.AllowOpenInKernelMode   = 0;
    VolumeParams.SupportsPosixUnlinkRename = 0;

    log_write(L"VolumeParams: sectors/au=%u serial=0x%x acls=%u\n",
              VolumeParams.SectorsPerAllocationUnit,
              VolumeParams.VolumeSerialNumber,
              VolumeParams.PersistentAcls);

    /* Create filesystem */
    log_write(L"Calling FspFileSystemCreate...\n");
    __try {
        Status = FspFileSystemCreate(
            L"" FSP_FSCTL_DISK_DEVICE_NAME,
            &VolumeParams, &Iface, &FileSystem);
    } __except(GetExceptionCode()) {
        log_write(L"!!! EXCEPTION 0x%08lx in FspFileSystemCreate\n",
                  GetExceptionCode());
        goto exit;
    }

    if (!NT_SUCCESS(Status))
    {
        log_write(L"FspFileSystemCreate FAILED: 0x%08lx\n", Status);
        goto exit;
    }
    log_write(L"FspFileSystemCreate OK, fs=%p\n", (void*)FileSystem);

    /* Set mount point */
    Status = FspFileSystemSetMountPoint(FileSystem, MOUNT_POINT);
    if (!NT_SUCCESS(Status))
    {
        log_write(L"FspFileSystemSetMountPoint FAILED: 0x%08lx\n", Status);
        goto cleanup_fs;
    }
    log_write(L"SetMountPoint OK\n");

    /* Start dispatcher */
    Status = FspFileSystemStartDispatcher(FileSystem, 1);
    if (!NT_SUCCESS(Status))
    {
        log_write(L"StartDispatcher FAILED: 0x%08lx\n", Status);
        goto cleanup_fs;
    }
    log_write(L"Dispatcher started (ThreadCount=1)\n");
    log_write(L"========================================\n");
    log_write(L"Mounted at %s\n", MOUNT_POINT);
    log_write(L"Open D:\\test in Explorer -> hello.txt\n");
    log_write(L"Press Enter to unmount.\n");
    log_write(L"========================================\n");

    getwchar();
    log_write(L"Shutting down...\n");

    FspFileSystemStopDispatcher(FileSystem);
    FspFileSystemDelete(FileSystem);
    log_write(L"Unmounted. Goodbye.\n");
    log_close();
    return 0;

cleanup_fs:
    FspFileSystemDelete(FileSystem);
exit:
    log_write(L"Program exiting with error.\n");
    log_close();
    return 1;
}
