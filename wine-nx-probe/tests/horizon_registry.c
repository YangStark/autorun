/* Host test for the Horizon server registry (dlls/ntdll/unix/horizon_registry.h).
 * An optional argument names a Wine system.reg to load as \Registry\Machine. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../dlls/ntdll/unix/horizon_registry.h"

static long long ticks = 1000;
static long long test_now(void) { return ++ticks; }

struct fake_event { unsigned int signaled; };
static void test_signal( void *event ) { ((struct fake_event *)event)->signaled++; }

/* UTF-16 of an ASCII string, in one of a few rotating buffers. */
static const unsigned short *w( const char *s, unsigned int *len )
{
    static unsigned short buffers[8][512];
    static unsigned int next;
    unsigned short *buf = buffers[next++ % 8];
    unsigned int i;

    for (i = 0; s[i]; i++) buf[i] = (unsigned char)s[i];
    *len = i * 2;
    return buf;
}

static int same( const void *data, unsigned int size, const char *s )
{
    unsigned int len;
    const unsigned short *str = w( s, &len );

    return size == len && !memcmp( data, str, len );
}

static unsigned int create( struct horizon_reg *reg, struct horizon_reg_key *base, const char *path,
                            unsigned int options, struct horizon_reg_key **key )
{
    unsigned int len;
    const unsigned short *name = w( path, &len );

    return horizon_reg_create( reg, base, name, len, 0, options, NULL, 0, key );
}

static unsigned int open_key( struct horizon_reg *reg, struct horizon_reg_key *base, const char *path,
                              unsigned int attributes, struct horizon_reg_key **key )
{
    unsigned int len;
    const unsigned short *name = w( path, &len );

    return horizon_reg_open( reg, base, name, len, attributes, key );
}

static unsigned int set_sz( struct horizon_reg *reg, struct horizon_reg_key *key, const char *name, const char *value )
{
    unsigned int namelen, len;
    unsigned short copy[256];
    const unsigned short *str = w( name, &namelen ), *data = w( value, &len );

    memcpy( copy, str, namelen );
    return horizon_reg_set_value( reg, key, copy, namelen, HORIZON_REG_SZ, data, len );
}

static void init( struct horizon_reg *reg, struct horizon_reg_key **machine )
{
    assert( horizon_reg_init( reg, test_now, test_signal ) );
    assert( (*machine = horizon_reg_create_ascii( reg, reg->root, "Machine" )) );
    assert( horizon_reg_create_ascii( reg, reg->root, "User\\.Default" ) );
}

