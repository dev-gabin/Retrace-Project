#ifndef RETRACE_DB_STORE_H
#define RETRACE_DB_STORE_H
#include <stddef.h>

/* All timestamps are UTC: YYYY-MM-DDTHH:MM:SS.ffffffZ.
 * One handle per thread. Free lists/images with free(). */
typedef struct RtStore RtStore;
typedef struct {
    char item[33];
    int observed;
    int has_position, pos_x, pos_y;
    char seen_at[28];
    char snapshot[256];
    int drawer_id; /* 0 means SQL NULL; otherwise 1..6 */
    char state[17];
} RtRecord;
typedef struct {
    const char *host, *user, *password, *database, *data_dir;
    unsigned int port;
} RtConfig;
enum { RT_OK=0, RT_NOT_FOUND=1, RT_STALE=2, RT_ERROR=-1,
       RT_COMMIT_UNKNOWN=-2, RT_CLEANUP_PENDING=3 };

int rt_open(RtStore **out, const RtConfig *config, char *error, size_t error_size);
void rt_close(RtStore *store);
const char *rt_error(const RtStore *store);
int rt_list(RtStore *store, RtRecord **rows, size_t *count);
int rt_get(RtStore *store, const char *item, RtRecord *row);
/* Save an observation referencing an already-written managed JPEG path
 * relative to data_dir, e.g. snapshots/rt_<32 lowercase hex digits>.jpg.
 * The JPEG is validated; the caller must publish it completely and never
 * modify it afterward. x/y must lie inside the decoded full camera frame. */
int rt_save(RtStore *store, const char *item, int x, int y,
            const char *utc_time, int drawer_id, const char *state,
            const char *snapshot_path);
int rt_load_snapshot(RtStore *store, const char *item,
                     unsigned char **jpeg, size_t *jpeg_size);
/* Remove only this library's unreferenced files; fail closed on DB errors.
 * Missing referenced files are reported, never hidden by deleting DB records. */
int rt_recover(RtStore *store, size_t *removed);
#endif
