/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#ifndef WINE_HORIZON_REGISTRY_H
#define WINE_HORIZON_REGISTRY_H

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The registry of the in-process Horizon server, following server/registry.c:
 * keys under \Registry with sorted subkeys and values, symbolic links, change
 * notifications, and parsing the "WINE REGISTRY Version 2" text of system.reg
 * and user.reg. horizon_registry_server.h loads those files at the first
 * request and writes them back after each change. As in Wine, creating a key
 * creates only the last element of its path. WoW64 redirection needs
 * Wow6432Node keys, which the server does not create, so the tree has a single
 * view. Names are UTF-16 with lengths in bytes and compare without ASCII case
 * (Wine folds all of Unicode). Callers hold the server lock; nothing here
 * performs I/O, so the host tests include this header directly. */

#define HORIZON_REG_SUCCESS                0x00000000u
#define HORIZON_REG_NAME_EXISTS            0x40000000u
#define HORIZON_REG_PENDING                0x00000103u
#define HORIZON_REG_NO_MORE_ENTRIES        0x8000001au
#define HORIZON_REG_INVALID_PARAMETER      0xc000000du
#define HORIZON_REG_NO_MEMORY              0xc0000017u
#define HORIZON_REG_ACCESS_DENIED          0xc0000022u
#define HORIZON_REG_NAME_NOT_FOUND         0xc0000034u
#define HORIZON_REG_NAME_COLLISION         0xc0000035u
#define HORIZON_REG_PATH_SYNTAX_BAD        0xc000003bu
#define HORIZON_REG_NAME_TOO_LONG          0xc0000106u
#define HORIZON_REG_CANNOT_DELETE          0xc0000121u
#define HORIZON_REG_NOT_REGISTRY_FILE      0xc000015cu
#define HORIZON_REG_KEY_DELETED            0xc000017cu
#define HORIZON_REG_CHILD_MUST_BE_VOLATILE 0xc0000181u

#define HORIZON_REG_OPTION_VOLATILE    0x0001  /* REG_OPTION_VOLATILE */
#define HORIZON_REG_OPTION_CREATE_LINK 0x0002  /* REG_OPTION_CREATE_LINK */
#define HORIZON_REG_OBJ_OPENLINK       0x0100  /* OBJ_OPENLINK */

#define HORIZON_REG_NONE      0
#define HORIZON_REG_SZ        1
#define HORIZON_REG_EXPAND_SZ 2
#define HORIZON_REG_BINARY    3
#define HORIZON_REG_DWORD     4
#define HORIZON_REG_LINK      6
#define HORIZON_REG_MULTI_SZ  7

/* KEY_INFORMATION_CLASS */
#define HORIZON_REG_KEY_BASIC  0
#define HORIZON_REG_KEY_NODE   1
#define HORIZON_REG_KEY_FULL   2
#define HORIZON_REG_KEY_NAME   3
#define HORIZON_REG_KEY_CACHED 4
/* KEY_VALUE_INFORMATION_CLASS */
#define HORIZON_REG_VALUE_BASIC   0
#define HORIZON_REG_VALUE_FULL    1
#define HORIZON_REG_VALUE_PARTIAL 2

#define HORIZON_REG_CHANGE_NAME     0x1  /* REG_NOTIFY_CHANGE_NAME */
#define HORIZON_REG_CHANGE_LAST_SET 0x4  /* REG_NOTIFY_CHANGE_LAST_SET */

#define HORIZON_REG_MAX_NAME  (256 * 2)    /* bytes in a key name */
#define HORIZON_REG_MAX_VALUE (16383 * 2)  /* bytes in a value name */
#define HORIZON_REG_MAX_LINKS 16           /* symbolic links followed in one lookup */

#define HORIZON_REG_FLAG_VOLATILE 0x1
#define HORIZON_REG_FLAG_DELETED  0x2
#define HORIZON_REG_FLAG_SYMLINK  0x4

#define HORIZON_REG_TICKS_1601_TO_1970 116444736000000000LL

struct horizon_reg_value
{
    unsigned short *name;
    unsigned int namelen;
    unsigned int type;
    unsigned int len;
    unsigned char *data;
};

struct horizon_reg_notify
{
    unsigned int hkey;             /* the handle that asked */
    void *event;                   /* a reference to the server's event */
    unsigned int filter;           /* HORIZON_REG_CHANGE_* */
    int subtree;
    struct horizon_reg_notify *next;
};

struct horizon_reg_key
{
    unsigned short *name;
    unsigned int namelen;
    unsigned short *class;
    unsigned int classlen;
    struct horizon_reg_key *parent;    /* NULL for the root and deleted keys */
    struct horizon_reg_key **subkeys;  /* sorted by name */
    unsigned int subkey_count, subkey_max;
    struct horizon_reg_value *values;  /* sorted by name */
    unsigned int value_count, value_max;
    unsigned int flags;
    long long modif;                   /* 100 ns since 1601 */
    unsigned int refs;                 /* the parent's link and each handle */
    struct horizon_reg_notify *notify;
};

struct horizon_reg
{
    struct horizon_reg_key *root;      /* \Registry */
    long long (*now)(void);            /* the time for modified keys */
    void (*signal)( void *event );     /* signal a notification's event and release it */
};

/* What a key information query returns besides its data (enum_key_reply). */
struct horizon_reg_key_info
{
    int subkeys, max_subkey, max_class, values, max_value, max_data;
    long long modif;
    unsigned int total, namelen;
};

static const unsigned short horizon_reg_symlink_value[] =
    {'S','y','m','b','o','l','i','c','L','i','n','k','V','a','l','u','e'};

