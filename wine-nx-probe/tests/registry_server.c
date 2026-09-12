/* Exercise the registry wire adapter, including actual protocol layouts. */
#include <assert.h>
#include <stddef.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wine/server_protocol.h"
#include "../../dlls/ntdll/unix/horizon_registry.h"
struct horizon_server_request_header { int req; unsigned int request_size, reply_size; };
struct horizon_server_reply_header { unsigned int error, reply_size; };
#include "../../dlls/ntdll/unix/horizon_registry_wire.h"
#define HORIZON_REQ_CREATE_KEY REQ_create_key
#define HORIZON_REQ_OPEN_KEY REQ_open_key
#define HORIZON_REQ_DELETE_KEY REQ_delete_key
#define HORIZON_REQ_ENUM_KEY REQ_enum_key
#define HORIZON_REQ_SET_KEY_VALUE REQ_set_key_value
#define HORIZON_REQ_GET_KEY_VALUE REQ_get_key_value
#define HORIZON_REQ_ENUM_KEY_VALUE REQ_enum_key_value
#define HORIZON_REQ_DELETE_KEY_VALUE REQ_delete_key_value
#define HORIZON_REQ_SET_REGISTRY_NOTIFICATION REQ_set_registry_notification
#define HORIZON_REQ_RENAME_KEY REQ_rename_key
#define HORIZON_STATUS_INVALID_HANDLE 0xc0000008u
#define HORIZON_STATUS_BUFFER_TOO_SMALL 0xc0000023u
#define HORIZON_STATUS_NOT_IMPLEMENTED 0xc0000002u
#define HORIZON_SERVER_OBJECT_REG_KEY 1
#define HORIZON_SERVER_OBJECT_EVENT 2
struct horizon_server_connection { int reply_fd; };
struct horizon_server_object { int type; unsigned int refs, signaled; struct horizon_reg_key *reg_key; };
struct horizon_server_handle_entry { unsigned int handle; struct horizon_server_object *object; };
struct horizon_object_attributes { unsigned int rootdir, attributes, sd_len, name_len; };
struct horizon_object_name { unsigned int rootdir, name_len; const unsigned char *name; };
static struct horizon_reg horizon_registry;
static pthread_mutex_t horizon_server_objects_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct horizon_server_handle_entry handles[64];
static unsigned int handle_count;
static unsigned char last_reply[64], payload[2048];
static unsigned int payload_size;
static struct horizon_server_handle_entry *horizon_server_find_handle_locked(unsigned int h)
{ return h && h <= handle_count && handles[h-1].object ? &handles[h-1] : NULL; }
static struct horizon_server_handle_entry *horizon_server_create_handle_locked(int type)
{
    struct horizon_server_handle_entry *e = &handles[handle_count++];
    assert(handle_count <= 64);
    e->handle = handle_count; e->object = calloc(1, sizeof(*e->object));
    e->object->type = type; e->object->refs = 1; return e;
}
static void horizon_server_free_object(struct horizon_server_object *o)
{ if(o->reg_key) horizon_reg_release(&horizon_registry, o->reg_key); free(o); }
static int horizon_server_write_reply(int fd, const void *r, unsigned int size, const void *data, unsigned int len)
{
    (void)fd; assert(size <= sizeof(last_reply) && len <= sizeof(payload));
    memset(last_reply,0,sizeof(last_reply)); memcpy(last_reply,r,size);
    if(len) memcpy(payload,data,len); payload_size = len; return 0;
}
static int horizon_server_write_status(int fd, unsigned int status)
{ struct horizon_server_reply_header r = {status,0}; return horizon_server_write_reply(fd,&r,sizeof(r),NULL,0); }
static unsigned int horizon_server_parse_object_attributes(const unsigned char *data, unsigned int size, struct horizon_object_name *n)
{
    const struct horizon_object_attributes *a = (const void *)data;
    if(size < sizeof(*a) || a->sd_len > size-sizeof(*a) || a->name_len > size-sizeof(*a)-a->sd_len) return HORIZON_REG_INVALID_PARAMETER;
    n->rootdir=a->rootdir; n->name=data+sizeof(*a)+a->sd_len; n->name_len=a->name_len; return 0;
}
static unsigned int horizon_server_object_attributes_size(const unsigned char *data, unsigned int size, unsigned int *offset)
{
    const struct horizon_object_attributes *a=(const void *)data;
    *offset=(sizeof(*a)+a->sd_len+a->name_len+3)&~3u;
    return *offset > size ? HORIZON_REG_INVALID_PARAMETER : 0;
}
#include "../../dlls/ntdll/unix/horizon_registry_server.h"
#define CHECK_LAYOUT(n) _Static_assert(sizeof(struct horizon_##n) == sizeof(struct n), #n)
CHECK_LAYOUT(create_key_request); CHECK_LAYOUT(create_key_reply);
CHECK_LAYOUT(open_key_request); CHECK_LAYOUT(open_key_reply);
CHECK_LAYOUT(enum_key_request); CHECK_LAYOUT(enum_key_reply);
CHECK_LAYOUT(get_key_value_reply); CHECK_LAYOUT(enum_key_value_reply);
CHECK_LAYOUT(set_registry_notification_request);
_Static_assert(offsetof(struct horizon_enum_key_reply, modif) == offsetof(struct enum_key_reply, modif), "timestamp offset");
static struct horizon_server_connection connection;
static unsigned int status(void) { return ((struct horizon_server_reply_header *)last_reply)->error; }
static unsigned int wide(unsigned short *out, const char *in)
{ unsigned int n=0; while(*in) out[n++]=(unsigned char)*in++; return n*2; }
static unsigned int open_path(unsigned int root, const char *path)
{
    unsigned short name[256];
    struct horizon_open_key_request req = {{REQ_open_key,0,0},root,0,0};
    horizon_server_handle_registry(&connection,(void *)&req,(void *)name,wide(name,path));
    assert(!status()); return ((struct horizon_open_key_reply *)last_reply)->hkey;
}
int main(void)
{
    unsigned int key, user, i;
    unsigned short name[256];
    struct horizon_get_key_value_request get = {{REQ_get_key_value,0,sizeof(payload)},0};
    struct horizon_set_key_value_request set = {{REQ_set_key_value,0,0},0,4,0};
    struct horizon_enum_key_request query = {{REQ_enum_key,0,2},0,-1,3};
    struct horizon_set_registry_notification_request notify = {{REQ_set_registry_notification,0,0},0,0,0,4,{0}};
    struct horizon_server_handle_entry *event;
    struct {struct horizon_server_request_header header; unsigned int handle, which;} token = {{REQ_get_token_sid,0,0},0xfffffffa,1};
    horizon_server_handle_registry_user(&connection,(void *)&token);
    assert(status()==HORIZON_STATUS_BUFFER_TOO_SMALL);
    token.header.reply_size=28; horizon_server_handle_registry_user(&connection,(void *)&token);
    assert(!status() && payload_size==28 && payload[0]==1 && payload[1]==5);
    key = open_path(0,"\\Registry\\Machine\\Software\\Classes\\CLSID\\{BCDE0395-E52F-467C-8E3D-C4579291692E}\\InprocServer32");
    get.hkey=key; horizon_server_handle_registry(&connection,(void *)&get,NULL,0);
    assert(!status() && payload_size==26 && !memcmp(payload,"m\0m\0d\0",6));
    user=open_path(0,"\\Registry\\User\\S-1-5-21-0-0-0-1000\\Software\\Wine\\Drivers");
    get.hkey=user; horizon_server_handle_registry(&connection,(void *)&get,(void *)name,wide(name,"Audio"));
    assert(!status() && payload_size==16 && !memcmp(payload,"n\0x\0",4));
    query.hkey=key; horizon_server_handle_registry(&connection,(void *)&query,NULL,0);
    assert(!status() && payload_size==2 && ((struct horizon_enum_key_reply *)last_reply)->total>2);
    event=horizon_server_create_handle_locked(HORIZON_SERVER_OBJECT_EVENT); event->object->signaled=1;
    notify.hkey=user; notify.event=event->handle;
    horizon_server_handle_registry(&connection,(void *)&notify,NULL,0);
    assert(status()==HORIZON_REG_PENDING && event->object->refs==2 && !event->object->signaled);
    set.hkey=user; set.namelen=wide(name,"Test"); memcpy((char *)name+set.namelen,"\1\0\0\0",4);
    horizon_server_handle_registry(&connection,(void *)&set,(void *)name,set.namelen+4);
    assert(!status() && event->object->refs==1 && event->object->signaled);
    set.namelen=100; horizon_server_handle_registry(&connection,(void *)&set,(void *)name,4);
    assert(status()==HORIZON_REG_INVALID_PARAMETER);
    for(i=0;i<handle_count;i++) horizon_server_free_object(handles[i].object);
    horizon_reg_release(&horizon_registry,horizon_registry.root);
    puts("Registry server: protocol layouts, HKCU identity, COM/audio seeds, truncated replies and notifications passed");
    return 0;
}