static void test_paths(void)
{
    struct horizon_reg reg;
    struct horizon_reg_key *machine, *software, *key, *other, *third;
    char long_name[300];

    init( &reg, &machine );
    assert( create( &reg, NULL, "\\Registry\\Machine\\Software", 0, &software ) == HORIZON_REG_SUCCESS );
    assert( software->refs == 2 && software->parent == machine );
    assert( create( &reg, NULL, "\\REGISTRY\\machine\\SOFTWARE\\", 0, &key ) == HORIZON_REG_NAME_EXISTS );
    assert( key == software && software->refs == 3 );
    horizon_reg_release( &reg, key );

    /* Only the last element is created. */
    assert( create( &reg, NULL, "\\Registry\\Machine\\Missing\\Child", 0, &key ) == HORIZON_REG_NAME_NOT_FOUND );
    assert( !key );
    assert( create( &reg, software, "Wine", 0, &key ) == HORIZON_REG_SUCCESS );
    assert( open_key( &reg, NULL, "\\Registry\\Machine\\Software\\\\wine", 0, &other ) == HORIZON_REG_SUCCESS );
    assert( other == key );
    horizon_reg_release( &reg, other );
    assert( open_key( &reg, machine, "software\\WINE\\", 0, &other ) == HORIZON_REG_SUCCESS && other == key );
    horizon_reg_release( &reg, other );
    assert( open_key( &reg, key, "", 0, &other ) == HORIZON_REG_SUCCESS && other == key );
    horizon_reg_release( &reg, other );
    assert( open_key( &reg, NULL, "\\Registry", 0, &other ) == HORIZON_REG_SUCCESS && other == reg.root );
    horizon_reg_release( &reg, other );

    assert( open_key( &reg, NULL, "", 0, &other ) == HORIZON_REG_PATH_SYNTAX_BAD );
    assert( open_key( &reg, NULL, "Registry\\Machine", 0, &other ) == HORIZON_REG_PATH_SYNTAX_BAD );
    assert( open_key( &reg, machine, "\\Software", 0, &other ) == HORIZON_REG_PATH_SYNTAX_BAD );
    assert( open_key( &reg, NULL, "\\RegistryX\\Machine", 0, &other ) == HORIZON_REG_NAME_NOT_FOUND );
    assert( open_key( &reg, NULL, "\\Machine", 0, &other ) == HORIZON_REG_NAME_NOT_FOUND );
    assert( open_key( &reg, NULL, "\\Registry\\Machine\\Software\\Nope", 0, &other ) == HORIZON_REG_NAME_NOT_FOUND );
    assert( open_key( &reg, NULL, "\\Registry\\Machine\\Nope\\Wine", 0, &other ) == HORIZON_REG_NAME_NOT_FOUND );

    /* Volatile keys have only volatile subkeys. */
    assert( create( &reg, software, "Volatile", HORIZON_REG_OPTION_VOLATILE, &other ) == HORIZON_REG_SUCCESS );
    assert( create( &reg, other, "Stable", 0, &third ) == HORIZON_REG_CHILD_MUST_BE_VOLATILE && !third );
    assert( create( &reg, other, "Volatile", HORIZON_REG_OPTION_VOLATILE, &third ) == HORIZON_REG_SUCCESS );
    assert( third->flags & HORIZON_REG_FLAG_VOLATILE );
    horizon_reg_release( &reg, third );
    horizon_reg_release( &reg, other );

    memset( long_name, 'a', 256 );
    long_name[256] = 0;
    assert( create( &reg, software, long_name, 0, &other ) == HORIZON_REG_SUCCESS );
    horizon_reg_release( &reg, other );
    long_name[256] = 'a';
    long_name[257] = 0;
    assert( create( &reg, software, long_name, 0, &other ) == HORIZON_REG_INVALID_PARAMETER );

    horizon_reg_release( &reg, key );
    horizon_reg_release( &reg, software );
    horizon_reg_release( &reg, reg.root );
}