static inline int horizon_reg_compare( const unsigned short *a, unsigned int alen,
                                       const unsigned short *b, unsigned int blen )
{
    unsigned int i, count = (alen < blen ? alen : blen) / 2;

    for (i = 0; i < count; i++)
    {
        unsigned short ca = a[i] >= 'a' && a[i] <= 'z' ? a[i] - ('a' - 'A') : a[i];
        unsigned short cb = b[i] >= 'a' && b[i] <= 'z' ? b[i] - ('a' - 'A') : b[i];

        if (ca != cb) return ca < cb ? -1 : 1;
    }
    return alen == blen ? 0 : alen < blen ? -1 : 1;
}

/* The subkey named name, or NULL with *index where it would be inserted. */
static inline struct horizon_reg_key *horizon_reg_find_subkey( const struct horizon_reg_key *key,
                                                               const unsigned short *name, unsigned int len,
                                                               unsigned int *index )
{
    unsigned int min = 0, max = key->subkey_count;

    while (min < max)
    {
        unsigned int i = (min + max) / 2;
        int res = horizon_reg_compare( key->subkeys[i]->name, key->subkeys[i]->namelen, name, len );

        if (!res)
        {
            *index = i;
            return key->subkeys[i];
        }
        if (res > 0) max = i;
        else min = i + 1;
    }
    *index = min;
    return NULL;
}

static inline struct horizon_reg_value *horizon_reg_find_value( const struct horizon_reg_key *key,
                                                                const unsigned short *name, unsigned int len,
                                                                unsigned int *index )
{
    unsigned int min = 0, max = key->value_count;

    while (min < max)
    {
        unsigned int i = (min + max) / 2;
        int res = horizon_reg_compare( key->values[i].name, key->values[i].namelen, name, len );

        if (!res)
        {
            *index = i;
            return &key->values[i];
        }
        if (res > 0) max = i;
        else min = i + 1;
    }
    *index = min;
    return NULL;
}

/* Signal the notifications of key that watch change: all of them on the key
 * itself, and those watching the subtree on its parents. */
static inline void horizon_reg_check_notify( struct horizon_reg *reg, struct horizon_reg_key *key,
                                             unsigned int change, int not_subtree )
{
    struct horizon_reg_notify **ptr = &key->notify, *notify;

    while ((notify = *ptr))
    {
        if ((notify->subtree || not_subtree) && (change & notify->filter))
        {
            *ptr = notify->next;
            reg->signal( notify->event );
            free( notify );
        }
        else ptr = &notify->next;
    }
}

static inline void horizon_reg_touch( struct horizon_reg *reg, struct horizon_reg_key *key, unsigned int change )
{
    key->modif = reg->now();
    horizon_reg_check_notify( reg, key, change, 1 );
    for (key = key->parent; key; key = key->parent) horizon_reg_check_notify( reg, key, change, 0 );
}

static inline struct horizon_reg_key *horizon_reg_alloc_key( const unsigned short *name, unsigned int len,
                                                             long long modif )
{
    struct horizon_reg_key *key = calloc( 1, sizeof(*key) );

    if (!key) return NULL;
    if (len && !(key->name = malloc( len )))
    {
        free( key );
        return NULL;
    }
    if (len) memcpy( key->name, name, len );
    key->namelen = len;
    key->modif = modif;
    key->refs = 1;
    return key;
}

static inline void horizon_reg_release( struct horizon_reg *reg, struct horizon_reg_key *key )
{
    struct horizon_reg_notify *notify;
    unsigned int i;

    if (--key->refs) return;
    while ((notify = key->notify))
    {
        key->notify = notify->next;
        reg->signal( notify->event );
        free( notify );
    }
    for (i = 0; i < key->subkey_count; i++)
    {
        key->subkeys[i]->parent = NULL;
        key->subkeys[i]->flags |= HORIZON_REG_FLAG_DELETED;
        horizon_reg_release( reg, key->subkeys[i] );
    }
    for (i = 0; i < key->value_count; i++)
    {
        free( key->values[i].name );
        free( key->values[i].data );
    }
    free( key->subkeys );
    free( key->values );
    free( key->name );
    free( key->class );
    free( key );
}

/* Insert key into parent at index, from horizon_reg_find_subkey. The parent
 * takes the reference key was allocated with. */
static inline unsigned int horizon_reg_link( struct horizon_reg_key *parent, struct horizon_reg_key *key,
                                             unsigned int index )
{
    if (parent->subkey_count == parent->subkey_max)
    {
        unsigned int max = parent->subkey_max ? parent->subkey_max + parent->subkey_max / 2 : 8;
        struct horizon_reg_key **subkeys = realloc( parent->subkeys, max * sizeof(*subkeys) );

        if (!subkeys) return HORIZON_REG_NO_MEMORY;
        parent->subkeys = subkeys;
        parent->subkey_max = max;
    }
    memmove( parent->subkeys + index + 1, parent->subkeys + index,
             (parent->subkey_count - index) * sizeof(*parent->subkeys) );
    parent->subkeys[index] = key;
    parent->subkey_count++;
    key->parent = parent;
    return HORIZON_REG_SUCCESS;
}

static inline int horizon_reg_init( struct horizon_reg *reg, long long (*now)(void), void (*signal)( void * ) )
{
    static const unsigned short name[] = {'R','e','g','i','s','t','r','y'};

    reg->now = now;
    reg->signal = signal;
    reg->root = horizon_reg_alloc_key( name, sizeof(name), now() );
    return reg->root != NULL;
}

