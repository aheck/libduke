#ifndef DUKE_TOOL_PLATFORM_H
#define DUKE_TOOL_PLATFORM_H
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
typedef struct _stat64 ToolStat;
#define tool_stat _stat64
#define tool_regular(mode) (((mode) & _S_IFMT) == _S_IFREG)
#define tool_close _close
#define tool_fdopen _fdopen
#define tool_fileno _fileno
#define tool_sync _commit
#define tool_unlink _unlink
#else
#include <unistd.h>
typedef struct stat ToolStat;
#define tool_stat stat
#define tool_regular(mode) S_ISREG(mode)
#define tool_close close
#define tool_fdopen fdopen
#define tool_fileno fileno
#define tool_sync fsync
#define tool_unlink unlink
#endif

/* Private tool I/O, not part of the installed libduke API. */
typedef struct ToolDirectory ToolDirectory;
ToolDirectory *tool_opendir(const char *path);
/* Returns a borrowed filename; NULL means EOF (errno=0) or an error. */
const char *tool_readdir(ToolDirectory *directory);
int tool_closedir(ToolDirectory *directory);
int tool_lstat(const char *path, ToolStat *status);
/* Exclusively create a binary temporary file next to the destination. The
 * mutable template ends in XXXXXX. Returns a CRT/POSIX descriptor, or -1. */
int tool_mkstemp(char *name);
void tool_permissions(int fd, const char *destination, bool creating);
/* Publish a completed temporary file. When creating, never replace an existing
 * name, even if it appeared after the initial existence check. */
int tool_publish(const char *temporary, const char *destination, bool creating);
#endif
