#define _POSIX_C_SOURCE 200809L
#include "../src/tools/platform.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void require(bool success)
{
    if (!success) {
        fprintf(stderr, "Platform test failed: %s\n", strerror(errno));
        exit(1);
    }
}

int main(void)
{
    char first[] = "tool-publish-first.tmp.XXXXXX";
    char second[] = "tool-publish-second.tmp.XXXXXX";
    char destination[] = "tool-publish-destination.tmp.XXXXXX";
    int a = tool_mkstemp(first);
    int b = tool_mkstemp(second);
    int d = tool_mkstemp(destination);
    require(a >= 0 && b >= 0 && d >= 0);
    require(tool_close(d) == 0 && tool_unlink(destination) == 0);
    FILE *output = tool_fdopen(a, "wb");
    require(output != NULL && fputs("first", output) >= 0 && fclose(output) == 0);
    output = tool_fdopen(b, "wb");
    require(output != NULL && fputs("second", output) >= 0 && fclose(output) == 0);
    require(tool_publish(first, destination, true) == 0);
    require(tool_publish(second, destination, true) != 0 && errno == EEXIST);
    char contents[7] = { 0 };
    FILE *input = fopen(destination, "rb");
    require(input != NULL && fread(contents, 1, 5, input) == 5 && fclose(input) == 0);
    require(strcmp(contents, "first") == 0);
    require(tool_publish(second, destination, false) == 0);
    input = fopen(destination, "rb");
    require(input != NULL && fread(contents, 1, 6, input) == 6 && fclose(input) == 0);
    require(strcmp(contents, "second") == 0);
    require(tool_unlink(destination) == 0);
    return 0;
}