/* Bytes of name before its first backslash. */
static inline unsigned int horizon_reg_element( const unsigned short *name, unsigned int len )
{
    unsigned int i;

    for (i = 0; i < len / 2; i++) if (name[i] == '\\') break;
    return i * 2;
}

struct horizon_reg_path
{
    struct horizon_reg_key *key;   /* the key named, or the parent of the missing last element */
    const unsigned short *last;    /* the missing last element, NULL when the key exists */
    unsigned int last_len;
};

/* Walk name from base, or from \Registry when base is NULL and name is
 * absolute. Symbolic links are followed, except a last element opened with
 * OBJ_OPENLINK. Only the last element may be missing. */
static inline unsigned int horizon_reg_lookup( struct horizon_reg *reg, struct horizon_reg_key *base,
                                               const unsigned short *name, unsigned int len,
                                               unsigned int attributes, struct horizon_reg_path *path,
                                               unsigned int links )
{
    static const unsigned short prefix[] = {'\\','R','e','g','i','s','t','r','y'};
    struct horizon_reg_key *key = base;
    unsigned int index;

    len &= ~1u;
    if (!key)
    {
        if (!len || name[0] != '\\') return HORIZON_REG_PATH_SYNTAX_BAD;
        if (len < sizeof(prefix) || horizon_reg_compare( name, sizeof(prefix), prefix, sizeof(prefix) ) ||
            (len > sizeof(prefix) && name[sizeof(prefix) / 2] != '\\'))
            return HORIZON_REG_NAME_NOT_FOUND;
        key = reg->root;
        name += sizeof(prefix) / 2;
        len -= sizeof(prefix);
    }
    else if (len && name[0] == '\\') return HORIZON_REG_PATH_SYNTAX_BAD;

    for (;;)
    {
        struct horizon_reg_key *found;
        unsigned int elem, next;

        if (key->flags & HORIZON_REG_FLAG_DELETED) return HORIZON_REG_KEY_DELETED;
        while (len && name[0] == '\\')
        {
            name++;
            len -= 2;
        }
        if ((key->flags & HORIZON_REG_FLAG_SYMLINK) && (len || !(attributes & HORIZON_REG_OBJ_OPENLINK)))
        {
            struct horizon_reg_value *value = horizon_reg_find_value( key, horizon_reg_symlink_value,
                                                                      sizeof(horizon_reg_symlink_value), &index );
            struct horizon_reg_path target;
            unsigned int status;

            if (!value || value->len < 2 || links >= HORIZON_REG_MAX_LINKS ||
                ((const unsigned short *)value->data)[0] != '\\')
                return HORIZON_REG_NAME_NOT_FOUND;
            status = horizon_reg_lookup( reg, NULL, (const unsigned short *)value->data, value->len, 0,
                                         &target, links + 1 );
            if (status) return status;
            if (target.last) return HORIZON_REG_NAME_NOT_FOUND;
            key = target.key;
            continue;
        }
        if (!len) break;

        elem = horizon_reg_element( name, len );
        if (elem > HORIZON_REG_MAX_NAME) return HORIZON_REG_INVALID_PARAMETER;
        for (next = elem; next < len && name[next / 2] == '\\'; next += 2) ;
        if (!(found = horizon_reg_find_subkey( key, name, elem, &index )))
        {
            if (next < len) return HORIZON_REG_NAME_NOT_FOUND;
            path->key = key;
            path->last = name;
            path->last_len = elem;
            return HORIZON_REG_SUCCESS;
        }
        key = found;
        name += next / 2;
        len -= next;
    }
    path->key = key;
    path->last = NULL;
    path->last_len = 0;
    return HORIZON_REG_SUCCESS;
}

/* Open a key; *ret holds a reference for the caller. */
static inline unsigned int horizon_reg_open( struct horizon_reg *reg, struct horizon_reg_key *base,
                                             const unsigned short *name, unsigned int len,
                                             unsigned int attributes, struct horizon_reg_key **ret )
{
    struct horizon_reg_path path;
    unsigned int status = horizon_reg_lookup( reg, base, name, len, attributes, &path, 0 );

    *ret = NULL;
    if (status) return status;
    if (path.last) return HORIZON_REG_NAME_NOT_FOUND;
    path.key->refs++;
    *ret = path.key;
    return HORIZON_REG_SUCCESS;
}

/* Open or create a key; *ret holds a reference for the caller. Returns
 * HORIZON_REG_NAME_EXISTS for a key that existed. A class replaces the key's. */
static inline unsigned int horizon_reg_create( struct horizon_reg *reg, struct horizon_reg_key *base,
                                               const unsigned short *name, unsigned int len,
                                               unsigned int attributes, unsigned int options,
                                               const unsigned short *class, unsigned int classlen,
                                               struct horizon_reg_key **ret )
{
    struct horizon_reg_path path;
    struct horizon_reg_key *key;
    unsigned short *class_copy = NULL;
    unsigned int status, index;

    *ret = NULL;
    if (options & HORIZON_REG_OPTION_CREATE_LINK) attributes |= HORIZON_REG_OBJ_OPENLINK;
    if ((status = horizon_reg_lookup( reg, base, name, len, attributes, &path, 0 ))) return status;
    if (!path.last && (options & HORIZON_REG_OPTION_CREATE_LINK)) return HORIZON_REG_NAME_COLLISION;
    if (path.last && !(options & HORIZON_REG_OPTION_VOLATILE) && (path.key->flags & HORIZON_REG_FLAG_VOLATILE))
        return HORIZON_REG_CHILD_MUST_BE_VOLATILE;

    classlen &= ~1u;
    if (classlen && !(class_copy = malloc( classlen ))) return HORIZON_REG_NO_MEMORY;
    if (classlen) memcpy( class_copy, class, classlen );

