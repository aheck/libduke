#ifndef DUKE_TEST_PLATFORM_H
#define DUKE_TEST_PLATFORM_H
#ifdef _WIN32
#include <process.h>
#define test_process_id _getpid
#else
#include <unistd.h>
#define test_process_id getpid
#endif
#endif
