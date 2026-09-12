/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 * Included by horizon.c with the server's wire and object definitions. */
#include <time.h>

static long long horizon_registry_now(void)
{
    return (long long)time(NULL) * 10000000 + HORIZON_REG_TICKS_1601_TO_1970;
}

static void horizon_registry_save_value(FILE *file, const struct horizon_reg_value *value)
{
    unsigned int i;
    if (!value->namelen) fputs("@", file);
    else
    {
        fputc('"', file);
        for (i = 0; i < value->namelen / 2; i++) fputc(value->name[i] < 128 ? value->name[i] : '?', file);
        fputc('"', file);
    }
    if (value->type == HORIZON_REG_SZ || value->type == HORIZON_REG_EXPAND_SZ)
    {
        fputs("=\"", file);
        for (i = 0; i + 1 < value->len / 2; i++)
        {
            unsigned short c = ((const unsigned short *)value->data)[i];
            if (c == '"' || c == '\\') fputc('\\', file);
            fputc(c < 128 ? c : '?', file);
        }
        fputs("\"\n", file);
    }
    else
    {
        fprintf(file, "=hex(%x):", value->type);
        for (i = 0; i < value->len; i++) fprintf(file, "%02x%s", value->data[i], i + 1 < value->len ? "," : "");
        fputc('\n', file);
    }
}

static void horizon_registry_save_tree(FILE *file, const struct horizon_reg_key *key,
                                       const char *prefix, int write_header)
{
    char path[1024], child_prefix[1024];
    unsigned int i, j, len = 0;
    if (write_header)
    {
        if (prefix && *prefix) fprintf(file, "\n[%s]\n", prefix);
        for (i = 0; i < key->value_count; i++) horizon_registry_save_value(file, &key->values[i]);
    }
    for (i = 0; i < key->subkey_count; i++)
    {
        const struct horizon_reg_key *child = key->subkeys[i];
        len = 0;
        if (prefix) { len = strlen(prefix); memcpy(path, prefix, len); if (len) path[len++] = '\\'; }
        for (j = 0; j < child->namelen / 2 && len + 1 < sizeof(path); j++) path[len++] = child->name[j] < 128 ? child->name[j] : '?';
        path[len] = 0;
        snprintf(child_prefix, sizeof(child_prefix), "%s", path);
        horizon_registry_save_tree(file, child, child_prefix, 1);
    }
}

/* Save the two prefix hives after a mutating request. This is intentionally
 * best-effort: a read-only SD card must not make RegSetValue fail. */
static void horizon_registry_save(void)
{
    struct horizon_reg_key *machine, *user;
    unsigned int index;
    FILE *file;
    machine = horizon_reg_find_subkey(horizon_registry.root, (const unsigned short[]){'M','a','c','h','i','n','e'}, 12, &index);
    user = horizon_reg_find_subkey(horizon_registry.root, (const unsigned short[]){'U','s','e','r','\\','S','-','1','-','5','-','2','1','-','0','-','0','-','0','-','1','0','0','0'}, 46, &index);
    if (machine && (file = fopen("sdmc:/switch/wine/system.reg.tmp", "wb")))
    {
        fputs("WINE REGISTRY Version 2\n", file); horizon_registry_save_tree(file, machine, "", 0); fclose(file);
        rename("sdmc:/switch/wine/system.reg.tmp", "sdmc:/switch/wine/system.reg");
    }
    if (user && (file = fopen("sdmc:/switch/wine/user.reg.tmp", "wb")))
    {
        fputs("WINE REGISTRY Version 2\n", file); horizon_registry_save_tree(file, user, "", 0); fclose(file);
        rename("sdmc:/switch/wine/user.reg.tmp", "sdmc:/switch/wine/user.reg");
    }
}