static void test_key_information(void)
{
    struct horizon_reg reg;
    struct horizon_reg_key *machine, *software, *key;
    struct horizon_reg_key_info info;
    unsigned char data[256];
    unsigned int size, len, i;
    const unsigned short *class;
    static const char *const names[] = { "b", "A", "c" };

    init( &reg, &machine );
    assert( create( &reg, machine, "Software", 0, &software ) == HORIZON_REG_SUCCESS );
    for (i = 0; i < 3; i++)
    {
        unsigned int namelen;
        unsigned short name[4];

        memcpy( name, w( names[i], &namelen ), namelen );
        class = w( "MyClass", &len );
        assert( horizon_reg_create( &reg, software, name, namelen, 0, 0, i ? NULL : class, i ? 0 : len, &key ) ==
                HORIZON_REG_SUCCESS );
        horizon_reg_release( &reg, key );
    }

    /* Subkeys are sorted without case. */
    assert( horizon_reg_enum_key( software, 0, HORIZON_REG_KEY_BASIC, &info, data, sizeof(data), &size ) ==
            HORIZON_REG_SUCCESS );
    assert( same( data, size, "A" ) && info.namelen == 2 && info.total == 2 );
    assert( horizon_reg_enum_key( software, 1, HORIZON_REG_KEY_BASIC, &info, data, sizeof(data), &size ) ==
            HORIZON_REG_SUCCESS && same( data, size, "b" ) );
    assert( horizon_reg_enum_key( software, 2, HORIZON_REG_KEY_BASIC, &info, data, sizeof(data), &size ) ==
            HORIZON_REG_SUCCESS && same( data, size, "c" ) );
    assert( horizon_reg_enum_key( software, 3, HORIZON_REG_KEY_BASIC, &info, data, sizeof(data), &size ) ==
            HORIZON_REG_NO_MORE_ENTRIES );
    assert( horizon_reg_enum_key( software, -2, HORIZON_REG_KEY_BASIC, &info, data, sizeof(data), &size ) ==
            HORIZON_REG_NO_MORE_ENTRIES );

    /* Node information has the class after the name, and truncates. */
    assert( horizon_reg_enum_key( software, 1, HORIZON_REG_KEY_NODE, &info, data, sizeof(data), &size ) ==
            HORIZON_REG_SUCCESS );
    assert( info.total == 16 && size == 16 && info.namelen == 2 );
    assert( same( data, 2, "b" ) && same( data + 2, 14, "MyClass" ) );
    assert( horizon_reg_enum_key( software, 1, HORIZON_REG_KEY_NODE, &info, data, 2, &size ) ==
            HORIZON_REG_SUCCESS && size == 2 && info.namelen == 2 && info.total == 16 );
    assert( horizon_reg_enum_key( software, 1, HORIZON_REG_KEY_NODE, &info, data, 0, &size ) ==
            HORIZON_REG_SUCCESS && size == 0 && info.namelen == 0 && info.total == 16 );

    /* Full information counts subkeys and values, and returns only the class. */
    assert( set_sz( &reg, software, "Value", "12345" ) == HORIZON_REG_SUCCESS );
    assert( horizon_reg_set_value( &reg, software, NULL, 0, HORIZON_REG_DWORD, "\1\0\0\0", 4 ) == HORIZON_REG_SUCCESS );
    assert( horizon_reg_enum_key( software, -1, HORIZON_REG_KEY_FULL, &info, data, sizeof(data), &size ) ==
            HORIZON_REG_SUCCESS );
    assert( info.subkeys == 3 && info.max_subkey == 2 && info.max_class == 14 );
    assert( info.values == 2 && info.max_value == 10 && info.max_data == 10 );
    assert( info.total == 0 && size == 0 && info.namelen == 16 && info.modif == software->modif );
    assert( horizon_reg_enum_key( software, 1, HORIZON_REG_KEY_FULL, &info, data, sizeof(data), &size ) ==
            HORIZON_REG_SUCCESS && info.total == 14 && size == 14 && info.namelen == 0 );
    assert( same( data, size, "MyClass" ) );
    assert( horizon_reg_enum_key( software, -1, HORIZON_REG_KEY_CACHED, &info, data, sizeof(data), &size ) ==
            HORIZON_REG_SUCCESS && info.total == 0 && info.namelen == 16 && info.values == 2 );

    /* Name information is the full path. */
    assert( horizon_reg_enum_key( software, 1, HORIZON_REG_KEY_NAME, &info, data, sizeof(data), &size ) ==
            HORIZON_REG_SUCCESS );
    assert( same( data, size, "\\Registry\\Machine\\Software\\b" ) && info.namelen == size && info.total == size );
    assert( horizon_reg_enum_key( software, 1, HORIZON_REG_KEY_NAME, &info, data, 10, &size ) ==
            HORIZON_REG_SUCCESS && size == 10 && info.namelen == info.total && info.total == 56 );
    assert( horizon_reg_enum_key( software, -1, 7, &info, data, sizeof(data), &size ) == HORIZON_REG_INVALID_PARAMETER );

    horizon_reg_release( &reg, software );
    horizon_reg_release( &reg, reg.root );
}

