#define _POSIX_C_SOURCE 200809L
#include "platform.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>

static int windows_error(DWORD error)
{
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND: errno = ENOENT; break;
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS: errno = EEXIST; break;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION: errno = EACCES; break;
    case ERROR_DIRECTORY: errno = ENOTDIR; break;
    case ERROR_DISK_FULL: errno = ENOSPC; break;
    default: errno = EIO; break;
    }
    return -1;
}

struct ToolDirectory {
    HANDLE handle;
    WIN32_FIND_DATAA item;
    bool first;
};

ToolDirectory *tool_opendir(const char *path)
{
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        windows_error(GetLastError());
        return NULL;
    }
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        errno = ENOTDIR;
        return NULL;
    }
    size_t size = strlen(path) + 3;
    char *pattern = malloc(size);
    ToolDirectory *directory = calloc(1, sizeof(*directory));
    if (pattern == NULL || directory == NULL) {
        free(pattern);
        free(directory);
        errno = ENOMEM;
        return NULL;
    }
    snprintf(pattern, size, "%s/*", path);
    directory->handle = FindFirstFileA(pattern, &directory->item);
    DWORD error = GetLastError();
    free(pattern);
    if (directory->handle == INVALID_HANDLE_VALUE) {
        if (error != ERROR_FILE_NOT_FOUND) {
            free(directory);
            windows_error(error);
            return NULL;
        }
    }
    directory->first = true;
    return directory;
}

const char *tool_readdir(ToolDirectory *directory)
{
    errno = 0;
    if (directory->handle == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    if (directory->first) {
        directory->first = false;
        return directory->item.cFileName;
    }
    if (!FindNextFileA(directory->handle, &directory->item)) {
        DWORD error = GetLastError();
        if (error != ERROR_NO_MORE_FILES) {
            windows_error(error);
        }
        return NULL;
    }
    return directory->item.cFileName;
}

int tool_closedir(ToolDirectory *directory)
{
    int result = 0;
    if (directory->handle != INVALID_HANDLE_VALUE && !FindClose(directory->handle)) {
        result = windows_error(GetLastError());
    }
    free(directory);
    return result;
}

int tool_lstat(const char *path, ToolStat *status)
{
    /* GetFileAttributes also sees existing reparse-point names. The caller
     * only needs existence; publication performs the authoritative check. */
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        return windows_error(GetLastError());
    }
    memset(status, 0, sizeof(*status));
    return 0;
}

int tool_mkstemp(char *name)
{
    static LONG sequence = 0;
    size_t size = strlen(name);
    if (size < 6 || strcmp(name + size - 6, "XXXXXX") != 0) {
        errno = EINVAL;
        return -1;
    }
    for (unsigned attempt = 0; attempt < 256; ++attempt) {
        unsigned value = (GetCurrentProcessId() ^ GetTickCount()
            ^ (unsigned) InterlockedIncrement(&sequence)) & 0xffffff;
        snprintf(name + size - 6, 7, "%06x", value);
        HANDLE handle = CreateFileA(name, GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, NULL);
        if (handle != INVALID_HANDLE_VALUE) {
            int fd = _open_osfhandle((intptr_t) handle, _O_RDWR | _O_BINARY);
            if (fd < 0) {
                int saved = errno;
                CloseHandle(handle);
                DeleteFileA(name);
                errno = saved;
            }
            return fd;
        }
        DWORD error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
            return windows_error(error);
        }
    }
    errno = EEXIST;
    return -1;
}

void tool_permissions(int fd, const char *destination, bool creating)
{
    /* Windows files inherit the destination directory's ACL. POSIX mode bits
     * cannot represent it; do not try to translate them with chmod. */
    (void) fd;
    (void) destination;
    (void) creating;
}

int tool_publish(const char *temporary, const char *destination, bool creating)
{
    DWORD flags = MOVEFILE_WRITE_THROUGH;
    if (!creating) {
        flags |= MOVEFILE_REPLACE_EXISTING;
    }
    if (!MoveFileExA(temporary, destination, flags)) {
        return windows_error(GetLastError());
    }
    return 0;
}
#else
#include <dirent.h>
struct ToolDirectory { DIR *handle; };
ToolDirectory *tool_opendir(const char *path)
{
    ToolDirectory *directory = malloc(sizeof(*directory));
    if (directory == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    directory->handle = opendir(path);
    if (directory->handle == NULL) {
        free(directory);
        return NULL;
    }
    return directory;
}
const char *tool_readdir(ToolDirectory *directory)
{
    errno = 0;
    struct dirent *item = readdir(directory->handle);
    return item == NULL ? NULL : item->d_name;
}
int tool_closedir(ToolDirectory *directory)
{
    int result = closedir(directory->handle);
    free(directory);
    return result;
}
int tool_lstat(const char *path, ToolStat *status)
{
    return lstat(path, status);
}
int tool_mkstemp(char *name)
{
    return mkstemp(name);
}
void tool_permissions(int fd, const char *destination, bool creating)
{
    struct stat status;
    if (!creating && stat(destination, &status) == 0) {
        (void) fchmod(fd, status.st_mode);
    } else if (creating) {
        mode_t mask = umask(0);
        umask(mask);
        (void) fchmod(fd, 0666 & ~mask);
    }
}
int tool_publish(const char *temporary, const char *destination, bool creating)
{
    if (!creating) {
        return rename(temporary, destination);
    }
    if (link(temporary, destination) != 0) {
        return -1;
    }
    if (unlink(temporary) != 0) {
        fprintf(stderr, "WARNING: Failed to remove temporary name %s\n", temporary);
    }
    return 0;
}
#endif
