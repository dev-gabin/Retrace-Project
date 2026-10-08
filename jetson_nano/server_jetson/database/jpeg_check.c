#include "jpeg_check.h"
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <jpeglib.h>

struct JpegCheck {
    struct jpeg_decompress_struct decoder;
    struct jpeg_error_mgr errors;
    jmp_buf jump;
    unsigned char *line;
};
static void fail(j_common_ptr common) {
    struct JpegCheck *check = (struct JpegCheck *)common->client_data;
    longjmp(check->jump, 1);
}
static void message(j_common_ptr common, int level) {
    /* libjpeg can otherwise silently repair a truncated image. */
    if (level < 0) fail(common);
}
int rt_check_jpeg(const unsigned char *data, size_t size, int *width, int *height) {
    if (!data || size < 4 || size > 32u*1024u*1024u) return -1;
    struct JpegCheck *check = calloc(1, sizeof(*check));
    if (!check) return -1;
    check->decoder.err = jpeg_std_error(&check->errors);
    check->errors.error_exit = fail;
    check->errors.emit_message = message;
    check->decoder.client_data = check;
    if (setjmp(check->jump)) {
        jpeg_destroy_decompress(&check->decoder);
        free(check->line);
        free(check);
        return -1;
    }
    jpeg_create_decompress(&check->decoder);
    jpeg_mem_src(&check->decoder, data, (unsigned long)size);
    jpeg_read_header(&check->decoder, TRUE);
    if (!check->decoder.image_width || !check->decoder.image_height ||
        check->decoder.image_width > 8192 || check->decoder.image_height > 8192 ||
        (size_t)check->decoder.image_width * check->decoder.image_height > 32000000u) {
        fail((j_common_ptr)&check->decoder);
    }
    check->decoder.out_color_space = JCS_RGB;
    jpeg_start_decompress(&check->decoder);
    check->line = malloc((size_t)check->decoder.output_width * 3);
    if (!check->line) fail((j_common_ptr)&check->decoder);
    while (check->decoder.output_scanline < check->decoder.output_height) {
        JSAMPROW line = check->line;
        if (jpeg_read_scanlines(&check->decoder, &line, 1) != 1)
            fail((j_common_ptr)&check->decoder);
    }
    *width = (int)check->decoder.output_width;
    *height = (int)check->decoder.output_height;
    jpeg_finish_decompress(&check->decoder);
    jpeg_destroy_decompress(&check->decoder);
    free(check->line);
    free(check);
    return 0;
}