static void test_values(void)
{
    struct horizon_reg reg;
    struct horizon_reg_key *machine, *key;
    unsigned char data[64];
    unsigned int total, size, namelen, len;
    unsigned short *long_name;
    long long modif;
    int type;

    init( &reg, &machine );
    assert( create( &reg, machine, "Values", 0, &key ) == HORIZON_REG_SUCCESS );
    assert( set_sz( &reg, key, "Name", "abc" ) == HORIZON_REG_SUCCESS );
    assert( horizon_reg_set_value( &reg, key, NULL, 0, HORIZON_REG_BINARY, "\1\2", 2 ) == HORIZON_REG_SUCCESS );

    assert( horizon_reg_get_value( key, w( "NAME", &len ), len, &type, &total, data, sizeof(data), &size ) ==
            HORIZON_REG_SUCCESS );
    assert( type == HORIZON_REG_SZ && total == 6 && size == 6 && same( data, size, "abc" ) );
    assert( horizon_reg_get_value( key, w( "Name", &len ), len, &type, &total, data, 4, &size ) ==
            HORIZON_REG_SUCCESS && total == 6 && size == 4 );
    assert( horizon_reg_get_value( key, w( "Nope", &len ), len, &type, &total, data, 4, &size ) ==
            HORIZON_REG_NAME_NOT_FOUND && type == -1 );

    /* Setting the same data leaves the key unmodified. */
    modif = key->modif;
    assert( set_sz( &reg, key, "name", "abc" ) == HORIZON_REG_SUCCESS && key->modif == modif );
    assert( set_sz( &reg, key, "name", "abd" ) == HORIZON_REG_SUCCESS && key->modif > modif );
    assert( key->value_count == 2 && same( key->values[1].name, key->values[1].namelen, "Name" ) );

    /* The default value sorts first. */
    namelen = 77;
    assert( horizon_reg_enum_value( key, 0, HORIZON_REG_VALUE_PARTIAL, &type, &total, &namelen, data, sizeof(data),
                                    &size ) == HORIZON_REG_SUCCESS );
    assert( type == HORIZON_REG_BINARY && total == 2 && size == 2 && namelen == 0 && !memcmp( data, "\1\2", 2 ) );
    namelen = 77;
    assert( horizon_reg_enum_value( key, 1, HORIZON_REG_VALUE_FULL, &type, &total, &namelen, data, sizeof(data),
                                    &size ) == HORIZON_REG_SUCCESS );
    assert( total == 14 && size == 14 && namelen == 8 && same( data, 8, "Name" ) && same( data + 8, 6, "abd" ) );
    assert( horizon_reg_enum_value( key, 1, HORIZON_REG_VALUE_FULL, &type, &total, &namelen, data, 6, &size ) ==
            HORIZON_REG_SUCCESS && size == 6 && namelen == 6 );
    namelen = 77;
    assert( horizon_reg_enum_value( key, 1, HORIZON_REG_VALUE_BASIC, &type, &total, &namelen, data, 0, &size ) ==
            HORIZON_REG_SUCCESS && size == 0 && namelen == 77 && total == 8 );
    assert( horizon_reg_enum_value( key, 2, HORIZON_REG_VALUE_BASIC, &type, &total, &namelen, data, 0, &size ) ==
            HORIZON_REG_NO_MORE_ENTRIES );
    assert( horizon_reg_enum_value( key, 0, 3, &type, &total, &namelen, data, 0, &size ) ==
            HORIZON_REG_INVALID_PARAMETER );

    assert( horizon_reg_delete_value( &reg, key, w( "NAME", &len ), len ) == HORIZON_REG_SUCCESS );
    assert( horizon_reg_delete_value( &reg, key, w( "NAME", &len ), len ) == HORIZON_REG_NAME_NOT_FOUND );
    assert( key->value_count == 1 );

    long_name = calloc( 16384, sizeof(*long_name) );
    assert( long_name );
    assert( horizon_reg_set_value( &reg, key, long_name, 16384 * 2, HORIZON_REG_NONE, NULL, 0 ) ==
            HORIZON_REG_NAME_TOO_LONG );
    free( long_name );

    horizon_reg_release( &reg, key );
    horizon_reg_release( &reg, reg.root );
}

