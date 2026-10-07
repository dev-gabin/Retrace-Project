#define _GNU_SOURCE
#include "db_store.h"
#include "jpeg_check.h"
#include <mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <dirent.h>
#include <time.h>

struct RtStore {
    MYSQL *db;
    int root, images, lock;
    int broken;
    char error[512];
};
static int errorf(RtStore *s, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(s->error, sizeof(s->error), fmt, ap);
    va_end(ap); return RT_ERROR;
}
const char *rt_error(const RtStore *s) { return s ? s->error : "No storage handle"; }
static int query(RtStore *s, const char *sql) {
    if (s->broken) return errorf(s, "Connection uncertain; close and reopen storage first");
    if (mysql_query(s->db, sql)) return errorf(s, "Database: %s", mysql_error(s->db));
    return RT_OK;
}
static int scalar_one(RtStore *s, const char *sql) {
    if (query(s, sql)) return RT_ERROR;
    MYSQL_RES *result = mysql_store_result(s->db);
    if (!result) return errorf(s,"Cannot read database lock result");
    MYSQL_ROW row = mysql_fetch_row(result);
    int ok = row && row[0] && strcmp(row[0],"1") == 0;
    mysql_free_result(result);
    return ok ? RT_OK : errorf(s,"Storage lock unavailable (10 second timeout)");
}
static int lock_store(RtStore *s) {
    if (flock(s->lock, LOCK_EX)) return errorf(s,"File lock: %s",strerror(errno));
    if (scalar_one(s,"SELECT GET_LOCK(CONCAT('retrace-storage:',DATABASE()),10)")) {
        flock(s->lock,LOCK_UN); return RT_ERROR;
    }
    return RT_OK;
}
static void unlock_store(RtStore *s) {
    /* A failed release makes the connection unusable; close releases named locks. */
    if (mysql_query(s->db,"DO RELEASE_LOCK(CONCAT('retrace-storage:',DATABASE()))"))
        s->broken=1;
    flock(s->lock,LOCK_UN);
}
void rt_close(RtStore *s) {
    if (!s) return;
    if (s->db) mysql_close(s->db);
    if (s->images>=0) close(s->images);
    if (s->lock>=0) close(s->lock);
    if (s->root>=0) close(s->root);
    free(s);
}
static int write_all(int fd, const void *buffer, size_t size) {
    const unsigned char *p=buffer;
    while (size) {
        ssize_t n=write(fd,p,size);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) return -1;
        p+=n; size-=(size_t)n;
    }
    return 0;
}
int rt_open(RtStore **out, const RtConfig *c, char *err, size_t cap) {
    if (!out || !c) {
        if (err && cap) snprintf(err,cap,"Missing storage output or configuration");
        return RT_ERROR;
    }
    *out=NULL;
    RtStore *s=calloc(1,sizeof(*s));
    if (!s) { if (err && cap) snprintf(err,cap,"Out of memory"); return RT_ERROR; }
    s->root=s->images=s->lock=-1;
    if (!c->host || !c->user || !c->password || !c->database || !c->data_dir ||
        !c->port || c->port>65535) { errorf(s,"Missing/invalid storage configuration"); goto bad; }
    s->db=mysql_init(NULL);
    if (!s->db) { errorf(s,"mysql_init failed"); goto bad; }
    unsigned int timeout=10;
    my_bool reconnect=0;
    if (mysql_options(s->db,MYSQL_OPT_CONNECT_TIMEOUT,&timeout) ||
        mysql_options(s->db,MYSQL_OPT_READ_TIMEOUT,&timeout) ||
        mysql_options(s->db,MYSQL_OPT_WRITE_TIMEOUT,&timeout) ||
        mysql_options(s->db,MYSQL_OPT_RECONNECT,&reconnect)) {
        errorf(s,"Configure database connection options failed"); goto bad;
    }
    if (!mysql_real_connect(s->db,c->host,c->user,c->password,c->database,c->port,NULL,0)) {
        errorf(s,"Connect: %s",mysql_error(s->db)); goto bad;
    }
    if (mysql_set_character_set(s->db,"utf8mb4")) {
        errorf(s,"Set character set: %s",mysql_error(s->db)); goto bad;
    }
    if (query(s,"SET SESSION time_zone='+00:00'") ||
        query(s,"SET SESSION sql_mode='STRICT_ALL_TABLES,NO_ZERO_DATE,NO_ZERO_IN_DATE,ERROR_FOR_DIVISION_BY_ZERO'")) goto bad;
    if (mkdir(c->data_dir,0700) && errno!=EEXIST) {
        errorf(s,"Create data directory: %s",strerror(errno)); goto bad;
    }
    s->root=open(c->data_dir,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if (s->root<0) { errorf(s,"Open data directory: %s",strerror(errno)); goto bad; }
    s->lock=openat(s->root,".storage.lock",O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
    if (s->lock<0) { errorf(s,"Open storage lock: %s",strerror(errno)); goto bad; }
    if (flock(s->lock,LOCK_EX)) { errorf(s,"Lock directory failed"); goto bad; }
    /* A folder belongs to exactly one configured database, preventing recovery
       against another database from deleting the first database's images. */
    char identity[1024], existing[1024]={0};
    int len=snprintf(identity,sizeof(identity),"%s:%u/%s\n",c->host,c->port,c->database);
    if (len<0 || (size_t)len>=sizeof(identity)) { errorf(s,"Configuration too long"); goto bad; }
    int fd=openat(s->root,".database",O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
    if (fd<0) { errorf(s,"Open directory identity failed"); goto bad; }
    ssize_t n=read(fd,existing,sizeof(existing)-1);
    int invalid=n<0 || (n>0 && strcmp(existing,identity));
    if (n==0 && (write_all(fd,identity,(size_t)len) || fsync(fd))) invalid=1;
    close(fd);
    if (invalid) { errorf(s,"Data directory belongs to another database or identity is unreadable"); goto bad; }
    if (mkdirat(s->root,"snapshots",0700) && errno!=EEXIST) { errorf(s,"Create snapshots failed"); goto bad; }
    s->images=openat(s->root,"snapshots",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if (s->images<0 || fsync(s->root)) { errorf(s,"Open/sync snapshots failed"); goto bad; }
    flock(s->lock,LOCK_UN);
    *out=s; return RT_OK;
bad:
    if (err && cap) snprintf(err,cap,"%s",s->error);
    rt_close(s); return RT_ERROR;
}
static int item_valid(const char *item) {
    if (!item || !*item || strlen(item)>32 || *item<'a' || *item>'z') return 0;
    for (const char *p=item; *p; ++p)
        if (!((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='_')) return 0;
    return 1;
}
int rt_list(RtStore *s, RtRecord **out, size_t *count) {
    if (!s || !out || !count) return RT_ERROR;
    *out=NULL; *count=0;
    const char *sql="SELECT i.item,l.pos_x,l.pos_y,"
        "DATE_FORMAT(l.seen_at,'%Y-%m-%dT%H:%i:%s.%fZ'),l.snapshot,l.drawer_id,"
        "COALESCE(l.state,'uncertain') FROM items i LEFT JOIN last_seen l ON i.item=l.item ORDER BY i.item";
    if (query(s,sql)) return RT_ERROR;
    MYSQL_RES *res=mysql_store_result(s->db);
    if (!res) return errorf(s,"Read result: %s",mysql_error(s->db));
    size_t n=(size_t)mysql_num_rows(res);
    if (n>10000) { mysql_free_result(res); return errorf(s,"Catalog exceeds 10000 items"); }
    RtRecord *rows=calloc(n?n:1,sizeof(*rows));
    if (!rows) { mysql_free_result(res); return errorf(s,"Out of memory"); }
    MYSQL_ROW row; size_t i=0;
    while ((row=mysql_fetch_row(res))) {
        RtRecord *r=&rows[i++];
        snprintf(r->item,sizeof(r->item),"%s",row[0]);
        r->observed=row[3]!=NULL;
        r->has_position=row[1] && row[2];
        if (r->has_position) { r->pos_x=atoi(row[1]); r->pos_y=atoi(row[2]); }
        if (row[3]) snprintf(r->seen_at,sizeof(r->seen_at),"%s",row[3]);
        if (row[4]) snprintf(r->snapshot,sizeof(r->snapshot),"%s",row[4]);
        r->drawer_id=row[5]?atoi(row[5]):0;
        snprintf(r->state,sizeof(r->state),"%s",row[6]);
    }
    mysql_free_result(res);
    *out=rows; *count=i; return RT_OK;
}
int rt_get(RtStore *s, const char *item, RtRecord *out) {
    if (!s || !out) return RT_ERROR;
    memset(out,0,sizeof(*out));
    if (!item_valid(item)) return errorf(s,"Invalid item ID");
    RtRecord *rows; size_t n;
    if (rt_list(s,&rows,&n)) return RT_ERROR;
    int rc=RT_NOT_FOUND;
    for (size_t i=0;i<n;i++) if (!strcmp(rows[i].item,item)) { *out=rows[i]; rc=RT_OK; break; }
    free(rows); return rc;
}
static int time_valid(const char *text) {
    if (!text || strlen(text)!=27 || text[19]!='.' || text[26]!='Z') return 0;
    for (int i=20;i<26;i++) if (text[i]<'0'||text[i]>'9') return 0;
    struct tm tm={0}, check;
    char *end=strptime(text,"%Y-%m-%dT%H:%M:%S",&tm);
    if (end!=text+19 || tm.tm_year<70 || tm.tm_year>8099 || tm.tm_sec>59) return 0;
    time_t t=timegm(&tm);
    if (!gmtime_r(&t,&check)) return 0;
    char normalized[32];
    strftime(normalized,sizeof(normalized),"%Y-%m-%dT%H:%M:%S",&check);
    return strncmp(normalized,text,19)==0;
}
static int own_name(const char *name, int temporary) {
    const char *prefix=temporary?".rt_":"rt_";
    size_t start=strlen(prefix);
    if (strlen(name)!=start+32+4 || strncmp(name,prefix,start)) return 0;
    for (size_t i=start;i<start+32;i++)
        if (!((name[i]>='0'&&name[i]<='9')||(name[i]>='a'&&name[i]<='f'))) return 0;
    return !strcmp(name+start+32,temporary?".tmp":".jpg");
}
static int own_path(const char *path) {
    return path && !strncmp(path,"snapshots/",10) && own_name(path+10,0);
}
static int upsert(RtStore *s, const char *item, int x, int y, const char *timestamp,
                  const char *path, int drawer, const char *state) {
    const char *sql="INSERT INTO last_seen(item,pos_x,pos_y,seen_at,snapshot,drawer_id,state) "
        "VALUES(?,?,?,?,?,?,?) ON DUPLICATE KEY UPDATE pos_x=VALUES(pos_x),pos_y=VALUES(pos_y),"
        "seen_at=VALUES(seen_at),snapshot=VALUES(snapshot),drawer_id=VALUES(drawer_id),state=VALUES(state)";
    MYSQL_STMT *st=mysql_stmt_init(s->db);
    if (!st) return errorf(s,"Cannot allocate prepared statement");
    MYSQL_BIND p[7]; memset(p,0,sizeof(p));
    char time_sql[27]; memcpy(time_sql,timestamp,26); time_sql[10]=' '; time_sql[26]=0;
    const char *strings[4]={item,time_sql,path,state};
    int indices[4]={0,3,4,6}; unsigned long lengths[4];
    for (int i=0;i<4;i++) {
        lengths[i]=(unsigned long)strlen(strings[i]);
        p[indices[i]].buffer_type=MYSQL_TYPE_STRING;
        p[indices[i]].buffer=(void *)strings[i];
        p[indices[i]].buffer_length=lengths[i];
        p[indices[i]].length=&lengths[i];
    }
    p[1].buffer_type=p[2].buffer_type=p[5].buffer_type=MYSQL_TYPE_LONG;
    p[1].buffer=&x; p[2].buffer=&y; p[5].buffer=&drawer;
    my_bool null_drawer=drawer==0; p[5].is_null=&null_drawer;
    int rc=RT_OK;
    if (mysql_stmt_prepare(st,sql,(unsigned long)strlen(sql)) ||
        mysql_stmt_bind_param(st,p) || mysql_stmt_execute(st))
        rc=errorf(s,"Save record: %s",mysql_stmt_error(st));
    mysql_stmt_close(st); return rc;
}
#ifdef RETRACE_TESTING
static void crash_at(const char *point) {
    const char *p=getenv("RETRACE_TEST_CRASH");
    if (p && !strcmp(p,point)) _exit(90);
}
#else
static void crash_at(const char *point) { (void)point; }
#endif
static int read_image(RtStore *s, const char *path, unsigned char **data, size_t *size);
int rt_save(RtStore *s, const char *item, int x, int y, const char *stamp,
            int drawer, const char *state, const char *snapshot_path) {
    if (!s) return RT_ERROR;
    s->error[0]=0;
    if (!item_valid(item) || !time_valid(stamp) || x<0 || y<0 || drawer<0 || drawer>6 ||
        !state || (strcmp(state,"visible") && strcmp(state,"occluded") && strcmp(state,"uncertain")) ||
        !own_path(snapshot_path))
        return errorf(s,"Invalid observation or managed snapshot path");
    if (lock_store(s)) return RT_ERROR;
    RtRecord old;
    unsigned char *jpeg=NULL;
    size_t jpeg_size=0;
    int transaction=0;
    int rc=read_image(s,snapshot_path,&jpeg,&jpeg_size);
    if (rc!=RT_OK) goto done;
    int width,height;
    if (rt_check_jpeg(jpeg,jpeg_size,&width,&height)) {
        rc=errorf(s,"Invalid/truncated or oversized JPEG"); goto done;
    }
    free(jpeg); jpeg=NULL;
    if (x>=width || y>=height) { rc=errorf(s,"Coordinates outside full camera frame"); goto done; }
    rc=rt_get(s,item,&old);
    if (rc!=RT_OK) goto done;
    if (old.observed && strcmp(stamp,old.seen_at)<=0) { rc=RT_STALE; goto done; }
    /* The caller has already atomically published this managed snapshot. */
    crash_at("after_publish");
    if ((rc=query(s,"START TRANSACTION"))) goto done;
    transaction=1;
    if ((rc=upsert(s,item,x,y,stamp,snapshot_path,drawer,state))) goto done;
    crash_at("before_commit");
    if (mysql_commit(s->db)) {
        errorf(s,"Commit result unknown; keep images, reopen connection and run recover: %s",mysql_error(s->db));
        s->broken=1; rc=RT_COMMIT_UNKNOWN; transaction=0; goto done;
    }
    transaction=0;
    crash_at("after_commit");
    /* Never delete a legacy or user-supplied path. Recovery follows the same rule. */
    if (own_path(old.snapshot) && strcmp(old.snapshot,snapshot_path) &&
        unlinkat(s->images,old.snapshot+10,0) && errno!=ENOENT) {
        errorf(s,"Saved; old image cleanup pending: %s",strerror(errno)); rc=RT_CLEANUP_PENDING;
    }
    if (fsync(s->images)) { errorf(s,"Saved; directory sync pending"); rc=RT_CLEANUP_PENDING; }
done:
    free(jpeg);
    if (transaction && mysql_rollback(s->db)) s->broken=1;
    unlock_store(s);
    return rc;
}
static int read_image(RtStore *s, const char *path, unsigned char **data, size_t *size) {
    if (!own_path(path)) return errorf(s,"No managed snapshot for this record");
    int fd=openat(s->images,path+10,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if (fd<0) return errorf(s,"Open snapshot: %s",strerror(errno));
    struct stat stat;
    if (fstat(fd,&stat) || !S_ISREG(stat.st_mode) || stat.st_size<=0 || stat.st_size>32*1024*1024) {
        close(fd); return errorf(s,"Invalid snapshot file");
    }
    unsigned char *buffer=malloc((size_t)stat.st_size);
    if (!buffer) { close(fd); return errorf(s,"Out of memory"); }
    size_t used=0;
    while (used<(size_t)stat.st_size) {
        ssize_t n=read(fd,buffer+used,(size_t)stat.st_size-used);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) { close(fd); free(buffer); return errorf(s,"Read snapshot failed"); }
        used+=(size_t)n;
    }
    close(fd); *data=buffer; *size=used; return RT_OK;
}
int rt_load_snapshot(RtStore *s, const char *item, unsigned char **data, size_t *size) {
    if (!s || !data || !size) return RT_ERROR;
    *data=NULL; *size=0;
    if (lock_store(s)) return RT_ERROR;
    RtRecord row;
    int rc=rt_get(s,item,&row);
    if (rc==RT_OK) rc=row.observed && *row.snapshot ? read_image(s,row.snapshot,data,size) : RT_NOT_FOUND;
    unlock_store(s); return rc;
}
int rt_recover(RtStore *s, size_t *removed) {
    if (!s || !removed) return RT_ERROR;
    *removed=0;
    if (lock_store(s)) return RT_ERROR;
    RtRecord *rows=NULL; size_t count=0; DIR *directory=NULL;
    int rc=rt_list(s,&rows,&count);
    if (rc) goto done;
    /* Check current references BEFORE removing any files. */
    for (size_t i=0;i<count;i++) if (own_path(rows[i].snapshot)) {
        struct stat st;
        if (fstatat(s->images,rows[i].snapshot+10,&st,AT_SYMLINK_NOFOLLOW) || !S_ISREG(st.st_mode)) {
            rc=errorf(s,"Referenced snapshot missing/not regular: %s",rows[i].snapshot); goto done;
        }
    }
    int scan=openat(s->images,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if (scan<0) { rc=errorf(s,"Cannot scan snapshots"); goto done; }
    directory=fdopendir(scan);
    if (!directory) { close(scan); rc=errorf(s,"Cannot scan snapshots"); goto done; }
    struct dirent *entry;
    errno=0;
    while ((entry=readdir(directory))) {
        const char *name=entry->d_name;
        if (!own_name(name,0) && !own_name(name,1)) continue;
        int referenced=0;
        for (size_t i=0;i<count;i++)
            if (own_path(rows[i].snapshot) && !strcmp(rows[i].snapshot+10,name)) referenced=1;
        if (!referenced) {
            if (unlinkat(s->images,name,0)) { rc=errorf(s,"Orphan cleanup failed: %s",strerror(errno)); goto done; }
            ++*removed;
        }
        errno=0;
    }
    if (errno) rc=errorf(s,"Snapshot scan failed");
    if (fsync(s->images)) rc=errorf(s,"Snapshot directory sync failed");
done:
    if (directory) closedir(directory);
    free(rows); unlock_store(s); return rc;
}