    if (!path.last)
    {
        key = path.key;
        status = HORIZON_REG_NAME_EXISTS;
    }
    else
    {
        if (!(key = horizon_reg_alloc_key( path.last, path.last_len, reg->now() )))
        {
            free( class_copy );
            return HORIZON_REG_NO_MEMORY;
        }
        horizon_reg_find_subkey( path.key, path.last, path.last_len, &index );
        if (horizon_reg_link( path.key, key, index ))
        {
            free( class_copy );
            horizon_reg_release( reg, key );
            return HORIZON_REG_NO_MEMORY;
        }
        if (options & HORIZON_REG_OPTION_VOLATILE) key->flags |= HORIZON_REG_FLAG_VOLATILE;
        if (options & HORIZON_REG_OPTION_CREATE_LINK) key->flags |= HORIZON_REG_FLAG_SYMLINK;
        horizon_reg_touch( reg, path.key, HORIZON_REG_CHANGE_NAME );
    }
    if (class_copy)
    {
        free( key->class );
        key->class = class_copy;
        key->classlen = classlen;
    }
    key->refs++;
    *ret = key;
    return status;
}

/* Delete a key without subkeys. Its handles stay valid and report it deleted. */
static inline unsigned int horizon_reg_delete( struct horizon_reg *reg, struct horizon_reg_key *key )
{
    struct horizon_reg_key *parent = key->parent;
    unsigned int i;

    if (key->flags & HORIZON_REG_FLAG_DELETED) return HORIZON_REG_SUCCESS;
    if (!parent || key->subkey_count) return HORIZON_REG_ACCESS_DENIED;

    for (i = 0; i < parent->subkey_count; i++) if (parent->subkeys[i] == key) break;
    memmove( parent->subkeys + i, parent->subkeys + i + 1, (parent->subkey_count - i - 1) * sizeof(*parent->subkeys) );
    parent->subkey_count--;
    key->parent = NULL;
    key->flags |= HORIZON_REG_FLAG_DELETED;
    horizon_reg_touch( reg, parent, HORIZON_REG_CHANGE_NAME );
    horizon_reg_release( reg, key );
    return HORIZON_REG_SUCCESS;
}

static inline unsigned int horizon_reg_rename( struct horizon_reg *reg, struct horizon_reg_key *key,
                                               const unsigned short *name, unsigned int len )
{
    struct horizon_reg_key *parent = key->parent;
    unsigned int index, current;
    unsigned short *copy;

    len &= ~1u;
    if (!len || horizon_reg_element( name, len ) != len || len > HORIZON_REG_MAX_NAME)
        return HORIZON_REG_INVALID_PARAMETER;
    if (!parent || horizon_reg_find_subkey( parent, name, len, &index )) return HORIZON_REG_CANNOT_DELETE;
    if (!(copy = malloc( len ))) return HORIZON_REG_NO_MEMORY;
    memcpy( copy, name, len );

    for (current = 0; current < parent->subkey_count; current++) if (parent->subkeys[current] == key) break;
    if (current < index)
    {
        index--;
        memmove( parent->subkeys + current, parent->subkeys + current + 1, (index - current) * sizeof(*parent->subkeys) );
    }
    else if (current > index)
        memmove( parent->subkeys + index + 1, parent->subkeys + index, (current - index) * sizeof(*parent->subkeys) );
    parent->subkeys[index] = key;

    free( key->name );
    key->name = copy;
    key->namelen = len;
    horizon_reg_touch( reg, key, HORIZON_REG_CHANGE_NAME );
    return HORIZON_REG_SUCCESS;
}

/* \Registry\... of a key. */
static inline unsigned short *horizon_reg_full_name( const struct horizon_reg_key *key, unsigned int *len )
{
    const struct horizon_reg_key *k;
    unsigned int size = 0, pos;
    unsigned short *name;

    for (k = key; k; k = k->parent) size += 2 + k->namelen;
    if (!(name = malloc( size ))) return NULL;
    pos = size;
    for (k = key; k; k = k->parent)
    {
        pos -= k->namelen;
        memcpy( name + pos / 2, k->name, k->namelen );
        pos -= 2;
        name[pos / 2] = '\\';
    }
    *len = size;
    return name;
}

/* The key itself (index -1) or a subkey, as enum_key replies: up to max
 * bytes of name and class go to data, *size of them. */