static void test_delete_and_rename(void)
{
    struct horizon_reg reg;
    struct horizon_reg_key *machine, *parent, *child, *other, *keys[3];
    struct horizon_reg_key_info info;
    unsigned char data[64];
    unsigned int size, len, i;
    static const char *const names[] = { "Alpha", "Beta", "Gamma" };

    init( &reg, &machine );
    assert( create( &reg, machine, "Parent", 0, &parent ) == HORIZON_REG_SUCCESS );
    assert( create( &reg, parent, "Child", 0, &child ) == HORIZON_REG_SUCCESS );
    assert( horizon_reg_delete( &reg, parent ) == HORIZON_REG_ACCESS_DENIED );
    assert( horizon_reg_delete( &reg, reg.root ) == HORIZON_REG_ACCESS_DENIED );
    assert( horizon_reg_delete( &reg, child ) == HORIZON_REG_SUCCESS );
    assert( (child->flags & HORIZON_REG_FLAG_DELETED) && child->refs == 1 && !parent->subkey_count );
    assert( open_key( &reg, child, "", 0, &other ) == HORIZON_REG_KEY_DELETED );
    assert( open_key( &reg, machine, "Parent\\Child", 0, &other ) == HORIZON_REG_NAME_NOT_FOUND );
    assert( horizon_reg_delete( &reg, child ) == HORIZON_REG_SUCCESS );
    horizon_reg_release( &reg, child );

    for (i = 0; i < 3; i++) assert( create( &reg, parent, names[i], 0, &keys[i] ) == HORIZON_REG_SUCCESS );
    assert( horizon_reg_rename( &reg, keys[0], w( "Zeta", &len ), len ) == HORIZON_REG_SUCCESS );
    assert( horizon_reg_rename( &reg, keys[2], w( "Aaa", &len ), len ) == HORIZON_REG_SUCCESS );
    assert( horizon_reg_enum_key( parent, 0, HORIZON_REG_KEY_BASIC, &info, data, sizeof(data), &size ) == 0 &&
            same( data, size, "Aaa" ) );
    assert( horizon_reg_enum_key( parent, 1, HORIZON_REG_KEY_BASIC, &info, data, sizeof(data), &size ) == 0 &&
            same( data, size, "Beta" ) );
    assert( horizon_reg_enum_key( parent, 2, HORIZON_REG_KEY_BASIC, &info, data, sizeof(data), &size ) == 0 &&
            same( data, size, "Zeta" ) );
    assert( horizon_reg_rename( &reg, keys[0], w( "beta", &len ), len ) == HORIZON_REG_CANNOT_DELETE );
    assert( horizon_reg_rename( &reg, keys[0], w( "a\\b", &len ), len ) == HORIZON_REG_INVALID_PARAMETER );
    assert( horizon_reg_rename( &reg, keys[0], w( "", &len ), len ) == HORIZON_REG_INVALID_PARAMETER );
    assert( horizon_reg_rename( &reg, reg.root, w( "Other", &len ), len ) == HORIZON_REG_CANNOT_DELETE );
    assert( open_key( &reg, machine, "Parent\\ZETA", 0, &other ) == HORIZON_REG_SUCCESS && other == keys[0] );
    horizon_reg_release( &reg, other );

    for (i = 0; i < 3; i++) horizon_reg_release( &reg, keys[i] );
    horizon_reg_release( &reg, parent );
    horizon_reg_release( &reg, reg.root );
}

static void test_notifications(void)
{
    struct horizon_reg reg;
    struct horizon_reg_key *machine, *parent, *child, *grandchild;
    struct fake_event events[5] = {{0}};

    init( &reg, &machine );
    assert( create( &reg, machine, "Parent", 0, &parent ) == HORIZON_REG_SUCCESS );
    assert( create( &reg, parent, "Child", 0, &child ) == HORIZON_REG_SUCCESS );
    assert( horizon_reg_notify( child, 0x10, &events[0], 0, HORIZON_REG_CHANGE_LAST_SET ) == HORIZON_REG_PENDING );
    assert( horizon_reg_notify( parent, 0x20, &events[1], 0, HORIZON_REG_CHANGE_LAST_SET ) == HORIZON_REG_PENDING );
    assert( horizon_reg_notify( parent, 0x24, &events[2], 1, HORIZON_REG_CHANGE_NAME ) == HORIZON_REG_PENDING );
    assert( horizon_reg_notify( parent, 0x28, &events[3], 1, HORIZON_REG_CHANGE_LAST_SET ) == HORIZON_REG_PENDING );

    /* A value set on the child signals its watchers and the parent's subtree
     * watchers for that change, once. */
    assert( set_sz( &reg, child, "Value", "1" ) == HORIZON_REG_SUCCESS );
    assert( events[0].signaled == 1 && !events[1].signaled && !events[2].signaled && events[3].signaled == 1 );
    assert( set_sz( &reg, child, "Value", "2" ) == HORIZON_REG_SUCCESS );
    assert( events[0].signaled == 1 && events[3].signaled == 1 );

    /* A new subkey below the child changes the child's names. */
    assert( create( &reg, child, "Grandchild", 0, &grandchild ) == HORIZON_REG_SUCCESS );
    assert( events[2].signaled == 1 && !events[1].signaled );

    /* Closing the handle signals what it asked for. */
    horizon_reg_handle_closed( &reg, parent, 0x99 );
    assert( !events[1].signaled );
    horizon_reg_handle_closed( &reg, parent, 0x20 );
    assert( events[1].signaled == 1 && !parent->notify );

    /* So does freeing a deleted key. */
    assert( horizon_reg_notify( grandchild, 0x30, &events[4], 0, HORIZON_REG_CHANGE_NAME ) == HORIZON_REG_PENDING );
    assert( horizon_reg_delete( &reg, grandchild ) == HORIZON_REG_SUCCESS && !events[4].signaled );
    horizon_reg_release( &reg, grandchild );
    assert( events[4].signaled == 1 );

    horizon_reg_release( &reg, child );
    horizon_reg_release( &reg, parent );
    horizon_reg_release( &reg, reg.root );
}

