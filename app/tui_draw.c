/* Presenter drawing, kept separate from edit ownership and key dispatch. */
#include "tui_priv.h"
#include "bridge.h"
#define MIN LC_MIN
#define MAX LC_MAX
#define TXT(s) ((s)?(s):"")
#define EMPTY(s) (!(s)||!(s)[0])
#define BOTTOM(r) ((r).y+(r).h-1)
static const char *pad(lc_scene *sc,const char *text,int width,int left) {
    size_t n,spaces; char *out; text=TXT(text); n=strlen(text); spaces=(size_t)MAX(0,width-(int)lc_text_width(text));
    if (n>(size_t)-1-spaces-1) return "";
    out=(char *)lc_render_array(sc,n+spaces+1,1); if (!out) return "";
    if (left) { memset(out,' ',spaces); memcpy(out+spaces,text,n); } else { memcpy(out,text,n); memset(out+n,' ',spaces); }
    return out;
}
static lc_rect dialog_box(lc_scene *sc,int w,int h,const char *title) {
    int x,y; w=MIN(w,sc->canvas.w-4); h=MIN(h,sc->canvas.h-2); x=(sc->canvas.w-w)/2; y=(sc->canvas.h-h)/2;
    lc_scene_cover(sc,lc_rect_make(x,y,w+2,h+1)); lc_canvas_shadow(&sc->canvas,x,y,w,h); lc_canvas_fill_bg(&sc->canvas,x,y,w,h,7);
    lc_canvas_box(&sc->canvas,x,y,w,h,sc->mode.ascii ? LC_BOX_ASCII : LC_BOX_DOUBLE,0);
    if (!EMPTY(title)) lc_canvas_text_c(&sc->canvas,x+2,y,w-4,lc_render_join(sc," ",title," "),0);
    return lc_rect_make(x+2,y+1,w-4,h-2);
}
static void draw_help(lc_scene *sc) {
    static const char *const lines[]={
        "\342\206\220 \342\206\222  space  pgup pgdn     previous / next slide",
        "home  end  12 enter         first / last / go to slide 12",
        "o                           overview: pick a slide from a list",
        "n                           speaker notes",
        "tab  shift-tab              focus the next / previous block", "",
        "e                           edit: the data sheet of a chart, or a text block",
        "a                           annotate the chart: callouts, target lines",
        "t T   p P                   chart type, palette",
        "v  l  g                     values, legend, grid",
        "d D   x                     3-D depth, pop a pie slice out", "",
        "E                           edit the slide title",
        "i   c                       add a text block / a chart to this slide",
        "N   X                       new slide after this one / delete the focused block",
        "y                           next theme", "",
        "s                           save the deck and any edited data",
        "r                           reload from disk, dropping unsaved edits", "q                           quit"
    };
    int n=(int)(sizeof lines/sizeof lines[0]),i; lc_rect in; const char *p,*start; char *keys; size_t j,bytes;
    in=dialog_box(sc,78,n+4,"keys");
    for (i=0;i<n && i<in.h;i++) {
        p=start=lines[i]; for (j=0;j<28 && *p;j++) lc_utf8_next(&p); bytes=(size_t)(p-start);
        keys=(char *)lc_render_array(sc,bytes+1,1); if (!keys) return; memcpy(keys,start,bytes);
        lc_canvas_text(&sc->canvas,in.x+1,in.y+1+i,keys,4,7);
        lc_canvas_text(&sc->canvas,in.x+1+(int)lc_text_width(keys),in.y+1+i,lc_text_truncate(sc->ctx,p,(size_t)MAX(0,in.w-29)),0,7);
    }
}
static void draw_overview(tui_state *p,lc_scene *sc) {
    int n=(int)p->deck->slide_count,h=MIN(n+4,sc->canvas.h-4),room,first,i,k; lc_rect in; const char *title,*line; app_slide *s;
    in=dialog_box(sc,70,h,EMPTY(p->deck->title) ? "slides" : p->deck->title); room=in.h-1; first=MAX(0,MIN(p->pick-room/2,n-room));
    for (i=0;i<room && first+i<n;i++) {
        k=first+i; s=p->deck->slides+k; title=EMPTY(s->title) ? "(untitled)" : s->title;
        line=lc_render_join(sc,pad(sc,lc_render_uint(sc,(size_t)k+1),3,1),"  ",title);
        line=pad(sc,lc_text_truncate(sc->ctx,line,(size_t)MAX(0,in.w)),in.w,0);
        lc_canvas_text(&sc->canvas,in.x,in.y+i,line,k==p->pick ? 15 : 0,k==p->pick ? 4 : 7);
    }
    lc_canvas_text(&sc->canvas,in.x,BOTTOM(in),lc_text_truncate(sc->ctx,"\342\206\221\342\206\223 pick   enter go   esc close",(size_t)MAX(0,in.w)),8,7);
}
static void draw_dialog(tui_state *p,lc_scene *sc) {
    int h,room,i; lc_rect in; size_t first,k; const char *text;
    if (p->dialog_lines) {
        h=MIN(sc->canvas.h-4,MAX(10,(int)p->dialog_count+6)); in=dialog_box(sc,MIN(sc->canvas.w-6,90),h,p->dialog_title); room=MAX(1,in.h-2);
        first=p->dialog_row>=(size_t)room ? p->dialog_row-(size_t)room+1 : 0;
        for (i=0;i<room;i++) {
            k=first+(size_t)i;
            if (k>=p->dialog_count) { lc_canvas_text(&sc->canvas,in.x,in.y+i,pad(sc,"",in.w,0),15,4); continue; }
            if (k==p->dialog_row) tui_line_draw(p->dialog_lines+k,sc,in.x,in.y+i,in.w,15,4);
            else { text=tui_line_text(sc->ctx,p->dialog_lines+k); lc_canvas_text(&sc->canvas,in.x,in.y+i,pad(sc,lc_text_truncate(sc->ctx,text,(size_t)MAX(0,in.w)),in.w,0),15,4); }
        }
        lc_canvas_text(&sc->canvas,in.x,BOTTOM(in),lc_text_truncate(sc->ctx,p->dialog_keys,(size_t)MAX(0,in.w)),8,7); return;
    }
    in=dialog_box(sc,MIN(sc->canvas.w-6,72),7,p->dialog_title);
    lc_canvas_text(&sc->canvas,in.x,in.y,lc_text_truncate(sc->ctx,p->dialog_label,(size_t)MAX(0,in.w)),0,7);
    tui_line_draw(p->dialog_line,sc,in.x,in.y+2,in.w,15,4);
    lc_canvas_text(&sc->canvas,in.x,BOTTOM(in),lc_text_truncate(sc->ctx,p->dialog_keys,(size_t)MAX(0,in.w)),8,7);
}
static const char *cursor_text(tui_state *p,lc_scene *sc,const app_block *b) {
    const app_dataset *d=&b->data; size_t i=(size_t)MAX(0,p->cur_index),s=(size_t)MAX(0,p->cur_series); const char *label,*value;
    if (!d->series_count || !app_data_rows(d)) return "";
    label=i<d->label_count ? d->labels[i] : lc_render_uint(sc,i+1); if (s>=d->series_count) return label;
    value=tui_cell_text(p,sc->ctx,d,(int)i,(int)s+1,0); if (EMPTY(value)) value="-";
    return lc_render_join(sc,lc_render_join(sc,label," \302\267 ",d->series[s].name)," = ",value);
}
static void sheet_colors(lc_scene *sc,app_dataset *d,const lc_chart_options *o) {
    size_t i,j,pos=0,count=0,colorpos; int used,pin;
    for (i=0;i<d->series_count;i++) { used=0; for (j=0;j<o->error_count;j++) if (lc_name_eq(d->series[i].name,o->errors[j].lo) || (!o->errors[j].plus_minus && lc_name_eq(d->series[i].name,o->errors[j].hi))) used=1; if (!used) count++; }
    for (i=0;i<d->series_count;i++) {
        if (!EMPTY(d->series[i].name) && d->series[i].name[0]=='\001') continue;
        used=0; for (j=0;j<o->error_count;j++) if (lc_name_eq(d->series[i].name,o->errors[j].lo) || (!o->errors[j].plus_minus && lc_name_eq(d->series[i].name,o->errors[j].hi))) used=1;
        if (used) continue;
        pin=-1; colorpos=0;
        for (j=0;j<o->color_count;j++) { if (EMPTY(o->colors[j].name)) { if (colorpos++==pos) pin=o->colors[j].color; } else if (lc_name_eq(o->colors[j].name,d->series[i].name)) { pin=o->colors[j].color; break; } }
        d->series[i].color=pin>=0 ? pin : lc_chart_palette(sc,o->palette,pos,count); pos++;
    }
}
static void sheet_cell(tui_state *p,lc_scene *sc,app_dataset *d,const int *width,int row,int col,int x,int y,int light) {
    int cw=width[col],cur=row==p->row && col==p->col,fg; const char *text;
    if (cur && p->editing) { tui_line_draw(&p->edit,sc,x,y,cw-1,15,4); return; }
    text=lc_text_truncate(sc->ctx,tui_cell_text(p,sc->ctx,d,row,col,0),(size_t)(cw-1)); text=pad(sc,text,cw-1,col!=0);
    fg=row<0 ? sc->skin.table_head : (!col ? sc->skin.label : sc->skin.table_row);
    if (row<0 && col>0 && !light) fg=d->series[col-1].color;
    lc_canvas_text(&sc->canvas,x,y,text,cur ? sc->skin.cursor_fg : fg,cur ? sc->skin.cursor_bg : -1);
}
static void sheet_row(tui_state *p,lc_scene *sc,app_dataset *d,const int *width,lc_rect in,int row,int y,int light) {
    int x=in.x,c; lc_codepoint rule=sc->mode.ascii ? '|' : 0x2502;
    sheet_cell(p,sc,d,width,row,0,x,y,light); x+=width[0];
    for (c=p->left+1;c<(int)d->series_count+1;c++) {
        if (x+width[c]>in.x+in.w) { lc_canvas_put(&sc->canvas,in.x+in.w-1,y,sc->mode.ascii ? '>' : 0x25ba,sc->skin.dim,-1); break; }
        lc_canvas_put(&sc->canvas,x-1,y,rule,sc->skin.table_rule,-1); sheet_cell(p,sc,d,width,row,c,x,y,light); x+=width[c];
    }
    if (x<=in.x+in.w) lc_canvas_put(&sc->canvas,x-1,y,rule,sc->skin.table_rule,-1);
}
static void draw_sheet(tui_state *p,lc_scene *sc,lc_slide_view *view) {
    app_block *b=tui_focused(p); app_dataset *d=&b->data; int W=sc->canvas.w,H=sc->canvas.h,R=(int)app_data_rows(d),C=(int)d->series_count+1;
    int sheet_h=MAX(7,MIN(R+4,H*2/5)),chart_h=H-1-sheet_h-1,y0,c,r,m,body_rows,used,last,light,i; int *width;
    lc_rect box,in; lc_dataset data; lc_chart_options o; const char *name,*hint,*message,*pos;
    if (app_block_options(sc->ctx,p->deck,b,&p->cli,&o)!=LC_OK || app_dataset_view(sc->ctx,d,&data)!=LC_OK) { lc_scene_fail(sc,LC_ENOMEM); return; }
    o.color=o.color && sc->mode.color; if (p->row>=0) { o.cur_index=p->row; o.cur_series=p->col>0 ? p->col-1 : -1; }
    if (chart_h>=8) lc_render_chart(sc,lc_rect_make(1,0,W-2-(sc->skin.slide_bg==LC_BG_NONE ? 0 : 2),chart_h),&data,&o);
    sheet_colors(sc,d,&o); y0=H-1-sheet_h; box=lc_rect_make(1,y0,W-2-(sc->skin.slide_bg==LC_BG_NONE ? 0 : 2),sheet_h);
    if (sc->skin.slide_bg!=LC_BG_NONE) { lc_canvas_shadow(&sc->canvas,box.x,box.y,box.w,box.h); lc_scene_panel(sc,box,sc->skin.panel_bg); }
    lc_canvas_box(&sc->canvas,box.x,box.y,box.w,box.h,sc->mode.ascii ? LC_BOX_ASCII : LC_BOX_SINGLE,sc->skin.accent);
    name=EMPTY(b->data_ref) ? "data (kept in the deck)" : b->data_ref; if (b->data_dirty) name=lc_render_join(sc,name," *","");
    lc_canvas_text(&sc->canvas,box.x+2,box.y,lc_render_join(sc," ",name," "),sc->skin.title,-1);
    if (d->lossy) lc_canvas_text_r(&sc->canvas,box.x+2,box.y,box.w-4,lc_render_join(sc," read-only on disk: ",d->lossy_why," "),9);
    width=(int *)lc_render_array(sc,(size_t)C,sizeof *width); if (!width) return;
    for (c=0;c<C;c++) { m=(int)lc_text_width(tui_cell_text(p,sc->ctx,d,-1,c,0)); for (r=0;r<R;r++) m=MAX(m,(int)lc_text_width(tui_cell_text(p,sc->ctx,d,r,c,0))); width[c]=MAX(6,MIN(m,c==0 ? 20 : 14))+2; }
    in=lc_rect_make(box.x+1,box.y+1,box.w-2,box.h-2); body_rows=in.h-1;
    if (p->row>=0) { if (p->row<p->top) p->top=p->row; if (p->row>=p->top+body_rows) p->top=p->row-body_rows+1; }
    p->top=MAX(0,MIN(p->top,MAX(0,R-body_rows))); if (p->col>0 && p->col-1<p->left) p->left=p->col-1;
    for (;;) { used=width[0]; last=p->left; for (c=p->left+1;c<C && used+width[c]<=in.w;c++) { used+=width[c]; last=c; } if (p->col<=last || p->left>=C-2 || !p->col) break; p->left++; }
    light=lc_contrast_on(sc->skin.panel_bg)==0 && sc->skin.slide_bg!=LC_BG_NONE;
    sheet_row(p,sc,d,width,in,-1,in.y,light); for (i=0;i<body_rows && p->top+i<R;i++) sheet_row(p,sc,d,width,in,p->top+i,in.y+1+i,light);
    if (R>body_rows) { pos=lc_render_join(sc,lc_render_uint(sc,(size_t)MAX(0,p->row)+1),"/",lc_render_uint(sc,(size_t)R)); lc_canvas_text_r(&sc->canvas,box.x+2,BOTTOM(box),box.w-4,lc_render_join(sc," ",pos," "),sc->skin.dim); }
    hint=p->editing ? "enter ok  tab next  esc cancel" : "enter edit  ins row  ^D del row  ^A series  ^X del series  u undo  s save  esc back";
    message=view->message; if (EMPTY(message)) message=lc_render_join(sc,tui_cell_text(p,sc->ctx,d,-1,p->col,0),p->row>=0 ? " \302\267 " : " (header)",p->row>=0 ? tui_cell_text(p,sc->ctx,d,p->row,0,0) : "");
    lc_draw_status(sc,message,hint,"",view->message_bad);
}
void tui_draw(tui_state *p) {
    int cols,rows; lc_scene *sc; lc_slide_view v; app_block *f; lc_status status; const char *footer;
    app_display_grid(p->display,&cols,&rows); cols=MAX(cols,40); rows=MAX(rows,10); lc_scratch_reset(p->ctx);
    status=lc_scene_create(p->ctx,cols,rows,app_display_mode(p->display),&sc); if (status!=LC_OK) { tui_say(p,lc_status_string(status),1); return; }
    app_apply_theme(sc,p->deck,p->theme); lc_canvas_clear(&sc->canvas,sc->skin.text,sc->skin.slide_bg);
    f=tui_focused(p);
    if (p->mode==TUI_SHEET && f) tui_sheet_ok(p,f);
    lc_slide_view_init(&v); v.notes=p->notes; v.message=p->message; v.message_bad=p->message_bad;
    footer=EMPTY(p->deck->footer) ? p->deck->title : p->deck->footer;
    if (EMPTY(footer) && !EMPTY(p->deck->file)) { footer=strrchr(p->deck->file,'/'); footer=footer ? footer+1 : p->deck->file; }
    v.footer=footer;
    if (*p->digits) v.message=lc_render_join(sc,"go to slide ",p->digits,"  (enter)");
    if (p->mode==TUI_SHEET && f) draw_sheet(p,sc,&v);
    else {
        v.focus=f;
        if (p->mode==TUI_ANNOTATE && f && tui_annotate_ok(p,f)) { v.cur_series=p->cur_series; v.cur_index=p->cur_index; if (EMPTY(p->message)) v.message=cursor_text(p,sc,f); v.hint="\342\206\220\342\206\222 point  \342\206\221\342\206\223 series  enter note  h line  v mark  del remove  esc done"; }
        else v.hint=p->deck->slide_count>1 ? "\342\206\220\342\206\222 slide  tab focus  e edit  a annotate  t type  s save  ? help  q quit" : "e edit  a annotate  t type  p palette  v values  s save  ? help  q quit";
        status=app_render_deck(p->ctx,sc,p->deck,p->slide,&v,&p->cli,p->theme); lc_scene_fail(sc,status);
        if (p->mode==TUI_HELP) draw_help(sc);
        if (p->mode==TUI_OVERVIEW) draw_overview(p,sc);
    }
    if (p->dialog_line || p->dialog_lines) draw_dialog(p,sc);
    status=app_display_show(p->display,sc); if (status!=LC_OK) tui_say(p,lc_status_string(status),1);
    lc_scene_destroy(sc);
}