static void horizon_registry_load_file(struct horizon_reg_key *base, const char *path)
{
    FILE *file;
    long length;
    char *buffer;
    unsigned int errors = 0;
    if (!(file = fopen(path, "rb"))) return;
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 || length > 16 * 1024 * 1024 ||
        fseek(file, 0, SEEK_SET) || !(buffer = malloc((size_t)length))) { fclose(file); return; }
    if (fread(buffer, 1, (size_t)length, file) == (size_t)length)
        horizon_reg_load(&horizon_registry, base, buffer, (size_t)length, &errors);
    free(buffer);
    fclose(file);
}

/* Notifications retain event objects, not handles which can be closed/reused.
 * The caller holds horizon_server_objects_mutex; pending waits are woken. */
static void horizon_registry_signal( void *event )
{
    struct horizon_server_object *object = event;
    object->signaled = 1;
    horizon_server_signal_changed_locked();
    if (!--object->refs) horizon_server_free_object( object );
}

static unsigned int horizon_registry_init(void)
{
    struct horizon_reg_key *machine, *user;
    if (horizon_registry.root) return 0;
    if (!horizon_reg_init( &horizon_registry, horizon_registry_now, horizon_registry_signal ))
        return HORIZON_REG_NO_MEMORY;
    machine = horizon_reg_create_ascii( &horizon_registry, horizon_registry.root, "Machine" );
    user = horizon_reg_create_ascii( &horizon_registry, horizon_registry.root, "User\\S-1-5-21-0-0-0-1000" );
    if (!machine || !user ||
        !horizon_reg_create_ascii( &horizon_registry, machine, "Software\\Classes" ) ||
        !horizon_reg_create_ascii( &horizon_registry, machine, "System\\CurrentControlSet" ) ||
        !horizon_reg_create_ascii( &horizon_registry, user, "Software" ) ||
        !horizon_reg_create_ascii( &horizon_registry, horizon_registry.root, "User\\.Default" ))
    {
        horizon_reg_release( &horizon_registry, horizon_registry.root );
        horizon_registry.root = NULL;
        return HORIZON_REG_NO_MEMORY;
    }
    {
        static const char machine_seed[] =
            "WINE REGISTRY Version 2\n"
            "[Software\\\\Classes\\\\CLSID\\\\{BCDE0395-E52F-467C-8E3D-C4579291692E}\\\\InprocServer32]\n"
            "@=\"mmdevapi.dll\"\n"
            "\"ThreadingModel\"=\"Both\"\n";
        static const char user_seed[] =
            "WINE REGISTRY Version 2\n[Software\\\\Wine\\\\Drivers]\n\"Audio\"=\"nxaudio\"\n";
        unsigned int errors = 0;
        if (horizon_reg_load( &horizon_registry, machine, machine_seed, sizeof(machine_seed) - 1, &errors ) ||
            horizon_reg_load( &horizon_registry, user, user_seed, sizeof(user_seed) - 1, &errors ) || errors)
        {
            horizon_reg_release( &horizon_registry, horizon_registry.root );
            horizon_registry.root = NULL;
            return HORIZON_REG_NO_MEMORY;
        }
    }
    horizon_registry_load_file(machine, "sdmc:/switch/wine/system.reg");
    horizon_registry_load_file(user, "sdmc:/switch/wine/user.reg");
    return 0;
}

/* The in-process server has a single local user and no impersonation tokens.
 * This identity also names HKCU; do not accept arbitrary token handles. */
static int horizon_server_handle_registry_user( struct horizon_server_connection *connection,
                                                const unsigned char *message )
{
    const struct { struct horizon_server_request_header header; unsigned int handle, which_sid; } *req = (const void *)message;
    struct { struct horizon_server_reply_header header; unsigned int sid_len, pad; } reply = {0};
    static const struct { unsigned char revision, count, authority[6]; unsigned int sub[5]; } sid =
        {1, 5, {0,0,0,0,0,5}, {21,0,0,0,1000}};
    if (req->handle != 0xfffffffau && req->handle != 0xfffffffcu)
        reply.header.error = HORIZON_STATUS_INVALID_HANDLE;
    else if (req->which_sid != 1) reply.header.error = HORIZON_STATUS_NOT_IMPLEMENTED;
    else
    {
        reply.sid_len = sizeof(sid);
        if (req->header.reply_size < sizeof(sid)) reply.header.error = HORIZON_STATUS_BUFFER_TOO_SMALL;
        else reply.header.reply_size = sizeof(sid);
    }
    return horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply),
                                        &sid, reply.header.reply_size );
}