static unsigned int set_link( struct horizon_reg *reg, struct horizon_reg_key *key, const char *target )
{
    unsigned int len, target_len;
    unsigned short name[32];
    const unsigned short *str = w( "SymbolicLinkValue", &len );

    memcpy( name, str, len );
    str = w( target, &target_len );
    return horizon_reg_set_value( reg, key, name, len, HORIZON_REG_LINK, str, target_len );
}

static void test_symbolic_links(void)
{
    struct horizon_reg reg;
    struct horizon_reg_key *machine, *control, *link, *key, *other;

    init( &reg, &machine );
    assert( (control = horizon_reg_create_ascii( &reg, machine, "System\\ControlSet001\\Control" )) );
    assert( create( &reg, NULL, "\\Registry\\Machine\\System\\CurrentControlSet", HORIZON_REG_OPTION_CREATE_LINK,
                    &link ) == HORIZON_REG_SUCCESS );
    assert( link->flags & HORIZON_REG_FLAG_SYMLINK );
    assert( set_sz( &reg, link, "Other", "x" ) == HORIZON_REG_ACCESS_DENIED );
    assert( set_link( &reg, link, "\\Registry\\Machine\\System\\ControlSet001" ) == HORIZON_REG_SUCCESS );

    assert( open_key( &reg, NULL, "\\Registry\\Machine\\System\\CurrentControlSet\\Control", 0, &key ) ==
            HORIZON_REG_SUCCESS && key == control );
    horizon_reg_release( &reg, key );
    assert( open_key( &reg, NULL, "\\Registry\\Machine\\System\\CurrentControlSet", 0, &key ) ==
            HORIZON_REG_SUCCESS && key == control->parent );
    horizon_reg_release( &reg, key );
    assert( open_key( &reg, NULL, "\\Registry\\Machine\\System\\CurrentControlSet", HORIZON_REG_OBJ_OPENLINK, &key ) ==
            HORIZON_REG_SUCCESS && key == link );
    horizon_reg_release( &reg, key );
    /* OBJ_OPENLINK applies to the last element only. */
    assert( open_key( &reg, machine, "System\\CurrentControlSet\\Control", HORIZON_REG_OBJ_OPENLINK, &key ) ==
            HORIZON_REG_SUCCESS && key == control );
    horizon_reg_release( &reg, key );

    /* Keys are created in the link's target. */
    assert( create( &reg, machine, "System\\CurrentControlSet\\Services", 0, &key ) == HORIZON_REG_SUCCESS );
    assert( key->parent == control->parent );
    horizon_reg_release( &reg, key );
    assert( create( &reg, machine, "System\\CurrentControlSet", HORIZON_REG_OPTION_CREATE_LINK, &key ) ==
            HORIZON_REG_NAME_COLLISION );

    /* A link without a target, and a link to itself. */
    assert( create( &reg, machine, "System\\Broken", HORIZON_REG_OPTION_CREATE_LINK, &other ) == HORIZON_REG_SUCCESS );
    assert( open_key( &reg, machine, "System\\Broken", 0, &key ) == HORIZON_REG_NAME_NOT_FOUND );
    assert( set_link( &reg, other, "\\Registry\\Machine\\System\\Broken" ) == HORIZON_REG_SUCCESS );
    assert( open_key( &reg, machine, "System\\Broken\\Control", 0, &key ) == HORIZON_REG_NAME_NOT_FOUND );
    horizon_reg_release( &reg, other );

    horizon_reg_release( &reg, link );
    horizon_reg_release( &reg, reg.root );
}

