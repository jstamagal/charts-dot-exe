#ifndef CHARTS_APP_PLATFORM_H
#define CHARTS_APP_PLATFORM_H
#include "charts.h"
#include "model.h"
typedef struct app_display app_display;
typedef struct app_raw { void *saved; int fd; } app_raw;
int app_is_tty(int fd);
void app_term_size(int *cols,int *rows);
int app_raw_enter(app_raw *raw,int fd);
void app_raw_leave(app_raw *raw);
void app_read_key(int timeout_ms,char key[32]);
void app_cursor_show(int show);
void app_screen_clear(void);
void app_cursor_home(void);
void app_guard_install(void);
void app_guard_restore(void);
int app_guard_continued(void);
int app_gfx_valid(const char *name);
app_display *app_display_open(const char *want,int scale,int color,app_error *why);
const char *app_display_name(const app_display *display);
lc_mode app_display_mode(const app_display *display);
void app_display_grid(app_display *display,int *cols,int *rows);
lc_status app_display_show(app_display *display,lc_scene *scene);
int app_display_needs_redraw(app_display *display);
void app_display_invalidate(app_display *display);
void app_display_close(app_display *display);
int app_mkdir(const char *path,app_error *err);
int app_write_replace(const char *path,const char *data,size_t count,app_error *err);
int app_path_is_dir(const char *path);
int app_paths_in_dir(const char *path,char ***paths,size_t *count,app_error *err);
void app_paths_free(char **paths,size_t count);
app_stamp app_file_stamp(const char *path);
int app_launch_file(const char *output,const char *deck_path,const char *executable,char **created_path,app_error *err);
#endif