static inline unsigned int horizon_reg_enum_key( const struct horizon_reg_key *key, int index, int info_class,
                                                 struct horizon_reg_key_info *info, unsigned char *data,
                                                 unsigned int max, unsigned int *size )
{
    unsigned short *fullname = NULL;
    unsigned int namelen, classlen, len, i;

    memset( info, 0, sizeof(*info) );
    *size = 0;
    if (index != -1)
    {
        if (index < 0 || (unsigned int)index >= key->subkey_count) return HORIZON_REG_NO_MORE_ENTRIES;
        key = key->subkeys[index];
    }
    namelen = key->namelen;
    classlen = key->classlen;

    switch (info_class)
    {
    case HORIZON_REG_KEY_NAME:
        if (!(fullname = horizon_reg_full_name( key, &namelen ))) return HORIZON_REG_NO_MEMORY;
        /* fall through */
    case HORIZON_REG_KEY_BASIC:
        classlen = 0;
        /* fall through */
    case HORIZON_REG_KEY_NODE:
        break;
    case HORIZON_REG_KEY_FULL:
    case HORIZON_REG_KEY_CACHED:
        for (i = 0; i < key->subkey_count; i++)
        {
            if ((int)key->subkeys[i]->namelen > info->max_subkey) info->max_subkey = key->subkeys[i]->namelen;
            if ((int)key->subkeys[i]->classlen > info->max_class) info->max_class = key->subkeys[i]->classlen;
        }
        for (i = 0; i < key->value_count; i++)
        {
            if ((int)key->values[i].namelen > info->max_value) info->max_value = key->values[i].namelen;
            if ((int)key->values[i].len > info->max_data) info->max_data = key->values[i].len;
        }
        info->namelen = namelen;
        if (info_class == HORIZON_REG_KEY_CACHED) classlen = 0;
        namelen = 0;
        break;
    default:
        return HORIZON_REG_INVALID_PARAMETER;
    }
    info->subkeys = key->subkey_count;
    info->values = key->value_count;
    info->modif = key->modif;
    info->total = namelen + classlen;

    len = info->total < max ? info->total : max;
    if (len > namelen)
    {
        info->namelen = namelen;
        memcpy( data, key->name, namelen );
        memcpy( data + namelen, key->class, len - namelen );
    }
    else if (len && fullname)
    {
        info->namelen = namelen;
        memcpy( data, fullname, len );
    }
    else if (len)
    {
        info->namelen = len;
        memcpy( data, key->name, len );
    }
    *size = len;
    free( fullname );
    return HORIZON_REG_SUCCESS;
}

static inline unsigned int horizon_reg_insert_value( struct horizon_reg_key *key, const unsigned short *name,
                                                     unsigned int namelen, unsigned int index,
                                                     struct horizon_reg_value **ret )
{
    struct horizon_reg_value *value;
    unsigned short *copy = NULL;

    if (namelen > HORIZON_REG_MAX_VALUE) return HORIZON_REG_NAME_TOO_LONG;
    if (key->value_count == key->value_max)
    {
        unsigned int max = key->value_max ? key->value_max + key->value_max / 2 : 8;
        struct horizon_reg_value *values = realloc( key->values, max * sizeof(*values) );

        if (!values) return HORIZON_REG_NO_MEMORY;
        key->values = values;
        key->value_max = max;
    }
    if (namelen && !(copy = malloc( namelen ))) return HORIZON_REG_NO_MEMORY;
    if (namelen) memcpy( copy, name, namelen );
    memmove( key->values + index + 1, key->values + index, (key->value_count - index) * sizeof(*key->values) );
    key->value_count++;
    value = &key->values[index];
    value->name = copy;
    value->namelen = namelen;
    value->type = HORIZON_REG_NONE;
    value->len = 0;
    value->data = NULL;
    *ret = value;
    return HORIZON_REG_SUCCESS;
}

static inline unsigned int horizon_reg_set_value( struct horizon_reg *reg, struct horizon_reg_key *key,
                                                  const unsigned short *name, unsigned int namelen,
                                                  unsigned int type, const void *data, unsigned int len )
{
    struct horizon_reg_value *value;
    unsigned char *copy = NULL;
    unsigned int index, status;

    namelen &= ~1u;
    if ((value = horizon_reg_find_value( key, name, namelen, &index )) &&
        value->type == type && value->len == len && value->data && !memcmp( value->data, data, len ))
        return HORIZON_REG_SUCCESS;
    if ((key->flags & HORIZON_REG_FLAG_SYMLINK) &&
        (type != HORIZON_REG_LINK ||
         horizon_reg_compare( name, namelen, horizon_reg_symlink_value, sizeof(horizon_reg_symlink_value) )))
        return HORIZON_REG_ACCESS_DENIED;

    if (len && !(copy = malloc( len ))) return HORIZON_REG_NO_MEMORY;
    if (len) memcpy( copy, data, len );
    if (!value && (status = horizon_reg_insert_value( key, name, namelen, index, &value )))
    {
        free( copy );
        return status;
    }
    free( value->data );
    value->type = type;
    value->len = len;
    value->data = copy;
    horizon_reg_touch( reg, key, HORIZON_REG_CHANGE_LAST_SET );
    return HORIZON_REG_SUCCESS;
}

/* Up to max bytes of the value's data go to data, *size of them. */
static inline unsigned int horizon_reg_get_value( const struct horizon_reg_key *key, const unsigned short *name,
                                                  unsigned int namelen, int *type, unsigned int *total,
                                                  unsigned char *data, unsigned int max, unsigned int *size )
{
    const struct horizon_reg_value *value;
    unsigned int index;

    *size = 0;
    if (!(value = horizon_reg_find_value( key, name, namelen & ~1u, &index )))
    {
        *type = -1;
        return HORIZON_REG_NAME_NOT_FOUND;
    }
    *type = value->type;
    *total = value->len;
    *size = value->len < max ? value->len : max;
    if (*size) memcpy( data, value->data, *size );
    return HORIZON_REG_SUCCESS;
}

/* The index'th value, as enum_key_value replies. *namelen is left alone when
 * no data is returned. */
static inline unsigned int horizon_reg_enum_value( const struct horizon_reg_key *key, int index, int info_class,
                                                   int *type, unsigned int *total, unsigned int *namelen,
                                                   unsigned char *data, unsigned int max, unsigned int *size )
{
    const struct horizon_reg_value *value;
    unsigned int name_bytes, len;

    *size = 0;
    if (index < 0 || (unsigned int)index >= key->value_count) return HORIZON_REG_NO_MORE_ENTRIES;
    value = &key->values[index];
    *type = value->type;
    name_bytes = value->namelen;

    switch (info_class)
    {
    case HORIZON_REG_VALUE_BASIC:
        *total = name_bytes;
        break;
    case HORIZON_REG_VALUE_FULL:
        *total = name_bytes + value->len;
        break;
    case HORIZON_REG_VALUE_PARTIAL:
        *total = value->len;
        name_bytes = 0;
        break;
    default:
        return HORIZON_REG_INVALID_PARAMETER;
    }

    len = *total < max ? *total : max;
    if (len > name_bytes)
    {
        *namelen = name_bytes;
        memcpy( data, value->name, name_bytes );
        memcpy( data + name_bytes, value->data, len - name_bytes );
    }
    else if (len)
    {
        *namelen = len;
        memcpy( data, value->name, len );
    }
    *size = len;
    return HORIZON_REG_SUCCESS;
}

