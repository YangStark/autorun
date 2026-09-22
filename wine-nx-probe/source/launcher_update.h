#ifndef LAUNCHER_UPDATE_H
#define LAUNCHER_UPDATE_H

struct ui;
struct launcher_update;
struct launcher_update *launcher_update_create( struct ui *ui, const char *runtime_dir,
                                                int (*restart)(void) );
void launcher_update_tick( void *update );
void launcher_update_open( struct launcher_update *update );
void launcher_update_destroy( struct launcher_update *update );

#endif
