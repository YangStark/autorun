/* Host test for the launcher's program list (source/launcher_list.h). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "../source/launcher_list.h"

static void test_names(void)
{
    assert( launcher_is_exe( "notepad.exe" ) && launcher_is_exe( "7ZR.EXE" ) && launcher_is_exe( "a.Exe" ) );
    assert( !launcher_is_exe( ".exe" ) && !launcher_is_exe( "._notepad.exe" ) );  /* macOS resource forks */
    assert( !launcher_is_exe( "notepad.exe.txt" ) && !launcher_is_exe( "readme" ) && !launcher_is_exe( "x.dll" ) );
}

static void test_args(void)
{
    assert( launcher_args_match( "C:\\notepad.exe C:\\notepad-test.txt", "C:\\notepad.exe" ) );
    assert( launcher_args_match( "c:\\7zr.exe b 1 -mmt2 -md18", "C:\\7zr.exe" ) );
    assert( launcher_args_match( "  \"C:\\Program Files\\app.exe\" -x", "C:\\Program Files\\app.exe" ) );
    assert( launcher_args_match( "C:\\pe32-timers.exe", "C:\\pe32-timers.exe" ) );
    assert( !launcher_args_match( "C:\\notepad.exe C:\\notepad-test.txt", "C:\\pe32-timers.exe" ) );
    assert( !launcher_args_match( "C:\\notepad.exe2 x", "C:\\notepad.exe" ) );
    assert( !launcher_args_match( "\"C:\\notepad.exe x", "C:\\notepad.exe" ) );
}

static void test_program_args(void)
{
    char path[64], line[64];

    assert( launcher_args_path( "sdmc:/switch/wine/drive_c/openttd/openttd.exe", path, sizeof(path) ) );
    assert( !strcmp( path, "sdmc:/switch/wine/drive_c/openttd/openttd.args.txt" ) );
    assert( launcher_args_path( "sdmc:/x/APP.EXE", path, sizeof(path) ) && !strcmp( path, "sdmc:/x/APP.args.txt" ) );
    assert( !launcher_args_path( "sdmc:/x/readme.txt", path, sizeof(path) ) );
    assert( !launcher_args_path( "sdmc:/switch/wine/drive_c/openttd/openttd.exe", path, 20 ) );
    assert( launcher_keys_path( "sdmc:/x/SPEED2.EXE", path, sizeof(path) ) && !strcmp( path, "sdmc:/x/SPEED2.keys.txt" ) );
    assert( !launcher_keys_path( "sdmc:/x/a.exe", path, 18 ) );  /* sdmc:/x/a.keys.txt needs 19 */
    assert( launcher_keys_path( "sdmc:/x/a.exe", path, 19 ) && !strcmp( path, "sdmc:/x/a.keys.txt" ) );
    assert( !launcher_keys_path( "sdmc:/x/readme.txt", path, sizeof(path) ) );
    assert( launcher_command_line( "C:\\openttd\\openttd.exe", "-s null -m null", line, sizeof(line) ) );
    assert( !strcmp( line, "C:\\openttd\\openttd.exe -s null -m null" ) );
    assert( launcher_command_line( "C:\\Program Files\\a.exe", "-x", line, sizeof(line) ) );
    assert( !strcmp( line, "\"C:\\Program Files\\a.exe\" -x" ) );
    assert( !launcher_command_line( "C:\\openttd\\openttd.exe", "-v win32:no_threads -s null -m null -r 1280x720", line, 40 ) );
}

static void test_order_and_find(void)
{
    struct launcher_entry entries[4] = {
        { "sdmc:/switch/wine/drive_c/pe32-timers.exe", "C:\\pe32-timers.exe", 0x14c },
        { "sdmc:/switch/wine/drive_c/curl/curl.exe", "C:\\curl\\curl.exe", 0xaa64 },
        { "sdmc:/switch/wine/drive_c/Notepad.exe", "C:\\Notepad.exe", 0x14c },
        { "sdmc:/switch/wine/drive_c/7zr.exe", "C:\\7zr.exe", 0x14c },
    };

    qsort( entries, 4, sizeof(entries[0]), launcher_compare );
    assert( !strcmp( entries[0].dos, "C:\\7zr.exe" ) && !strcmp( entries[1].dos, "C:\\curl\\curl.exe" ) );
    assert( !strcmp( entries[2].dos, "C:\\Notepad.exe" ) && !strcmp( entries[3].dos, "C:\\pe32-timers.exe" ) );
    assert( launcher_find( entries, 4, "sdmc:/switch/wine/drive_c/notepad.exe" ) == 2 );
    assert( launcher_find( entries, 4, "C:\\PE32-TIMERS.EXE" ) == 3 );
    assert( launcher_find( entries, 4, "sdmc:/switch/wine/drive_c/gone.exe" ) == 0 );
}

static void test_scrolling(void)
{
    /* 100 programs, 36 rows. */
    assert( launcher_first_visible( 0, 0, 100, 36 ) == 0 );
    assert( launcher_first_visible( 0, 35, 100, 36 ) == 0 );
    assert( launcher_first_visible( 0, 36, 100, 36 ) == 1 );    /* one step past the bottom */
    assert( launcher_first_visible( 10, 20, 100, 36 ) == 10 );  /* inside: unchanged */
    assert( launcher_first_visible( 10, 5, 100, 36 ) == 5 );
    assert( launcher_first_visible( 0, 99, 100, 36 ) == 64 );   /* the last page */
    assert( launcher_first_visible( 80, 99, 100, 36 ) == 64 );
    /* Fewer programs than rows. */
    assert( launcher_first_visible( 3, 2, 5, 36 ) == 0 );
    assert( launcher_first_visible( 0, 0, 0, 36 ) == 0 );
}

int main(void)
{
    test_names();
    test_args();
    test_program_args();
    test_order_and_find();
    test_scrolling();
    puts( "launcher list: program names, args.txt matching, program argument files, order, preselection and "
          "scrolling passed" );
    return 0;
}