static inline unsigned int horizon_reg_delete_value( struct horizon_reg *reg, struct horizon_reg_key *key,
                                                     const unsigned short *name, unsigned int namelen )
{
    struct horizon_reg_value *value;
    unsigned int index;

    if (!(value = horizon_reg_find_value( key, name, namelen & ~1u, &index ))) return HORIZON_REG_NAME_NOT_FOUND;
    free( value->name );
    free( value->data );
    memmove( key->values + index, key->values + index + 1, (key->value_count - index - 1) * sizeof(*key->values) );
    key->value_count--;
    horizon_reg_touch( reg, key, HORIZON_REG_CHANGE_LAST_SET );
    return HORIZON_REG_SUCCESS;
}

/* Watch key for a change; event is a reference, signaled and released once.
 * Returns HORIZON_REG_PENDING. */
static inline unsigned int horizon_reg_notify( struct horizon_reg_key *key, unsigned int hkey, void *event,
                                               int subtree, unsigned int filter )
{
    struct horizon_reg_notify *notify = malloc( sizeof(*notify) );

    if (!notify) return HORIZON_REG_NO_MEMORY;
    notify->hkey = hkey;
    notify->event = event;
    notify->subtree = subtree;
    notify->filter = filter;
    notify->next = key->notify;
    key->notify = notify;
    return HORIZON_REG_PENDING;
}

/* Closing a handle signals the notifications it asked for. */
static inline void horizon_reg_handle_closed( struct horizon_reg *reg, struct horizon_reg_key *key, unsigned int hkey )
{
    struct horizon_reg_notify **ptr = &key->notify, *notify;

    while ((notify = *ptr))
    {
        if (notify->hkey != hkey)
        {
            ptr = &notify->next;
            continue;
        }
        *ptr = notify->next;
        reg->signal( notify->event );
        free( notify );
    }
}

/* Create each key of an ASCII path below key, for the root keys. The tree
 * holds the only reference. */
static inline struct horizon_reg_key *horizon_reg_create_ascii( struct horizon_reg *reg, struct horizon_reg_key *key,
                                                                const char *path )
{
    unsigned short name[256];

    while (*path && key)
    {
        struct horizon_reg_key *sub;
        unsigned int len = 0, index;

        while (path[len] && path[len] != '\\' && len < 256)
        {
            name[len] = (unsigned char)path[len];
            len++;
        }
        if (!(sub = horizon_reg_find_subkey( key, name, len * 2, &index )))
        {
            if (!(sub = horizon_reg_alloc_key( name, len * 2, reg->now() ))) return NULL;
            if (horizon_reg_link( key, sub, index ))
            {
                horizon_reg_release( reg, sub );
                return NULL;
            }
        }
        key = sub;
        path += len;
        if (*path == '\\') path++;
    }
    return key;
}

/* Registry files (server/registry.c's load_keys). */

struct horizon_reg_loader
{
    const char *text, *end;
    char *line;               /* the current line, null-terminated */
    size_t line_size;
    unsigned short *tmp;
    unsigned int tmp_size;    /* bytes */
};

/* 1 with the next line, 0 at the end, -1 without memory. */
static inline int horizon_reg_next_line( struct horizon_reg_loader *loader )
{
    const char *eol;
    size_t len;

    if (loader->text >= loader->end) return 0;
    for (eol = loader->text; eol < loader->end && *eol != '\n'; eol++) ;
    len = eol - loader->text;
    if (len && loader->text[len - 1] == '\r') len--;
    if (len + 1 > loader->line_size)
    {
        char *line = realloc( loader->line, len + 1 );

        if (!line) return -1;
        loader->line = line;
        loader->line_size = len + 1;
    }
    memcpy( loader->line, loader->text, len );
    loader->line[len] = 0;
    loader->text = eol < loader->end ? eol + 1 : eol;
    return 1;
}

static inline int horizon_reg_tmp_space( struct horizon_reg_loader *loader, size_t bytes )
{
    unsigned short *tmp;

    bytes = (bytes + 1) & ~(size_t)1;
    if (loader->tmp_size >= bytes) return 1;
    if (bytes > 0x7fffffff || !(tmp = realloc( loader->tmp, bytes ))) return 0;
    loader->tmp = tmp;
    loader->tmp_size = bytes;
    return 1;
}

/* An escaped string up to endchar (server/unicode.c's parse_strW). Returns
 * the characters read including endchar, or -1. *len is the room in dest on
 * entry and the bytes stored with a terminating null on return. */
