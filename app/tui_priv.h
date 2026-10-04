#ifndef CHARTS_APP_TUI_INTERNAL_H
#define CHARTS_APP_TUI_INTERNAL_H
#include "tui.h"
#include "../lib/render.h"
#include "platform.h"
/* The presenter owns all mutable UI state; no renderer globals. */
typedef struct tui_line { lc_codepoint *chars; size_t count,capacity,at; } tui_line;
enum tui_mode { TUI_VIEW,TUI_SHEET,TUI_ANNOTATE,TUI_HELP,TUI_OVERVIEW };
typedef struct tui_state {
    app_deck *deck; app_present_opts options; app_spec cli; app_load_opts load;
    app_display *display; lc_context *ctx,*work; app_error error;
    enum tui_mode mode; int slide,focus,quit,dirty,notes;
    char *message,*theme; int message_bad,message_ttl; char digits[5];
    int row,col,top,left,editing; tui_line edit;
    app_dataset *undo; size_t undo_count,undo_capacity;
    int cur_series,cur_index,pick;
    const char *dialog_title,*dialog_label,*dialog_keys;
    const tui_line *dialog_line; tui_line *dialog_lines;
    size_t dialog_count,dialog_row;
} tui_state;
void tui_say(tui_state *p,const char *message,int bad);
app_slide *tui_slide(tui_state *p);
app_block *tui_focused(tui_state *p);
app_block *tui_target(tui_state *p,int chart_only);
int tui_sheet_ok(tui_state *p,app_block *block);
int tui_annotate_ok(tui_state *p,app_block *block);
const char *tui_cell_text(tui_state *p,lc_context *ctx,const app_dataset *data,int row,int col,int raw);
char *tui_line_text(lc_context *ctx,const tui_line *line);
void tui_line_draw(const tui_line *line,lc_scene *sc,int x,int y,int w,int fg,int bg);
void tui_draw(tui_state *p);
#endif
