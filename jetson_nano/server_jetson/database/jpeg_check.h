#ifndef RETRACE_JPEG_CHECK_H
#define RETRACE_JPEG_CHECK_H
#include <stddef.h>
int rt_check_jpeg(const unsigned char *data, size_t size, int *width, int *height);
#endif