static inline int horizon_reg_parse_str( unsigned short *dest, unsigned int *len, const char *src, char endchar )
{
    unsigned short *start = dest, *end = dest + *len / 2;
    const unsigned char *p = (const unsigned char *)src;

    while (*p && *p != endchar && dest < end)
    {
        unsigned int res, count;

        if (*p == '\\')
        {
            p++;
            if (!*p) break;
            switch (*p)
            {
            case 'a': *dest++ = '\a'; p++; continue;
            case 'b': *dest++ = '\b'; p++; continue;
            case 'e': *dest++ = 0x1b; p++; continue;
            case 'f': *dest++ = '\f'; p++; continue;
            case 'n': *dest++ = '\n'; p++; continue;
            case 'r': *dest++ = '\r'; p++; continue;
            case 't': *dest++ = '\t'; p++; continue;
            case 'v': *dest++ = '\v'; p++; continue;
            case 'x':
                p++;
                if (!isxdigit( *p )) *dest = 'x';
                else
                {
                    for (res = 0, count = 0; count < 4 && isxdigit( *p ); count++, p++)
                        res = res * 16 + (isdigit( *p ) ? *p - '0' : (tolower( *p ) - 'a' + 10));
                    *dest = res;
                }
                dest++;
                continue;
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
                for (res = 0, count = 0; count < 3 && *p >= '0' && *p <= '7'; count++, p++)
                    res = res * 8 + (*p - '0');
                *dest++ = res;
                continue;
            }
            /* an unrecognized escape is the character itself */
        }

        if (*p < 0x80)
        {
            *dest++ = *p++;
            continue;
        }
        /* UTF-8; invalid bytes are skipped */
        if (*p >= 0xf0 && *p <= 0xf4) { res = *p & 0x07; count = 3; }
        else if (*p >= 0xe0) { res = *p & 0x0f; count = 2; }
        else if (*p >= 0xc2) { res = *p & 0x1f; count = 1; }
        else { p++; continue; }
        p++;
        while (count && (*p & 0xc0) == 0x80)
        {
            res = (res << 6) | (*p++ & 0x3f);
            count--;
        }
        if (count || res > 0x10ffff) continue;
        if (res <= 0xffff) *dest++ = res;
        else
        {
            res -= 0x10000;
            *dest++ = 0xd800 | (res >> 10);
            if (dest < end) *dest++ = 0xdc00 | (res & 0x3ff);
        }
    }
    if (dest >= end) return -1;
    *dest++ = 0;
    if (!*p) return -1;
    *len = (dest - start) * 2;
    return (const char *)p + 1 - src;
}

/* [path] timestamp: the key, created with its parents as needed. */
static inline struct horizon_reg_key *horizon_reg_load_key( struct horizon_reg *reg, struct horizon_reg_key *key,
                                                            struct horizon_reg_loader *loader, const char *buffer )
{
    const unsigned short *name;
    unsigned int len, mod;
    long long modif;
    int res;

    if (!horizon_reg_tmp_space( loader, (strlen( buffer ) + 1) * 2 )) return NULL;
    len = loader->tmp_size;
    if ((res = horizon_reg_parse_str( loader->tmp, &len, buffer, ']' )) == -1) return NULL;
    if (sscanf( buffer + res, " %u", &mod ) == 1)
        modif = (long long)mod * 10000000 + HORIZON_REG_TICKS_1601_TO_1970;
    else
        modif = reg->now();

    name = loader->tmp;
    len -= 2;  /* the null */
    while (len)
    {
        struct horizon_reg_key *sub;
        unsigned int elem = horizon_reg_element( name, len ), next, index;

        for (next = elem; next < len && name[next / 2] == '\\'; next += 2) ;
        if (elem > HORIZON_REG_MAX_NAME) return NULL;
        if (elem && !(sub = horizon_reg_find_subkey( key, name, elem, &index )))
        {
            if (!(sub = horizon_reg_alloc_key( name, elem, modif ))) return NULL;
            if (horizon_reg_link( key, sub, index ))
            {
                horizon_reg_release( reg, sub );
                return NULL;
            }
        }
        if (elem) key = sub;
        name += next / 2;
        len -= next;
    }
    return key;
}

static inline int horizon_reg_data_type( const char *buffer, unsigned int *type, unsigned int *parse_type )
{
    static const struct { const char *tag; int len; int type; unsigned int parse_type; } types[] =
    {
        { "\"",        1, HORIZON_REG_SZ,        HORIZON_REG_SZ },
        { "str:\"",    5, HORIZON_REG_SZ,        HORIZON_REG_SZ },
        { "str(2):\"", 8, HORIZON_REG_EXPAND_SZ, HORIZON_REG_SZ },
        { "str(7):\"", 8, HORIZON_REG_MULTI_SZ,  HORIZON_REG_SZ },
        { "hex:",      4, HORIZON_REG_BINARY,    HORIZON_REG_BINARY },
        { "dword:",    6, HORIZON_REG_DWORD,     HORIZON_REG_DWORD },
        { "hex(",      4, -1,                    HORIZON_REG_BINARY },
    };
    unsigned int i;
    char *end;

    for (i = 0; i < sizeof(types) / sizeof(types[0]); i++)
    {
        if (strncmp( types[i].tag, buffer, types[i].len )) continue;
        *parse_type = types[i].parse_type;
        if (types[i].type != -1)
        {
            *type = types[i].type;
            return types[i].len;
        }
        *type = strtoul( buffer + 4, &end, 16 );
        if (end <= buffer + 4 || strncmp( end, "):", 2 )) return 0;
        return end + 2 - buffer;
    }
    return 0;
}

/* Comma-separated hex bytes. Returns the characters read or -1. */
static inline int horizon_reg_parse_hex( unsigned char *dest, unsigned int *len, const char *buffer )
{
    const char *p = buffer;
    unsigned int count = 0;
    char *end;

    while (isxdigit( (unsigned char)*p ))
    {
        unsigned long val = strtoul( p, &end, 16 );

        if (end == p || val > 0xff || count >= *len) return -1;
        dest[count++] = val;
        p = end;
        while (isspace( (unsigned char)*p )) p++;
        if (*p == ',') p++;
        while (isspace( (unsigned char)*p )) p++;
    }
    *len = count;
    return p - buffer;
}

