#include <sys/stat.h>

#include "util.h"

int get_file_size(int fd) {
    struct stat stbuf;
    fstat(fd, &stbuf);

    return stbuf.st_size;
}