static unsigned int horizon_registry_key( unsigned int handle, struct horizon_reg_key **key )
{
    struct horizon_server_handle_entry *entry = horizon_server_find_handle_locked( handle );
    *key = NULL;
    if (!entry || !entry->object || entry->object->type != HORIZON_SERVER_OBJECT_REG_KEY)
        return HORIZON_STATUS_INVALID_HANDLE;
    *key = entry->object->reg_key;
    if ((*key)->flags & HORIZON_REG_FLAG_DELETED) return HORIZON_REG_KEY_DELETED;
    return 0;
}

static int horizon_server_handle_registry( struct horizon_server_connection *connection,
                                           const unsigned char *message,
                                           const unsigned char *data, unsigned int data_size )
{
    const struct horizon_server_request_header *header = (const void *)message;
    union
    {
        struct horizon_server_reply_header header;
        struct horizon_create_key_reply create;
        struct horizon_enum_key_reply key;
        struct horizon_get_key_value_reply value;
        struct horizon_enum_key_value_reply enum_value;
    } reply;
    struct horizon_reg_key *key = NULL;
    struct horizon_server_handle_entry *entry;
    unsigned char *out = NULL;
    unsigned int status, size = 0, max = header->reply_size;
    memset( &reply, 0, sizeof(reply) );
    /* Bound peer-controlled allocations; regular values can be fetched in
     * smaller buffers with the full required length returned in the reply. */
    if (max > 16 * 1024 * 1024) max = 16 * 1024 * 1024;
    if (max && !(out = malloc(max)))
        return horizon_server_write_status( connection->reply_fd, HORIZON_REG_NO_MEMORY );
    pthread_mutex_lock( &horizon_server_objects_mutex );
    if ((status = horizon_registry_init())) goto done;
    if (header->req == HORIZON_REQ_CREATE_KEY || header->req == HORIZON_REQ_OPEN_KEY)
    {
        struct horizon_reg_key *base = NULL;
        unsigned int parent, attributes, options = 0, classlen = 0;
        const unsigned short *name, *class = NULL;
        unsigned int len;
        if (header->req == HORIZON_REQ_CREATE_KEY)
        {
            const struct horizon_create_key_request *req = (const void *)message;
            const struct horizon_object_attributes *attr = (const void *)data;
            struct horizon_object_name parsed;
            unsigned int offset;
            if (data_size < sizeof(*attr)) { status = HORIZON_REG_INVALID_PARAMETER; goto done; }
            if ((status = horizon_server_parse_object_attributes( data, data_size, &parsed )) ||
                (status = horizon_server_object_attributes_size( data, data_size, &offset ))) goto done;
            parent = attr->rootdir;
            attributes = attr->attributes;
            name = (const void *)parsed.name;
            len = parsed.name_len;
            options = req->options;
            class = (const void *)(data + offset);
            classlen = data_size - offset;
        }
        else
        {
            const struct horizon_open_key_request *req = (const void *)message;
            parent = req->parent;
            attributes = req->attributes;
            name = (const void *)data;
            len = data_size;
        }
        if ((len | classlen) & 1) { status = HORIZON_REG_INVALID_PARAMETER; goto done; }
        if (parent && (status = horizon_registry_key( parent, &base ))) goto done;
        if (header->req == HORIZON_REQ_CREATE_KEY)
            status = horizon_reg_create( &horizon_registry, base, name, len, attributes,
                                         options, class, classlen, &key );
        else status = horizon_reg_open( &horizon_registry, base, name, len, attributes, &key );
        if (!key) goto done;
        if (!(entry = horizon_server_create_handle_locked( HORIZON_SERVER_OBJECT_REG_KEY )))
        {
            horizon_reg_release( &horizon_registry, key );
            status = HORIZON_REG_NO_MEMORY;
            goto done;
        }
        entry->object->reg_key = key;
        reply.create.hkey = entry->handle;
        goto done;
    }
    /* All remaining registry requests begin with hkey at offset 12. */
    if ((status = horizon_registry_key( ((const struct horizon_delete_key_request *)message)->hkey, &key )))
        goto done;
    switch (header->req)
    {
    case HORIZON_REQ_DELETE_KEY:
        status = horizon_reg_delete( &horizon_registry, key );
        if (!status) horizon_registry_save();
        break;
    case HORIZON_REQ_RENAME_KEY:
        status = horizon_reg_rename( &horizon_registry, key, (const void *)data, data_size );
        if (!status) horizon_registry_save();
        break;
    case HORIZON_REQ_ENUM_KEY:
    {
        const struct horizon_enum_key_request *req = (const void *)message;
        struct horizon_reg_key_info info;
        status = horizon_reg_enum_key( key, req->index, req->info_class, &info, out, max, &size );
        reply.key.subkeys = info.subkeys;
        reply.key.max_subkey = info.max_subkey;
        reply.key.max_class = info.max_class;
        reply.key.values = info.values;
        reply.key.max_value = info.max_value;
        reply.key.max_data = info.max_data;
        reply.key.modif = info.modif;
        reply.key.total = info.total;
        reply.key.namelen = info.namelen;
        break;
    }
    case HORIZON_REQ_SET_KEY_VALUE:
    {
        const struct horizon_set_key_value_request *req = (const void *)message;
        if (req->namelen > data_size || (req->namelen & 1)) status = HORIZON_REG_INVALID_PARAMETER;
        else status = horizon_reg_set_value( &horizon_registry, key, (const void *)data, req->namelen,
                                             req->type, data + req->namelen, data_size - req->namelen );
        if (!status) horizon_registry_save();
        break;
    }
    case HORIZON_REQ_GET_KEY_VALUE:
        status = horizon_reg_get_value( key, (const void *)data, data_size, &reply.value.type,
                                        &reply.value.total, out, max, &size );
        break;
    case HORIZON_REQ_ENUM_KEY_VALUE:
    {
        const struct horizon_enum_key_value_request *req = (const void *)message;
        status = horizon_reg_enum_value( key, req->index, req->info_class, &reply.enum_value.type,
                                         &reply.enum_value.total, &reply.enum_value.namelen, out, max, &size );
        break;
    }
    case HORIZON_REQ_DELETE_KEY_VALUE:
        status = horizon_reg_delete_value( &horizon_registry, key, (const void *)data, data_size );
        if (!status) horizon_registry_save();
        break;
    case HORIZON_REQ_SET_REGISTRY_NOTIFICATION:
    {
        const struct horizon_set_registry_notification_request *req = (const void *)message;
        entry = horizon_server_find_handle_locked( req->event );
        if (!entry || entry->object->type != HORIZON_SERVER_OBJECT_EVENT)
            status = HORIZON_STATUS_INVALID_HANDLE;
        else
        {
            status = horizon_reg_notify( key, req->hkey, entry->object, req->subtree, req->filter );
            if (status == HORIZON_REG_PENDING)
            {
                entry->object->refs++;
                entry->object->signaled = 0;
            }
        }
        break;
    }
    default: status = HORIZON_REG_INVALID_PARAMETER; break;
    }
done:
    pthread_mutex_unlock( &horizon_server_objects_mutex );
    reply.header.error = status;
    reply.header.reply_size = size;
    status = horizon_server_write_reply( connection->reply_fd, &reply, sizeof(reply), out, size );
    free(out);
    return status;
}