static void test_registry_files(void)
{
    static const char text[] =
        "WINE REGISTRY Version 2\r\n"
        ";; All keys relative to \\\\Machine\n"
        "\n"
        "#arch=win32\n"
        "\n"
        "[Software\\\\Classes\\\\CLSID\\\\{BCDE0395-E52F-467C-8E3D-C4579291692E}\\\\InprocServer32] 1700000000\n"
        "#time=1d9f0e0c0a0b0c0\n"
        "@=\"mmdevapi.dll\"\n"
        "\"ThreadingModel\"=\"Both\"\n"
        "\r\n"
        "[Software\\\\Wine\\\\Escapes]\n"
        "#class=\"Cls\"\n"
        "\"Quote\\\"d\"=str:\"a\\\\b\\\"c\\x41\\101\\n\"\n"
        "\"Utf8\"=\"caf\xc3\xa9 \xf0\x9f\x8e\xb5\"\n"
        "\"Expand\"=str(2):\"%SystemRoot%\"\n"
        "\"Multi\"=hex(7):41,00,00,00,42,00,00,00,\\\n"
        "  00,00\n"
        "\"Bin\"=hex:01,02, 03 ,ff\n"
        "\"Dword\"=dword:0000002a\n"
        "\"Bad\"=hex:zz\n"
        "garbage line\n"
        "[System\\\\CurrentControlSet]\n"
        "#link\n"
        "\"SymbolicLinkValue\"=hex(6):5c,00,52,00,65,00,67,00,69,00,73,00,74,00,72,00,79,00,5c,00,4d,00,61,00,63,00,"
        "68,00,69,00,6e,00,65,00,5c,00,53,00,79,00,73,00,74,00,65,00,6d,00,5c,00,43,00,6f,00,6e,00,74,00,72,00,6f,00,"
        "6c,00,53,00,65,00,74,00,30,00,30,00,31,00\n"
        "[System\\\\ControlSet001\\\\Control]\n"
        "\"Value\"=dword:1\n";
    static const unsigned short utf8[] = { 'c','a','f',0xe9,' ',0xd83c,0xdfb5,0 };
    static const unsigned short quoted[] = { 'a','\\','b','"','c','A','A','\n',0 };
    static const unsigned short quote_name[] = { 'Q','u','o','t','e','"','d' };
    struct horizon_reg reg;
    struct horizon_reg_key *machine, *key;
    unsigned char data[64];
    unsigned int errors, total, size, len;
    int type;

    init( &reg, &machine );
    assert( horizon_reg_load( &reg, machine, "WINE REGISTRY Version 3\n", 24, &errors ) ==
            HORIZON_REG_NOT_REGISTRY_FILE );
    assert( horizon_reg_load( &reg, machine, text, sizeof(text) - 1, &errors ) == HORIZON_REG_SUCCESS );
    assert( errors == 2 );

    assert( open_key( &reg, machine, "Software\\Classes\\CLSID\\{bcde0395-e52f-467c-8e3d-c4579291692e}\\InprocServer32",
                      0, &key ) == HORIZON_REG_SUCCESS );
    assert( key->modif == 0x1d9f0e0c0a0b0c0 );
    assert( key->parent->modif == 1700000000LL * 10000000 + HORIZON_REG_TICKS_1601_TO_1970 );
    assert( horizon_reg_get_value( key, NULL, 0, &type, &total, data, sizeof(data), &size ) == 0 );
    assert( type == HORIZON_REG_SZ && size == 26 && !memcmp( data, w( "mmdevapi.dll", &len ), 26 - 2 ) &&
            !data[24] && !data[25] );
    assert( horizon_reg_get_value( key, w( "threadingmodel", &len ), len, &type, &total, data, sizeof(data), &size ) ==
            0 && size == 10 && same( data, 8, "Both" ) );
    horizon_reg_release( &reg, key );

    assert( open_key( &reg, machine, "Software\\Wine\\Escapes", 0, &key ) == HORIZON_REG_SUCCESS );
    assert( key->classlen == 8 && same( key->class, 6, "Cls" ) && !key->class[3] );
    assert( horizon_reg_get_value( key, quote_name, sizeof(quote_name), &type, &total, data, sizeof(data), &size ) ==
            0 && type == HORIZON_REG_SZ && size == sizeof(quoted) && !memcmp( data, quoted, size ) );
    assert( horizon_reg_get_value( key, w( "Utf8", &len ), len, &type, &total, data, sizeof(data), &size ) == 0 &&
            size == sizeof(utf8) && !memcmp( data, utf8, size ) );
    assert( horizon_reg_get_value( key, w( "Expand", &len ), len, &type, &total, data, sizeof(data), &size ) == 0 &&
            type == HORIZON_REG_EXPAND_SZ && size == 26 );
    assert( horizon_reg_get_value( key, w( "Multi", &len ), len, &type, &total, data, sizeof(data), &size ) == 0 &&
            type == HORIZON_REG_MULTI_SZ && size == 10 && !memcmp( data, "A\0\0\0B\0\0\0\0\0", 10 ) );
    assert( horizon_reg_get_value( key, w( "Bin", &len ), len, &type, &total, data, sizeof(data), &size ) == 0 &&
            type == HORIZON_REG_BINARY && size == 4 && !memcmp( data, "\1\2\3\xff", 4 ) );
    assert( horizon_reg_get_value( key, w( "Dword", &len ), len, &type, &total, data, sizeof(data), &size ) == 0 &&
            type == HORIZON_REG_DWORD && size == 4 && !memcmp( data, "\x2a\0\0\0", 4 ) );
    assert( horizon_reg_get_value( key, w( "Bad", &len ), len, &type, &total, data, sizeof(data), &size ) == 0 &&
            type == HORIZON_REG_NONE && size == 0 );
    horizon_reg_release( &reg, key );

    assert( open_key( &reg, machine, "System\\CurrentControlSet\\Control", 0, &key ) == HORIZON_REG_SUCCESS );
    assert( horizon_reg_get_value( key, w( "Value", &len ), len, &type, &total, data, sizeof(data), &size ) == 0 &&
            type == HORIZON_REG_DWORD && !memcmp( data, "\1\0\0\0", 4 ) );
    horizon_reg_release( &reg, key );

    horizon_reg_release( &reg, reg.root );
}

