/* Wine server protocol 931 registry wire layouts; see include/wine/server_protocol.h. */
struct horizon_create_key_request
{
    struct horizon_server_request_header header;
    unsigned int access;
    unsigned int options;
    /* VARARG(objattr,object_attributes); */
    /* VARARG(class,unicode_str); */
    char __pad_20[4];
};
struct horizon_create_key_reply
{
    struct horizon_server_reply_header header;
    unsigned int hkey;
    char __pad_12[4];
};
struct horizon_open_key_request
{
    struct horizon_server_request_header header;
    unsigned int parent;
    unsigned int access;
    unsigned int attributes;
    /* VARARG(name,unicode_str); */
};
struct horizon_open_key_reply
{
    struct horizon_server_reply_header header;
    unsigned int hkey;
    char __pad_12[4];
};
struct horizon_delete_key_request
{
    struct horizon_server_request_header header;
    unsigned int hkey;
};
struct horizon_delete_key_reply
{
    struct horizon_server_reply_header header;
};
struct horizon_enum_key_request
{
    struct horizon_server_request_header header;
    unsigned int hkey;
    int          index;
    int          info_class;
};
struct horizon_enum_key_reply
{
    struct horizon_server_reply_header header;
    int          subkeys;
    int          max_subkey;
    int          max_class;
    int          values;
    int          max_value;
    int          max_data;
    long long    modif;
    unsigned int  total;
    unsigned int  namelen;
    /* VARARG(name,unicode_str,namelen); */
    /* VARARG(class,unicode_str); */
};
struct horizon_set_key_value_request
{
    struct horizon_server_request_header header;
    unsigned int hkey;
    int          type;
    unsigned int  namelen;
    /* VARARG(name,unicode_str,namelen); */
    /* VARARG(data,bytes); */
};
struct horizon_set_key_value_reply
{
    struct horizon_server_reply_header header;
};
struct horizon_get_key_value_request
{
    struct horizon_server_request_header header;
    unsigned int hkey;
    /* VARARG(name,unicode_str); */
};
struct horizon_get_key_value_reply
{
    struct horizon_server_reply_header header;
    int          type;
    unsigned int  total;
    /* VARARG(data,bytes); */
};
struct horizon_enum_key_value_request
{
    struct horizon_server_request_header header;
    unsigned int hkey;
    int          index;
    int          info_class;
};
struct horizon_enum_key_value_reply
{
    struct horizon_server_reply_header header;
    int          type;
    unsigned int  total;
    unsigned int  namelen;
    /* VARARG(name,unicode_str,namelen); */
    /* VARARG(data,bytes); */
    char __pad_20[4];
};
struct horizon_delete_key_value_request
{
    struct horizon_server_request_header header;
    unsigned int hkey;
    /* VARARG(name,unicode_str); */
};
struct horizon_delete_key_value_reply
{
    struct horizon_server_reply_header header;
};
struct horizon_set_registry_notification_request
{
    struct horizon_server_request_header header;
    unsigned int hkey;
    unsigned int event;
    int          subtree;
    unsigned int filter;
    char __pad_28[4];
};
struct horizon_set_registry_notification_reply
{
    struct horizon_server_reply_header header;
};
struct horizon_rename_key_request
{
    struct horizon_server_request_header header;
    unsigned int hkey;
    /* VARARG(name,unicode_str); */
};
struct horizon_rename_key_reply
{
    struct horizon_server_reply_header header;
};