/* "name"=data or @=data. */
static inline int horizon_reg_load_value( struct horizon_reg_loader *loader, struct horizon_reg_key *key,
                                          const char *buffer )
{
    struct horizon_reg_value *value;
    unsigned int namelen = 0, len, index, type, parse_type, dword;
    const char *p = buffer;
    unsigned char *data = NULL;
    const void *src;
    int res;

    if (!horizon_reg_tmp_space( loader, (strlen( buffer ) + 1) * 2 )) return 0;
    if (*p == '@') p++;
    else
    {
        namelen = loader->tmp_size;
        if ((res = horizon_reg_parse_str( loader->tmp, &namelen, p + 1, '"' )) == -1) return 0;
        namelen -= 2;
        p += res + 1;
    }
    while (isspace( (unsigned char)*p )) p++;
    if (*p++ != '=') return 0;
    while (isspace( (unsigned char)*p )) p++;
    if (!(value = horizon_reg_find_value( key, loader->tmp, namelen, &index )) &&
        horizon_reg_insert_value( key, loader->tmp, namelen, index, &value ))
        return 0;
    if (!(res = horizon_reg_data_type( p, &type, &parse_type ))) goto error;
    p += res;

    switch (parse_type)
    {
    case HORIZON_REG_SZ:
        len = loader->tmp_size;
        if (horizon_reg_parse_str( loader->tmp, &len, p, '"' ) == -1) goto error;
        src = loader->tmp;
        break;
    case HORIZON_REG_DWORD:
        dword = strtoul( p, NULL, 16 );
        src = &dword;
        len = sizeof(dword);
        break;
    default:  /* hex bytes, continued on the next line after a backslash */
        len = 0;
        for (;;)
        {
            unsigned int room = 1 + strlen( p ) / 2;

            if (!horizon_reg_tmp_space( loader, len + room )) goto error;
            if ((res = horizon_reg_parse_hex( (unsigned char *)loader->tmp + len, &room, p )) == -1) goto error;
            len += room;
            p += res;
            while (isspace( (unsigned char)*p )) p++;
            if (!*p) break;
            if (*p != '\\' || horizon_reg_next_line( loader ) != 1) goto error;
            for (p = loader->line; isspace( (unsigned char)*p ); p++) ;
        }
        src = loader->tmp;
        break;
    }

    if (len && !(data = malloc( len ))) goto error;
    if (len) memcpy( data, src, len );
    free( value->data );
    value->data = data;
    value->len = len;
    value->type = type;
    return 1;

error:
    free( value->data );
    value->data = NULL;
    value->len = 0;
    value->type = HORIZON_REG_NONE;
    return 0;
}

static inline void horizon_reg_load_option( struct horizon_reg_loader *loader, struct horizon_reg_key *key,
                                            const char *buffer )
{
    if (!strncmp( buffer, "#time=", 6 ))
    {
        unsigned long long modif = 0;
        const char *p;

        for (p = buffer + 6; isxdigit( (unsigned char)*p ); p++)
            modif = (modif << 4) | (isdigit( (unsigned char)*p ) ? *p - '0' : tolower( (unsigned char)*p ) - 'a' + 10);
        key->modif = modif;
    }
    else if (!strncmp( buffer, "#class=\"", 8 ))
    {
        unsigned int len;
        unsigned short *class;

        if (!horizon_reg_tmp_space( loader, (strlen( buffer ) + 1) * 2 )) return;
        len = loader->tmp_size;
        if (horizon_reg_parse_str( loader->tmp, &len, buffer + 8, '"' ) == -1) return;
        if (!(class = malloc( len ))) return;
        memcpy( class, loader->tmp, len );
        free( key->class );
        key->class = class;
        key->classlen = len;
    }
    else if (!strncmp( buffer, "#link", 5 )) key->flags |= HORIZON_REG_FLAG_SYMLINK;
}

/* Load registry text below base. Lines that cannot be read are counted in
 * *errors and skipped, as Wine reports and skips them. */
static inline unsigned int horizon_reg_load( struct horizon_reg *reg, struct horizon_reg_key *base,
                                             const char *text, size_t size, unsigned int *errors )
{
    struct horizon_reg_loader loader = { text, text + size, NULL, 0, NULL, 0 };
    struct horizon_reg_key *key = NULL;
    unsigned int status = HORIZON_REG_SUCCESS;
    int ret;

    *errors = 0;
    if (horizon_reg_next_line( &loader ) != 1 || strcmp( loader.line, "WINE REGISTRY Version 2" ))
        status = HORIZON_REG_NOT_REGISTRY_FILE;
    else
    {
        while ((ret = horizon_reg_next_line( &loader )) == 1)
        {
            const char *p = loader.line;

            while (isspace( (unsigned char)*p )) p++;
            switch (*p)
            {
            case '[':
                if (!(key = horizon_reg_load_key( reg, base, &loader, p + 1 ))) (*errors)++;
                break;
            case '@':
            case '"':
                if (!key || !horizon_reg_load_value( &loader, key, p )) (*errors)++;
                break;
            case '#':
                if (key) horizon_reg_load_option( &loader, key, p );
                break;
            case ';':
            case 0:
                break;
            default:
                (*errors)++;
                break;
            }
        }
        if (ret == -1) status = HORIZON_REG_NO_MEMORY;
    }
    free( loader.line );
    free( loader.tmp );
    return status;
}

#endif
