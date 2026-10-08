#define _POSIX_C_SOURCE 200809L
#include "db_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

static const char *env(const char *key, const char *fallback) {
    const char *v=getenv(key); return v?v:fallback;
}
static int integer(const char *text, int *out) {
    char *end; errno=0;
    long n=strtol(text,&end,10);
    if (errno || !*text || *end || n<0 || n>INT_MAX) return -1;
    *out=(int)n; return 0;
}
static void string(const char *s) {
    if (!*s) { fputs("null",stdout); return; }
    putchar('"');
    for (const unsigned char *p=(const unsigned char *)s;*p;p++) {
        if (*p=='"'||*p=='\\') printf("\\%c",*p);
        else if (*p<32) printf("\\u%04x",*p);
        else putchar(*p);
    }
    putchar('"');
}
static void print_record(const RtRecord *r) {
    fputs("{\"item\":",stdout); string(r->item);
    if (r->has_position) printf(",\"pos_x\":%d,\"pos_y\":%d",r->pos_x,r->pos_y);
    else fputs(",\"pos_x\":null,\"pos_y\":null",stdout);
    fputs(",\"seen_at\":",stdout); string(r->seen_at);
    fputs(",\"snapshot\":",stdout); string(r->snapshot);
    if (r->drawer_id) printf(",\"drawer_id\":%d",r->drawer_id);
    else fputs(",\"drawer_id\":null",stdout);
    fputs(",\"state\":",stdout); string(r->state); putchar('}');
}
int main(int argc, char **argv) {
    if (argc<2) goto usage;
    const char *command=argv[1];
    if (!((!strcmp(command,"list") && argc==2) ||
          (!strcmp(command,"get") && argc==3) ||
          (!strcmp(command,"save") && argc==9) ||
          (!strcmp(command,"image") && argc==3) ||
          (!strcmp(command,"recover") && argc==2))) goto usage;
    int port;
    if (integer(env("RETRACE_DB_PORT","3306"),&port) || port<1 || port>65535) {
        fputs("Invalid RETRACE_DB_PORT\n",stderr); return 1;
    }
    RtConfig config={env("RETRACE_DB_HOST","127.0.0.1"),env("RETRACE_DB_USER","retrace"),
        getenv("RETRACE_DB_PASSWORD"),env("RETRACE_DB_NAME","retrace"),env("RETRACE_DATA_DIR","./data"),(unsigned)port};
    RtStore *store; char error[512];
    if (rt_open(&store,&config,error,sizeof(error))) { fprintf(stderr,"%s\n",error); return 1; }
    int rc=RT_OK;
    if (!strcmp(command,"list")) {
        RtRecord *rows; size_t n;
        rc=rt_list(store,&rows,&n);
        if (!rc) { fputs("{\"items\":[",stdout); for (size_t i=0;i<n;i++) { if(i) putchar(','); print_record(&rows[i]); } puts("]}"); free(rows); }
    } else if (!strcmp(command,"get")) {
        RtRecord row; rc=rt_get(store,argv[2],&row);
        if (!rc) { print_record(&row); putchar('\n'); }
    } else if (!strcmp(command,"image")) {
        unsigned char *data; size_t size;
        rc=rt_load_snapshot(store,argv[2],&data,&size);
        if (!rc) { if (fwrite(data,1,size,stdout)!=size || fflush(stdout)) rc=RT_ERROR; free(data); }
    } else if (!strcmp(command,"recover")) {
        size_t removed; rc=rt_recover(store,&removed);
        if (!rc) printf("{\"removed\":%zu}\n",removed);
    } else {
        int x,y,drawer;
        if (integer(argv[4],&x)||integer(argv[5],&y)||integer(argv[7],&drawer)) {
            fputs("Invalid numeric argument\n",stderr); rc=RT_ERROR;
        } else {
            rc=rt_save(store,argv[2],x,y,argv[6],drawer,argv[8],argv[3]);
            if (rc==RT_OK || rc==RT_CLEANUP_PENDING) puts("{\"saved\":true}");
        }
    }
    if (rc!=RT_OK) {
        if (rc==RT_NOT_FOUND) fputs("Item or snapshot not found\n",stderr);
        else if (rc==RT_STALE) fputs("Ignored: timestamp is not newer\n",stderr);
        else fprintf(stderr,"%s\n",rt_error(store));
    }
    rt_close(store);
    return rc==RT_OK?0:rc==RT_NOT_FOUND?2:rc==RT_STALE?3:rc==RT_CLEANUP_PENDING?4:rc==RT_COMMIT_UNKNOWN?5:1;
usage:
    fputs("Usage: retrace-db list | get ITEM | image ITEM | recover\n"
          "       retrace-db save ITEM snapshots/rt_<32-hex>.jpg X Y YYYY-MM-DDTHH:MM:SS.ffffffZ DRAWER STATE\n"
          "DRAWER: 0 (none) or 1..6. STATE: visible/occluded/uncertain.\n"
          "Configuration: RETRACE_DB_HOST/PORT/NAME/USER/PASSWORD, RETRACE_DATA_DIR\n",stderr);
    return 1;
}