static unsigned int count_keys( const struct horizon_reg_key *key )
{
    unsigned int i, count = 1;

    for (i = 0; i < key->subkey_count; i++) count += count_keys( key->subkeys[i] );
    return count;
}

/* A system.reg written by Wine loads without errors. */
static void test_wine_file( const char *path )
{
    struct horizon_reg reg;
    struct horizon_reg_key *machine, *key;
    unsigned int errors;
    char *text;
    long size;
    FILE *f = fopen( path, "rb" );

    assert( f && !fseek( f, 0, SEEK_END ) && (size = ftell( f )) > 0 && !fseek( f, 0, SEEK_SET ) );
    assert( (text = malloc( size )) && fread( text, 1, size, f ) == (size_t)size );
    fclose( f );

    init( &reg, &machine );
    assert( horizon_reg_load( &reg, machine, text, size, &errors ) == HORIZON_REG_SUCCESS );
    assert( !errors );
    assert( open_key( &reg, machine, "Software\\Microsoft\\Windows NT\\CurrentVersion", 0, &key ) == 0 );
    horizon_reg_release( &reg, key );
    printf( "%s: %u keys\n", path, count_keys( machine ) );
    horizon_reg_release( &reg, reg.root );
    free( text );
}

int main( int argc, char **argv )
{
    test_paths();
    test_key_information();
    test_values();
    test_delete_and_rename();
    test_notifications();
    test_symbolic_links();
    test_registry_files();
    if (argc > 1) test_wine_file( argv[1] );
    puts( "Horizon registry: paths, key information, values, delete and rename, notifications, symbolic links "
          "and registry files passed" );
    return 0;
}
