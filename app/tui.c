/* Stateful presenter. All UI and edit buffers have one explicit owner. */
#include "tui_priv.h"
#include "platform.h"
#include "bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#define MIN LC_MIN
#define MAX LC_MAX
#define TXT(s) ((s)?(s):"")
#define EMPTY(s) (!(s)||!(s)[0])
#define KEY(s) (strcmp(k,(s))==0)

static const char *const chart_types[]={"bar","stacked","hbar","dumbbell","line","area","pie","pie3d","donut","scatter","hist","table"};
static const char *const palette_names[]={"dos","ega","cga","ice","fire","green","amber","mono"};
static const char *const theme_names[]={"dos","black","light"};
static char *copy_work(tui_state *p,const char *s) {
    size_t n=strlen(TXT(s)); char *out=(char *)lc_scratch(p->work,n+1);
    if (out) memcpy(out,TXT(s),n+1);
    return out;
}
static char *join(tui_state *p,const char *a,const char *b,const char *c) {
    size_t na=strlen(TXT(a)),nb=strlen(TXT(b)),nc=strlen(TXT(c)); char *out;
    if (na>(size_t)-1-nb || na+nb>(size_t)-1-nc-1) return NULL;
    out=(char *)lc_scratch(p->work,na+nb+nc+1); if (!out) return NULL;
    memcpy(out,TXT(a),na); memcpy(out+na,TXT(b),nb); memcpy(out+na+nb,TXT(c),nc+1); return out;
}
static char *number(tui_state *p,long n) { char b[3*sizeof(long)+3]; sprintf(b,"%ld",n); return copy_work(p,b); }
static int replace(tui_state *p,char **dest,const char *text) {
    char *s=app_strdup(TXT(text),&p->error); if (!s) { tui_say(p,p->error.message,1); return 0; }
    free(*dest); *dest=s; return 1;
}
static const char *base(const char *path) { const char *s=strrchr(TXT(path),'/'); return s ? s+1 : TXT(path); }
static int printable(const char *k) { return *k && (k[1] ? (unsigned char)k[0]>=0xc0 : (unsigned char)k[0]>=32); }
static void line_free(tui_line *line) { free(line->chars); memset(line,0,sizeof *line); }
static int line_reserve(tui_state *p,tui_line *line,size_t n) { return app_reserve((void **)&line->chars,&line->capacity,n,sizeof *line->chars,&p->error); }
static int line_set(tui_state *p,tui_line *line,const char *text) {
    const char *s=TXT(text); size_t n=strlen(s);
    if (!line_reserve(p,line,n+1)) return 0;
    line->count=0; while (*s) line->chars[line->count++]=lc_utf8_next(&s); line->at=line->count; return 1;
}
char *tui_line_text(lc_context *ctx,const tui_line *line) {
    char *s,bytes[5]; size_t i,n,used=0;
    if (line->count>((size_t)-1-1)/4) return NULL;
    s=(char *)lc_scratch(ctx,line->count*4+1); if (!s) return NULL;
    for (i=0;i<line->count;i++) { n=lc_utf8_encode(line->chars[i],bytes); memcpy(s+used,bytes,n); used+=n; }
    s[used]=0; return s;
}
static int line_key(tui_state *p,tui_line *line,const char *k) {
    const char *s;
    if (KEY("left")) { if (line->at) line->at--; }
    else if (KEY("right")) { if (line->at<line->count) line->at++; }
    else if (KEY("home")||KEY("ctrl-a")) line->at=0;
    else if (KEY("end")||KEY("ctrl-e")) line->at=line->count;
    else if (KEY("backspace")) { if (line->at) { line->at--; memmove(line->chars+line->at,line->chars+line->at+1,(line->count-line->at-1)*sizeof *line->chars); line->count--; } }
    else if (KEY("delete")) { if (line->at<line->count) { memmove(line->chars+line->at,line->chars+line->at+1,(line->count-line->at-1)*sizeof *line->chars); line->count--; } }
    else if (KEY("ctrl-u")) { if (line->count>line->at) memmove(line->chars,line->chars+line->at,(line->count-line->at)*sizeof *line->chars); line->count-=line->at; line->at=0; }
    else if (KEY("ctrl-k")) line->count=line->at;
    else if (printable(k)) {
        if (!line_reserve(p,line,line->count+1)) { tui_say(p,p->error.message,1); return 0; }
        memmove(line->chars+line->at+1,line->chars+line->at,(line->count-line->at)*sizeof *line->chars);
        s=k; line->chars[line->at++]=lc_utf8_next(&s); line->count++;
    } else return 0;
    return 1;
}
void tui_line_draw(const tui_line *line,lc_scene *sc,int x,int y,int w,int fg,int bg) {
    size_t first,k; int i,caret;
    if (w<1) return;
    first=line->at>=(size_t)w ? line->at-(size_t)w+1 : 0;
    for (i=0;i<w;i++) { k=first+(size_t)i; caret=k==line->at; lc_canvas_put(&sc->canvas,x+i,y,k<line->count ? line->chars[k] : ' ',caret ? sc->skin.cursor_fg : fg,caret ? sc->skin.cursor_bg : bg); }
}
void tui_say(tui_state *p,const char *message,int bad) {
    char *copy=app_strdup(TXT(message),&p->error);
    if (copy) { free(p->message); p->message=copy; }
    p->message_bad=bad; p->message_ttl=bad ? 40 : 14;
}
app_slide *tui_slide(tui_state *p) { return p->slide>=0 && (size_t)p->slide<p->deck->slide_count ? p->deck->slides+p->slide : NULL; }
static size_t leaf_count(app_block *b,size_t n) { size_t i,count=0; for (i=0;i<n;i++) count+=b[i].kind==APP_BLOCK_ROWS || b[i].kind==APP_BLOCK_COLS ? leaf_count(b[i].children,b[i].child_count) : 1; return count; }
static app_block *leaf_get(app_block *b,size_t n,size_t *index) {
    size_t i; app_block *found;
    for (i=0;i<n;i++) {
        if (b[i].kind==APP_BLOCK_ROWS || b[i].kind==APP_BLOCK_COLS) { found=leaf_get(b[i].children,b[i].child_count,index); if (found) return found; }
        else if (!*index) return b+i; else --*index;
    }
    return NULL;
}
app_block *tui_focused(tui_state *p) { app_slide *s=tui_slide(p); size_t index; if (!s || p->focus<0) return NULL; index=(size_t)p->focus; return leaf_get(s->blocks,s->block_count,&index); }
app_block *tui_target(tui_state *p,int chart_only) {
    app_block *b=tui_focused(p); app_slide *s; size_t i,n,k;
    if (b && (!chart_only || b->kind==APP_BLOCK_CHART)) return b;
    s=tui_slide(p); if (!s) return NULL;
    n=leaf_count(s->blocks,s->block_count);
    for (i=0;i<n;i++) { k=i; b=leaf_get(s->blocks,s->block_count,&k); if (b && (!chart_only || b->kind==APP_BLOCK_CHART)) { p->focus=(int)i; return b; } }
    return NULL;
}
static int block_dirty(const app_block *blocks,size_t n) { size_t i; for (i=0;i<n;i++) if (blocks[i].data_dirty || block_dirty(blocks[i].children,blocks[i].child_count)) return 1; return 0; }
static int data_dirty(tui_state *p) { size_t i; for (i=0;i<p->deck->slide_count;i++) if (block_dirty(p->deck->slides[i].blocks,p->deck->slides[i].block_count)) return 1; return 0; }
static int unsaved(tui_state *p) { return p->deck->dirty || data_dirty(p); }
static size_t error_count(tui_state *p) { size_t i,n=0; for (i=0;i<p->deck->issue_count;i++) if (p->deck->issues[i].error) n++; return n; }
static void go(tui_state *p,int n) { n=MAX(0,MIN(n,(int)p->deck->slide_count-1)); if (n!=p->slide) { p->slide=n; p->focus=-1; } }
static void reset_data(app_block *b,size_t n) { size_t i; for (i=0;i<n;i++) { if (b[i].kind==APP_BLOCK_CHART) { b[i].data_dirty=0; b[i].mtime.exists=-1; } reset_data(b[i].children,b[i].child_count); } }
static void reload(tui_state *p) {
    app_deck fresh; size_t i;
    if (p->deck->implicit || EMPTY(p->deck->file)) {
        for (i=0;i<p->deck->slide_count;i++) reset_data(p->deck->slides[i].blocks,p->deck->slides[i].block_count);
        p->error.code=0; app_deck_refresh(p->deck,&p->load,&p->error); tui_say(p,p->error.code ? p->error.message : "reloaded",p->error.code!=0); return;
    }
    app_deck_init(&fresh);
    if (app_deck_load_file(&fresh,p->deck->file,&p->load,&p->error)) {
        app_deck_free(p->deck); *p->deck=fresh; p->slide=MAX(0,MIN(p->slide,(int)p->deck->slide_count-1)); p->focus=-1; p->mode=TUI_VIEW;
        replace(p,&p->theme,TXT(p->options.theme));
        if (app_deck_ok(p->deck)) tui_say(p,"reloaded",0); else tui_say(p,join(p,number(p,(long)error_count(p))," problem(s) after reload: run charts --check",""),1);
    } else { app_deck_free(&fresh); p->deck->mtime=app_file_stamp(p->deck->file); tui_say(p,p->error.message,1); }
}
static void poll_files(tui_state *p) {
    app_stamp m; int changed;
    if (!EMPTY(p->deck->file) && !p->deck->implicit) {
        m=app_file_stamp(p->deck->file);
        if (!app_stamp_equal(&m,&p->deck->mtime) && m.exists) {
            if (unsaved(p)) { p->deck->mtime=m; tui_say(p,"the deck changed on disk; you have unsaved edits (r reloads and drops them)",1); }
            else reload(p);
            p->dirty=1;
        }
    }
    p->error.code=0; changed=app_deck_refresh(p->deck,&p->load,&p->error); if (changed) p->dirty=1;
}
static int save_blocks(tui_state *p,app_block *b,size_t n,const char **wrote) {
    size_t i;
    for (i=0;i<n;i++) {
        if (b[i].kind==APP_BLOCK_CHART && b[i].data_dirty) {
            if (!app_deck_store_data(p->deck,b+i,&p->error)) return 0;
            if (!EMPTY(b[i].data_file)) *wrote=join(p,*wrote,EMPTY(*wrote) ? "" : ", ",base(b[i].data_file));
        }
        if (!save_blocks(p,b[i].children,b[i].child_count,wrote)) return 0;
    }
    return 1;
}
static void save(tui_state *p) {
    const char *wrote=""; size_t i;
    for (i=0;i<p->deck->slide_count;i++) if (!save_blocks(p,p->deck->slides[i].blocks,p->deck->slides[i].block_count,&wrote)) { tui_say(p,p->error.message,1); return; }
    if (p->deck->dirty || p->deck->tweaked) {
        if (!app_deck_save(p->deck,&p->error)) { tui_say(p,p->error.message,1); return; }
        wrote=join(p,wrote,EMPTY(wrote) ? "" : ", ",base(p->deck->file));
    }
    tui_say(p,EMPTY(wrote) ? "nothing to save" : join(p,"saved ",wrote,""),0);
}
static int must_redraw(tui_state *p) { if (app_guard_continued()) { app_display_invalidate(p->display); return 1; } return app_display_needs_redraw(p->display); }
static void wait_key(tui_state *p,char k[32]) { do { if (must_redraw(p)) tui_draw(p); app_read_key(250,k); } while (!*k || KEY("unknown")); }
static int ask(tui_state *p,const char *title,const char *label,char **value) {
    tui_line line; char k[32],*text; int ok=0;
    memset(&line,0,sizeof line); if (!line_set(p,&line,TXT(*value))) return 0;
    p->dialog_title=title; p->dialog_label=label; p->dialog_keys="enter accept   esc cancel"; p->dialog_line=&line;
    for (;;) { tui_draw(p); wait_key(p,k); if (KEY("enter")) { ok=1; break; } if (KEY("esc")||KEY("ctrl-c")) break; line_key(p,&line,k); }
    p->dialog_line=NULL;
    if (ok) {
        text=tui_line_text(p->work,&line);
        if (!text) { tui_say(p,"out of memory",1); ok=0; }
        else ok=replace(p,value,text);
    }
    line_free(&line); return ok;
}
static int confirm(tui_state *p,const char *title,const char *label,const char *help,const char *keys) {
    tui_line line; char k[32]; int result=0;
    memset(&line,0,sizeof line); p->dialog_title=title; p->dialog_label=label; p->dialog_keys=help; p->dialog_line=&line;
    for (;;) { tui_draw(p); wait_key(p,k); if (KEY("esc")||KEY("ctrl-c")) break; if (*k && !k[1] && strchr(keys,*k)) { result=*k; break; } }
    p->dialog_line=NULL; return result;
}
static void free_lines(char **lines,size_t count) { size_t i; for (i=0;i<count;i++) free(lines[i]); free(lines); }
static int edit_text(tui_state *p,const char *title,char ***lines,size_t *count,size_t *capacity) {
    tui_line *ed=NULL,*cur,*prev,next; size_t n=0,cap=0,i,at,join_at,edit_count=0; char k[32]; char **result=NULL,*s,*trimmed; int ok=0;
    for (i=0;i<*count || !n;i++) {
        if (!app_reserve((void **)&ed,&cap,n+1,sizeof *ed,&p->error)) goto done;
        memset(ed+n,0,sizeof *ed); n++;
        if (i<*count && !line_set(p,ed+n-1,(*lines)[i])) goto done;
    }
    p->dialog_title=title; p->dialog_keys="enter new line   esc done   ctrl-c cancel     # heading   - bullet   **bold**"; p->dialog_lines=ed; p->dialog_count=n; p->dialog_row=n-1;
    for (;;) {
        tui_draw(p); wait_key(p,k); cur=ed+p->dialog_row;
        if (KEY("esc")) { ok=1; break; } if (KEY("ctrl-c")) break;
        if (KEY("enter")) {
            memset(&next,0,sizeof next); at=cur->at;
            if (!line_reserve(p,&next,cur->count-at)) goto done;
            next.count=cur->count-at; if (next.count) memcpy(next.chars,cur->chars+at,next.count*sizeof *next.chars);
            if (!app_reserve((void **)&ed,&cap,n+1,sizeof *ed,&p->error)) { line_free(&next); goto done; }
            cur=ed+p->dialog_row; cur->count=at; memmove(cur+2,cur+1,(n-p->dialog_row-1)*sizeof *ed); cur[1]=next; n++; p->dialog_row++;
        } else if (KEY("up")) { at=cur->at; if (p->dialog_row) { p->dialog_row--; ed[p->dialog_row].at=MIN(ed[p->dialog_row].count,at); } }
        else if (KEY("down")) { at=cur->at; if (p->dialog_row+1<n) { p->dialog_row++; ed[p->dialog_row].at=MIN(ed[p->dialog_row].count,at); } }
        else if (KEY("backspace") && !cur->at) {
            if (p->dialog_row) {
                prev=cur-1; join_at=prev->count; if (!line_reserve(p,prev,prev->count+cur->count)) goto done;
                if (cur->count) memcpy(prev->chars+prev->count,cur->chars,cur->count*sizeof *cur->chars);
                prev->count+=cur->count; prev->at=join_at; line_free(cur); memmove(cur,cur+1,(n-p->dialog_row-1)*sizeof *ed); n--; p->dialog_row--;
            }
        } else line_key(p,cur,k);
        p->dialog_lines=ed; p->dialog_count=n;
    }
    edit_count=n;
    if (ok) {
        result=(char **)calloc(n ? n : 1,sizeof *result); if (!result) { ok=0; goto done; }
        for (i=0;i<n;i++) { s=tui_line_text(p->work,ed+i); if (!s) { tui_say(p,"out of memory",1); ok=0; goto done; } result[i]=app_strdup(s,&p->error); if (!result[i]) { free_lines(result,i); result=NULL; ok=0; goto done; } }
        while (n) { trimmed=app_trim_copy(result[n-1],&p->error); if (!trimmed) { ok=0; goto done; } at=strlen(trimmed); free(trimmed); if (at) break; free(result[--n]); result[n]=NULL; }
        free_lines(*lines,*count); *lines=result; *count=n; *capacity=p->dialog_count; result=NULL;
    }
done:
    p->dialog_lines=NULL; p->dialog_count=0;
    if (!edit_count) edit_count=n;
    for (i=0;i<edit_count;i++) line_free(ed+i);
    free(ed); if (result) free_lines(result,n); return ok;
}

static int store_spec(tui_state *p,app_block *b,int content) { if (!app_deck_store_spec(p->deck,b,content,&p->error)) { tui_say(p,p->error.message,1); return 0; } return 1; }
static void undo_clear(tui_state *p) { size_t i; for (i=0;i<p->undo_count;i++) app_dataset_free(p->undo+i); p->undo_count=0; }
static int snapshot(tui_state *p,app_block *b) {
    app_dataset copy; app_dataset_init(&copy);
    if (!app_dataset_clone(&copy,&b->data,&p->error)) { app_dataset_free(&copy); tui_say(p,p->error.message,1); return 0; }
    if (!app_reserve((void **)&p->undo,&p->undo_capacity,p->undo_count+1,sizeof *p->undo,&p->error)) { app_dataset_free(&copy); tui_say(p,p->error.message,1); return 0; }
    p->undo[p->undo_count++]=copy;
    if (p->undo_count>100) { app_dataset_free(p->undo); memmove(p->undo,p->undo+1,(--p->undo_count)*sizeof *p->undo); }
    b->data_dirty=1; return 1;
}
int tui_sheet_ok(tui_state *p,app_block *b) {
    if (!EMPTY(b->error) || !b->data.series_count) { p->mode=TUI_VIEW; p->editing=0; tui_say(p,EMPTY(b->error) ? "the data has no series to edit" : join(p,"the data changed on disk: ",b->error,""),1); return 0; }
    p->row=MAX(-1,MIN(p->row,(int)app_data_rows(&b->data)-1)); p->col=MAX(0,MIN(p->col,(int)b->data.series_count)); return 1;
}
int tui_annotate_ok(tui_state *p,app_block *b) {
    if (b->kind!=APP_BLOCK_CHART || !EMPTY(b->error) || !b->data.series_count || !app_data_rows(&b->data)) { p->mode=TUI_VIEW; return 0; }
    p->cur_index=MAX(0,MIN(p->cur_index,(int)app_data_rows(&b->data)-1)); p->cur_series=MAX(0,MIN(p->cur_series,(int)b->data.series_count-1)); return 1;
}
const char *tui_cell_text(tui_state *p,lc_context *ctx,const app_dataset *d,int row,int col,int raw) {
    const app_series *s; char b[3*sizeof(long)+3],*out; size_t n; double value;
    (void)p;
    if (col<0 || (size_t)col>d->series_count) return "";
    if (row<0) {
        if (!col) return EMPTY(d->label_name) ? "label" : d->label_name;
        s=d->series+col-1; return !EMPTY(s->name) && s->name[0]=='\001' ? TXT(d->x_name) : TXT(s->name);
    }
    if (!col) {
        if ((size_t)row<d->label_count) return TXT(d->labels[row]);
        sprintf(b,"%ld",(long)row+1); n=strlen(b); out=(char *)lc_scratch(ctx,n+1); if (out) memcpy(out,b,n+1); return out ? out : "";
    }
    s=d->series+col-1;
    if ((size_t)row>=s->count || (s->valid && !s->valid[row]) || !lc_finite(s->values[row])) return "";
    value=s->values[row]; return raw ? lc_fmt_raw(ctx,value) : lc_fmt_val(ctx,value,-1);
}
static int ensure_labels(tui_state *p,app_dataset *d,size_t n) {
    if (!app_reserve((void **)&d->labels,&d->label_capacity,n,sizeof *d->labels,&p->error)) return 0;
    while (d->label_count<n) { d->labels[d->label_count]=app_strdup("",&p->error); if (!d->labels[d->label_count]) return 0; d->label_count++; }
    return 1;
}
static int ensure_values(tui_state *p,app_series *s,size_t n) {
    double *values; unsigned char *valid; size_t i;
    if (n<=s->capacity && s->valid) { while (s->count<n) { s->values[s->count]=0; s->valid[s->count++]=0; } return 1; }
    if (n<s->count) n=s->count;
    if (n>(size_t)-1/sizeof *values) { app_error_set(&p->error,1,"too many cells"); return 0; }
    values=(double *)calloc(n ? n : 1,sizeof *values); valid=(unsigned char *)calloc(n ? n : 1,1);
    if (!values || !valid) { free(values); free(valid); app_error_set(&p->error,1,"out of memory"); return 0; }
    for (i=0;i<s->count;i++) { values[i]=s->values[i]; valid[i]=(unsigned char)(s->valid ? s->valid[i] : lc_finite(s->values[i])); }
    free(s->values); free(s->valid); s->values=values; s->valid=valid; s->capacity=n; s->count=n; return 1;
}
static int commit_cell(tui_state *p,app_block *b) {
    app_dataset *d=&b->data; app_series *s; char *text,*raw; size_t i; double value=0; int result=1;
    raw=tui_line_text(p->work,&p->edit);
    if (!raw) { tui_say(p,"out of memory",1); return 0; }
    text=app_trim_copy(raw,&p->error); if (!text) return 0;
    if (!strcmp(text,TXT(tui_cell_text(p,p->work,d,p->row,p->col,1)))) { free(text); return 1; }
    if (p->row>=0 && p->col>0 && *text && !app_parse_number(text,&value)) { tui_say(p,join(p,"\"",text,"\" is not a number"),1); free(text); return 0; }
    if (!snapshot(p,b)) { free(text); return 0; }
    if (p->row<0) {
        if (!p->col) result=replace(p,&d->label_name,text);
        else { s=d->series+p->col-1; if (!EMPTY(s->name) && s->name[0]=='\001') result=replace(p,&d->x_name,*text ? text : "x"); else result=replace(p,&s->name,*text ? text : join(p,"series ",number(p,p->col),"")); }
    } else {
        i=(size_t)p->row;
        if (!p->col) { result=ensure_labels(p,d,i+1); if (result) result=replace(p,d->labels+i,text); }
        else { s=d->series+p->col-1; result=ensure_values(p,s,MAX(s->count,i+1)); if (result) { s->values[i]=value; s->valid[i]=(unsigned char)(*text!=0); } }
    }
    if (!result) tui_say(p,p->error.message,1);
    free(text); return result;
}
static void prune_notes(tui_state *p,app_block *b) {
    app_spec *spec=(b->spec.flags&APP_SPEC_NOTES) ? &b->spec : &b->data.spec; size_t i,j; int found;
    for (i=0;i<spec->note_count;i++) {
        if (spec->notes[i].kind!=APP_NOTE_POINT && spec->notes[i].kind!=APP_NOTE_VLINE) continue;
        found=0; for (j=0;j<b->data.label_count;j++) if (lc_name_eq(b->data.labels[j],spec->notes[i].label)) found=1;
        if (!found && !EMPTY(spec->notes[i].label)) { tui_say(p,join(p,"the annotation on \"",spec->notes[i].label,"\" has lost its row"),1); return; }
    }
}
static void tweak(tui_state *p,const char *k) {
    app_block *b=tui_target(p,1); lc_chart_options o; int at,n,dir,e; size_t i; unsigned long flag=0;
    if (!b) { tui_say(p,"no chart on this slide",1); return; }
    if (app_block_options(p->work,p->deck,b,NULL,&o)!=LC_OK) { tui_say(p,"out of memory",1); return; }
    if (KEY("t")||KEY("T")) {
        at=0; n=12; dir=KEY("T") ? -1 : 1; for (i=0;i<12;i++) if (lc_name_eq(chart_types[i],lc_chart_type(o.type))) at=(int)i;
        if (!replace(p,&b->spec.type,chart_types[(at+dir+n)%n])) return;
        flag=APP_SPEC_TYPE; tui_say(p,join(p,"type: ",b->spec.type,""),0);
    } else if (KEY("p")||KEY("P")) {
        at=0; n=8; dir=KEY("P") ? -1 : 1; for (i=0;i<8;i++) if (lc_name_eq(palette_names[i],o.palette)) at=(int)i;
        if (!replace(p,&b->spec.palette,palette_names[(at+dir+n)%n])) return;
        flag=APP_SPEC_PALETTE; tui_say(p,join(p,"palette: ",b->spec.palette,""),0);
    } else if (KEY("v")) { b->spec.values=!o.values; flag=APP_SPEC_VALUES; }
    else if (KEY("l")) { b->spec.legend=!o.legend; flag=APP_SPEC_LEGEND; }
    else if (KEY("g")) { b->spec.grid=!o.grid; flag=APP_SPEC_GRID; }
    else if (KEY("d")||KEY("D")) { b->spec.depth=MAX(0,MIN(6,o.depth+(KEY("d") ? 1 : -1))); flag=APP_SPEC_DEPTH; tui_say(p,join(p,"depth ",number(p,b->spec.depth),""),0); }
    else if (KEY("x")) { n=(int)(b->data.series_count==1 ? app_data_rows(&b->data) : b->data.series_count); e=o.explode; b->spec.explode=e==-2 ? -1 : (e+1<n ? e+1 : -2); flag=APP_SPEC_EXPLODE; }
    b->spec.flags|=flag; p->cli.flags&=~flag; store_spec(p,b,0);
}
static void key_sheet(tui_state *p,const char *k) {
    app_block *b=tui_focused(p); app_dataset *d; app_series *s; size_t at,n,i; int R,C;
    if (!b || !tui_sheet_ok(p,b)) { p->mode=TUI_VIEW; return; }
    d=&b->data; R=(int)app_data_rows(d); C=(int)d->series_count+1;
    if (p->editing) {
        if (KEY("esc")||KEY("ctrl-c")) { p->editing=0; return; }
        if (KEY("enter")||KEY("tab")||KEY("down")||KEY("up")||KEY("shift-tab")) {
            if (!commit_cell(p,b)) return;
            p->editing=0; if (KEY("enter")||KEY("down")) p->row=MIN(R-1,p->row+1); else if (KEY("up")) p->row=MAX(-1,p->row-1); else if (KEY("tab")) p->col=MIN(C-1,p->col+1); else p->col=MAX(0,p->col-1); return;
        }
        line_key(p,&p->edit,k); return;
    }
    if (KEY("esc")||KEY("q")) { p->mode=TUI_VIEW; return; }
    if (KEY("up")) p->row=MAX(-1,p->row-1);
    else if (KEY("down")) p->row=MIN(R-1,p->row+1);
    else if (KEY("left")||KEY("shift-tab")) p->col=MAX(0,p->col-1);
    else if (KEY("right")||KEY("tab")) p->col=MIN(C-1,p->col+1);
    else if (KEY("pgup")) p->row=MAX(-1,p->row-10);
    else if (KEY("pgdn")) p->row=MIN(R-1,p->row+10);
    else if (KEY("home")) p->col=0;
    else if (KEY("end")) p->col=C-1;
    else if (KEY("ctrl-home")||KEY("ctrl-t")) { p->row=0; p->col=1; }
    else if (KEY("enter")||KEY("f2")) { p->editing=line_set(p,&p->edit,tui_cell_text(p,p->work,d,p->row,p->col,1)); }
    else if (KEY("delete")||KEY("backspace")) {
        if (p->row>=0 && p->col>0 && snapshot(p,b)) { s=d->series+p->col-1; if ((size_t)p->row<s->count && ensure_values(p,s,s->count)) { s->values[p->row]=0; s->valid[p->row]=0; } }
    } else if (KEY("insert")||KEY("ctrl-n")) {
        if (!snapshot(p,b)) return;
        n=app_data_rows(d); at=MIN((size_t)MAX(0,p->row+1),n);
        if (!ensure_labels(p,d,n+1)) goto failure;
        free(d->labels[n]); memmove(d->labels+at+1,d->labels+at,(n-at)*sizeof *d->labels); d->labels[at]=app_strdup("new",&p->error); if (!d->labels[at]) goto failure;
        for (i=0;i<d->series_count;i++) { s=d->series+i; if (!ensure_values(p,s,n+1)) goto failure; memmove(s->values+at+1,s->values+at,(n-at)*sizeof *s->values); memmove(s->valid+at+1,s->valid+at,n-at); s->values[at]=0; s->valid[at]=0; }
        p->row=(int)at; p->col=0; p->editing=line_set(p,&p->edit,"");
    } else if (KEY("ctrl-d")) {
        if (p->row>=0 && R>1) {
            if (!snapshot(p,b)) return;
            at=(size_t)p->row;
            if (at<d->label_count) { free(d->labels[at]); memmove(d->labels+at,d->labels+at+1,(--d->label_count-at)*sizeof *d->labels); }
            for (i=0;i<d->series_count;i++) { s=d->series+i; if (at<s->count) { memmove(s->values+at,s->values+at+1,(s->count-at-1)*sizeof *s->values); if (s->valid) memmove(s->valid+at,s->valid+at+1,s->count-at-1); s->count--; } }
            p->row=MIN(p->row,(int)app_data_rows(d)-1); prune_notes(p,b);
        } else tui_say(p,"a chart needs at least one row",1);
    } else if (KEY("ctrl-a")) {
        if (!snapshot(p,b)) return;
        n=app_data_rows(d); if (!app_reserve((void **)&d->series,&d->series_capacity,d->series_count+1,sizeof *d->series,&p->error)) goto failure;
        s=d->series+d->series_count; memset(s,0,sizeof *s); s->decimals=-1; s->color=7; s->name=app_strdup(join(p,"series ",number(p,(long)d->series_count+1),""),&p->error);
        if (!s->name || !ensure_values(p,s,n)) { free(s->name); free(s->values); free(s->valid); memset(s,0,sizeof *s); goto failure; }
        d->series_count++; p->col=(int)d->series_count; p->row=-1; p->editing=line_set(p,&p->edit,s->name);
    } else if (KEY("ctrl-x")) {
        if (p->col>0 && d->series_count>1) {
            if (!snapshot(p,b)) return;
            s=d->series+p->col-1; free(s->name); free(s->values); free(s->valid); memmove(s,s+1,(d->series_count-(size_t)p->col)*sizeof *s); d->series_count--; p->col=MIN(p->col,(int)d->series_count);
        } else tui_say(p,!p->col ? "the label column stays" : "a chart needs at least one series",1);
    } else if (KEY("ctrl-z")||KEY("u")) {
        if (!p->undo_count) tui_say(p,"nothing to undo",0);
        else { app_dataset_free(d); *d=p->undo[--p->undo_count]; p->row=MIN(p->row,(int)app_data_rows(d)-1); p->col=MIN(p->col,(int)d->series_count); }
    } else if (KEY("ctrl-s")||KEY("s")) save(p);
    else if (KEY("t")||KEY("T")) tweak(p,k);
    else if (printable(k)) { p->editing=line_set(p,&p->edit,""); line_key(p,&p->edit,k); }
    return;
failure:
    tui_say(p,p->error.message,1);
    /* Undo was taken before mutation; restore it after any partial resize. */
    if (p->undo_count) { app_dataset_free(d); *d=p->undo[--p->undo_count]; }
}

static void note_free(app_annotation *a) { free(a->text); free(a->label); free(a->series); memset(a,0,sizeof *a); }
static app_spec *notes_of(tui_state *p,app_block *b) {
    app_spec copy; size_t i;
    if (!(b->spec.flags&APP_SPEC_NOTES)) {
        app_spec_init(&copy); if (!app_spec_clone(&copy,&b->data.spec,&p->error)) { app_spec_free(&copy); tui_say(p,p->error.message,1); return NULL; }
        for (i=0;i<b->spec.note_count;i++) note_free(b->spec.notes+i);
        free(b->spec.notes); b->spec.notes=copy.notes; b->spec.note_count=copy.note_count; b->spec.note_capacity=copy.note_capacity;
        copy.notes=NULL; copy.note_count=copy.note_capacity=0; app_spec_free(&copy); b->spec.flags|=APP_SPEC_NOTES;
    }
    return &b->spec;
}
static int first_series(const app_dataset *d) { return d->series_count>1 && !EMPTY(d->series[0].name) && d->series[0].name[0]=='\001' ? 1 : 0; }
static app_annotation *note_at_cursor(tui_state *p,app_block *b) {
    app_spec *spec=notes_of(p,b); const app_dataset *d=&b->data; const char *label; size_t i; app_annotation *a; int row,series;
    if (!spec) return NULL;
    label=(size_t)p->cur_index<d->label_count ? d->labels[p->cur_index] : "";
    for (i=0;i<spec->note_count;i++) {
        a=spec->notes+i; if (a->kind!=APP_NOTE_POINT) continue;
        row=(!EMPTY(a->label) && lc_name_eq(a->label,label)) || (EMPTY(a->label) && a->index==p->cur_index);
        series=EMPTY(a->series) || ((size_t)p->cur_series<d->series_count && app_streq_ci(a->series,d->series[p->cur_series].name));
        if (row && series) return a;
    }
    return NULL;
}
static void erase_note(app_spec *s,app_annotation *a) { size_t index=(size_t)(a-s->notes); note_free(a); memmove(a,a+1,(s->note_count-index-1)*sizeof *a); s->note_count--; }
static int append_note(tui_state *p,app_block *b,app_note_kind kind,const char *label,const char *series,const char *text,double value) {
    app_spec *s=notes_of(p,b); app_annotation a;
    if (!s) return 0;
    memset(&a,0,sizeof a); a.kind=kind; a.index=kind==APP_NOTE_POINT || kind==APP_NOTE_VLINE ? p->cur_index : -1;
    a.series_index=-1; a.color=-1; a.fx=0.98; a.fy=0.02; a.value=value;
    a.label=app_strdup(TXT(label),&p->error); a.series=app_strdup(TXT(series),&p->error); a.text=app_strdup(TXT(text),&p->error);
    if (!a.label || !a.series || !a.text || !app_reserve((void **)&s->notes,&s->note_capacity,s->note_count+1,sizeof *s->notes,&p->error)) { note_free(&a); tui_say(p,p->error.message,1); return 0; }
    s->notes[s->note_count++]=a; return 1;
}
static void open_annotate(tui_state *p) {
    app_block *b=tui_target(p,1);
    if (!b) { tui_say(p,"no chart on this slide",1); return; }
    if (!EMPTY(b->error) || !b->data.series_count || !app_data_rows(&b->data)) { tui_say(p,"this chart has no data to annotate",1); return; }
    p->mode=TUI_ANNOTATE; p->cur_index=MIN(p->cur_index,(int)app_data_rows(&b->data)-1); p->cur_series=first_series(&b->data);
}
static void key_annotate(tui_state *p,const char *k) {
    app_block *b=tui_focused(p); const app_dataset *d; app_spec *spec; app_annotation *old;
    int R,NS,s0,ok; size_t i,hit; const char *label,*series; char *text=NULL,*at=NULL,*trimmed; double value;
    if (!b || !tui_annotate_ok(p,b)) { p->mode=TUI_VIEW; return; }
    d=&b->data; R=(int)app_data_rows(d); NS=(int)d->series_count; s0=first_series(d);
    label=(size_t)p->cur_index<d->label_count ? TXT(d->labels[p->cur_index]) : "";
    series=(size_t)p->cur_series<d->series_count ? TXT(d->series[p->cur_series].name) : "";
    if (KEY("esc")||KEY("q")||KEY("a")) { p->mode=TUI_VIEW; return; }
    if (KEY("left")) p->cur_index=(p->cur_index+R-1)%R;
    else if (KEY("right")) p->cur_index=(p->cur_index+1)%R;
    else if (KEY("up")) p->cur_series=p->cur_series+1>=NS ? s0 : p->cur_series+1;
    else if (KEY("down")) p->cur_series=p->cur_series-1<s0 ? NS-1 : p->cur_series-1;
    else if (KEY("home")) p->cur_index=0;
    else if (KEY("end")) p->cur_index=R-1;
    else if (KEY("enter")) {
        old=note_at_cursor(p,b); text=app_strdup(old ? TXT(old->text) : "",&p->error); if (!text) return;
        if (!ask(p,old ? "edit note" : join(p,"note on ",label,""),"What should it say?",&text)) goto done;
        trimmed=app_trim_copy(text,&p->error); if (!trimmed) goto done;
        if (!*trimmed) { if (old) erase_note(&b->spec,old); }
        else if (old) replace(p,&old->text,text);
        else append_note(p,b,APP_NOTE_POINT,label,NS-s0>1 ? series : "",text,0);
        free(trimmed); store_spec(p,b,1);
    } else if (KEY("h")) {
        value=0; if ((size_t)p->cur_series<d->series_count && (size_t)p->cur_index<d->series[p->cur_series].count) value=d->series[p->cur_series].values[p->cur_index];
        at=app_strdup(lc_fmt_raw(p->work,value),&p->error); if (!at) goto done;
        if (!ask(p,"line across the plot","At what value?",&at)) goto done;
        if (!app_parse_number(at,&value)) { tui_say(p,join(p,"\"",at,"\" is not a number"),1); goto done; }
        if (!ask(p,"line across the plot","Label (may be empty):",&text)) goto done;
        if (append_note(p,b,APP_NOTE_HLINE,"","",text,value)) store_spec(p,b,1);
    } else if (KEY("v")) {
        if (!ask(p,join(p,"mark ",label,""),"Label (may be empty):",&text)) goto done;
        if (append_note(p,b,APP_NOTE_VLINE,label,"",text,0)) store_spec(p,b,1);
    } else if (KEY("delete")||KEY("backspace")||KEY("x")) {
        old=note_at_cursor(p,b); spec=notes_of(p,b); if (!spec) goto done;
        if (old) erase_note(spec,old);
        else {
            if (!spec->note_count) { tui_say(p,"no annotations on this chart",0); goto done; }
            hit=spec->note_count-1;
            for (i=spec->note_count;i>0;i--) if (spec->notes[i-1].kind==APP_NOTE_VLINE && lc_name_eq(spec->notes[i-1].label,label)) { hit=i-1; break; }
            ok=confirm(p,"remove annotation",join(p,"Remove \"",lc_text_truncate(p->work,spec->notes[hit].text,40),"\"?"),"y remove   esc keep","y");
            if (ok!='y') goto done;
            erase_note(spec,spec->notes+hit);
        }
        store_spec(p,b,1);
    }
done:
    free(text); free(at);
}
static int structural_ok(tui_state *p) { if (data_dirty(p)) { tui_say(p,"save the edited data first (s)",1); return 0; } return 1; }
static int json_set_string(tui_state *p,app_json *obj,const char *key,const char *value) {
    app_json *v=app_json_new(APP_JSON_STRING,&p->error); if (!v) return 0;
    v->string=app_strdup(TXT(value),&p->error);
    if (!v->string || !app_json_set(obj,key,v,&p->error)) { app_json_free(v); return 0; }
    return 1;
}
static void reparse(tui_state *p) { if (!app_deck_reparse(p->deck,&p->load,&p->error)) tui_say(p,p->error.message,1); }
static app_json *slide_blocks(tui_state *p,app_json *slide) {
    app_json *blocks=app_json_find(slide,"blocks"),*block,*value; size_t i; const char *key;
    if (blocks) return blocks;
    block=app_json_new(APP_JSON_OBJECT,&p->error); blocks=app_json_new(APP_JSON_ARRAY,&p->error);
    if (!block || !blocks) { app_json_free(block); app_json_free(blocks); return NULL; }
    for (i=0;i<slide->count;i++) {
        key=slide->members[i].key;
        if (!strcmp(key,"title") || !strcmp(key,"subtitle") || !strcmp(key,"notes") || !strcmp(key,"layout")) continue;
        value=app_json_clone(slide->members[i].value,&p->error);
        if (!value || !app_json_set(block,key,value,&p->error)) { app_json_free(value); app_json_free(block); app_json_free(blocks); return NULL; }
    }
    if (block->count) { if (!app_json_push(blocks,block,&p->error)) { app_json_free(block); app_json_free(blocks); return NULL; } }
    else { app_json_free(block); block=NULL; }
    /* Install before removing shorthand keys; allocation failure leaves the
     * original slide intact. Member names stay owned by the cloned block. */
    if (!app_json_set(slide,"blocks",blocks,&p->error)) { app_json_free(blocks); return NULL; }
    if (block) for (i=0;i<block->count;i++) app_json_erase(slide,block->members[i].key);
    return blocks;
}
static void edit_title(tui_state *p) {
    app_slide *s=tui_slide(p); app_json *node; char *title;
    if (!s) return;
    title=app_strdup(TXT(s->title),&p->error); if (!title) return;
    if (ask(p,"slide title",join(p,"Title of slide ",number(p,p->slide+1),":"),&title) && structural_ok(p)) {
        node=app_deck_node(p->deck,s->path);
        if (node) { if (!*title) app_json_erase(node,"title"); else if (!json_set_string(p,node,"title",title)) { free(title); tui_say(p,p->error.message,1); return; } p->deck->dirty=1; reparse(p); }
    }
    free(title);
}
static void add_text(tui_state *p) {
    app_slide *s=tui_slide(p); app_json *node,*block=NULL,*array=NULL,*value,*blocks; char **lines=NULL; size_t n=0,cap=0,i;
    if (!s || !structural_ok(p)) return;
    if (!edit_text(p,"new text block",&lines,&n,&cap) || !n) goto done;
    node=app_deck_node(p->deck,s->path); if (!node) goto done;
    block=app_json_new(APP_JSON_OBJECT,&p->error); array=app_json_new(APP_JSON_ARRAY,&p->error); if (!block || !array) goto done;
    for (i=0;i<n;i++) { value=app_json_new(APP_JSON_STRING,&p->error); if (!value) goto done; value->string=app_strdup(lines[i],&p->error); if (!value->string || !app_json_push(array,value,&p->error)) { app_json_free(value); goto done; } }
    if (!app_json_set(block,"text",array,&p->error)) goto done;
    array=NULL; blocks=slide_blocks(p,node); if (!blocks || !app_json_push(blocks,block,&p->error)) goto done;
    block=NULL; p->deck->dirty=1; reparse(p);
done:
    app_json_free(block); app_json_free(array); free_lines(lines,n);
}
static void add_chart(tui_state *p) {
    app_slide *s=tui_slide(p); app_json *node,*block,*blocks; char *path=NULL,*trimmed;
    if (!s || !structural_ok(p)) return;
    if (!ask(p,"add a chart","Data file (csv, tsv or json), relative to the deck:",&path)) { free(path); return; }
    trimmed=app_trim_copy(path,&p->error); free(path); if (!trimmed) return;
    if (!*trimmed) { free(trimmed); return; }
    node=app_deck_node(p->deck,s->path); if (!node) { free(trimmed); return; }
    block=app_json_new(APP_JSON_OBJECT,&p->error); if (!block) { free(trimmed); return; }
    if (!json_set_string(p,block,"data",trimmed)) { app_json_free(block); free(trimmed); return; }
    free(trimmed); blocks=slide_blocks(p,node); if (!blocks || !app_json_push(blocks,block,&p->error)) { app_json_free(block); return; }
    p->deck->dirty=1; reparse(p); if (!app_deck_ok(p->deck) && p->deck->issue_count) tui_say(p,p->deck->issues[p->deck->issue_count-1].message,1);
}
static void add_slide(tui_state *p) {
    app_json *array,*slide,*blocks; char *title=NULL; size_t at;
    if (!structural_ok(p) || !ask(p,"new slide","Title of the new slide:",&title)) { free(title); return; }
    array=app_json_find(p->deck->root,"slides"); if (!array || array->type!=APP_JSON_ARRAY) { free(title); return; }
    slide=app_json_new(APP_JSON_OBJECT,&p->error); blocks=app_json_new(APP_JSON_ARRAY,&p->error);
    if (!slide || !blocks || !json_set_string(p,slide,"title",title)) { app_json_free(slide); app_json_free(blocks); free(title); return; }
    free(title);
    if (!app_json_set(slide,"blocks",blocks,&p->error)) { app_json_free(slide); app_json_free(blocks); return; }
    if (!app_json_push(array,slide,&p->error)) { app_json_free(slide); return; }
    at=MIN((size_t)(p->slide+1),array->count-1); memmove(array->items+at+1,array->items+at,(array->count-at-1)*sizeof *array->items); array->items[at]=slide;
    p->deck->dirty=1; reparse(p); go(p,p->slide+1); tui_say(p,"i adds text, c adds a chart",0);
}
static void delete_block(tui_state *p) {
    app_block *b=tui_focused(p); app_json *array; char *path,*open; size_t index,n;
    if (!b) { tui_say(p,"tab to a block first",0); return; }
    if (!structural_ok(p) || confirm(p,"delete block","Remove the focused block from this slide?","y delete   esc keep","y")!='y') return;
    path=copy_work(p,b->path); if (!path) return; open=strrchr(path,'['); n=strlen(path);
    if (!open || !n || path[n-1]!=']' || strncmp(path,"slides[",7) || !strchr(path,'.')) { tui_say(p,"this slide is its one block; delete the slide in the JSON",1); return; }
    index=(size_t)strtoul(open+1,NULL,10); *open=0; array=app_deck_node(p->deck,path);
    if (!array || array->type!=APP_JSON_ARRAY || index>=array->count) { tui_say(p,"this slide is its one block; delete the slide in the JSON",1); return; }
    app_json_free(array->items[index]); memmove(array->items+index,array->items+index+1,(array->count-index-1)*sizeof *array->items); array->count--;
    p->deck->dirty=1; p->focus=-1; reparse(p);
}
static void open_editor(tui_state *p) {
    app_block *b=tui_target(p,0); char **lines=NULL,*value; size_t n=0,capacity=0,i;
    if (!b) { tui_say(p,"nothing to edit on this slide (i adds text, c a chart)",0); return; }
    if (b->kind==APP_BLOCK_TEXT) {
        if (!app_reserve((void **)&lines,&capacity,b->line_count,sizeof *lines,&p->error)) return;
        for (i=0;i<b->line_count;i++) { lines[n]=app_strdup(b->lines[i],&p->error); if (!lines[n]) { free_lines(lines,n); return; } n++; }
        if (edit_text(p,"text",&lines,&n,&capacity)) { free_lines(b->lines,b->line_count); b->lines=lines; b->line_count=n; b->line_capacity=capacity; lines=NULL; n=0; if (!app_deck_store_text(p->deck,b,&p->error)) tui_say(p,p->error.message,1); }
        free_lines(lines,n);
    } else if (b->kind==APP_BLOCK_STAT) {
        value=app_strdup(TXT(b->value),&p->error); if (!value) return;
        if (ask(p,"big number","Value:",&value)) { free(b->value); b->value=value; value=NULL; if (!app_deck_store_text(p->deck,b,&p->error)) tui_say(p,p->error.message,1); } free(value);
    } else if (b->kind!=APP_BLOCK_CHART) tui_say(p,"shapes and flows are edited in the deck file; the presenter redraws when it changes",0);
    else if (!EMPTY(b->error)) tui_say(p,b->error,1);
    else { p->mode=TUI_SHEET; p->row=0; p->col=1; p->top=p->left=0; p->editing=0; undo_clear(p); }
}

static void key_overview(tui_state *p,const char *k) {
    int n=(int)p->deck->slide_count;
    if (KEY("esc")||KEY("o")||KEY("q")) p->mode=TUI_VIEW;
    else if (KEY("up")||KEY("k")) p->pick=MAX(0,p->pick-1);
    else if (KEY("down")||KEY("j")) p->pick=MIN(n-1,p->pick+1);
    else if (KEY("pgup")) p->pick=MAX(0,p->pick-10);
    else if (KEY("pgdn")) p->pick=MIN(n-1,p->pick+10);
    else if (KEY("home")) p->pick=0;
    else if (KEY("end")) p->pick=n-1;
    else if (KEY("enter")||KEY(" ")) { go(p,p->pick); p->mode=TUI_VIEW; }
}
static void key_view(tui_state *p,const char *k) {
    size_t n,i; int digit,choice,at; app_slide *s;
    if (*k && !k[1] && *k>='0' && *k<='9') { n=strlen(p->digits); if (n<4) { p->digits[n]=*k; p->digits[n+1]=0; } return; }
    if (*p->digits) { digit=atoi(p->digits); p->digits[0]=0; if (KEY("enter")||KEY("g")) { go(p,digit-1); return; } if (KEY("esc")||KEY("backspace")) return; }
    if (KEY("q")||KEY("ctrl-c")||KEY("ctrl-d")) {
        if (!unsaved(p)) { p->quit=1; return; }
        choice=confirm(p,"unsaved changes","There are edits that have not been saved.","s save and quit   q quit without saving   esc stay","sq");
        if (choice=='s') { save(p); p->quit=!unsaved(p); } else if (choice=='q') p->quit=1;
    } else if (KEY("right")||KEY(" ")||KEY("pgdn")||KEY("down")||KEY("j")||KEY("enter")) go(p,p->slide+1);
    else if (KEY("left")||KEY("pgup")||KEY("up")||KEY("k")||KEY("backspace")) go(p,p->slide-1);
    else if (KEY("home")) go(p,0);
    else if (KEY("end")) go(p,(int)p->deck->slide_count-1);
    else if (KEY("o")) { p->mode=TUI_OVERVIEW; p->pick=p->slide; }
    else if (KEY("?")||KEY("f1")) p->mode=TUI_HELP;
    else if (KEY("n")) { p->notes=!p->notes; s=tui_slide(p); if (p->notes && s && EMPTY(s->notes)) { p->notes=0; tui_say(p,"this slide has no notes",0); } }
    else if (KEY("tab")||KEY("shift-tab")) { s=tui_slide(p); n=s ? leaf_count(s->blocks,s->block_count) : 0; if (!n) return; if (KEY("tab")) p->focus=p->focus+1>=(int)n ? (n==1 ? 0 : -1) : p->focus+1; else p->focus=p->focus<0 ? (int)n-1 : p->focus-1; }
    else if (KEY("esc")) { p->focus=-1; p->notes=0; }
    else if (KEY("s")||KEY("ctrl-s")) save(p);
    else if (KEY("r")||KEY("ctrl-l")) { if (KEY("r")) { if (unsaved(p) && confirm(p,"reload","Reloading drops your unsaved edits.","r reload   esc keep editing","r")!='r') return; reload(p); } }
    else if (KEY("e")) open_editor(p);
    else if (KEY("a")) open_annotate(p);
    else if (KEY("t")||KEY("T")||KEY("p")||KEY("P")||KEY("v")||KEY("l")||KEY("g")||KEY("d")||KEY("D")||KEY("x")) tweak(p,k);
    else if (KEY("y")) {
        at=0; for (i=0;i<3;i++) if (lc_name_eq(theme_names[i],EMPTY(p->theme) ? p->deck->theme : p->theme)) at=(int)i;
        if (replace(p,&p->theme,theme_names[(at+1)%3]) && json_set_string(p,p->deck->root,"theme",p->theme) && replace(p,&p->deck->theme,p->theme)) { p->deck->tweaked=1; tui_say(p,join(p,"theme: ",p->theme,""),0); }
    } else if (KEY("E")) edit_title(p);
    else if (KEY("i")) add_text(p);
    else if (KEY("c")) add_chart(p);
    else if (KEY("N")) add_slide(p);
    else if (KEY("X")) delete_block(p);
}
static void handle(tui_state *p,const char *k) {
    switch (p->mode) {
    case TUI_HELP: p->mode=TUI_VIEW; break;
    case TUI_OVERVIEW: key_overview(p,k); break;
    case TUI_SHEET: key_sheet(p,k); break;
    case TUI_ANNOTATE: key_annotate(p,k); break;
    default: key_view(p,k); break;
    }
}
int app_run_presenter(app_deck *deck,const app_present_opts *options) {
    tui_state state,*p=&state; app_raw raw; app_error why; int c,r,cols=0,rows=0,cells=0,result=0; char key[32];
    if (!deck || !options) return 2;
    memset(p,0,sizeof *p); memset(&raw,0,sizeof raw); memset(&why,0,sizeof why);
    p->deck=deck; p->options=*options; p->focus=-1; p->dirty=1;
    app_spec_init(&p->cli); app_load_opts_init(&p->load); if (options->load) p->load=*options->load;
    if (options->cli && !app_spec_clone(&p->cli,options->cli,&p->error)) { result=1; goto done; }
    if (lc_context_create(NULL,&p->ctx)!=LC_OK || lc_context_create(NULL,&p->work)!=LC_OK) { result=1; goto done; }
    p->theme=app_strdup(TXT(options->theme),&p->error); if (!p->theme) { result=1; goto done; }
    app_guard_install(); p->display=app_display_open(EMPTY(options->gfx) ? "auto" : options->gfx,options->scale,options->color,&why);
    if (!p->display) { fprintf(stderr,"charts: %s\n",why.message); result=1; goto done; }
    if (!app_raw_enter(&raw,0)) { fprintf(stderr,"charts: the presenter needs a terminal on stdin\n"); result=2; goto done; }
    cells=!strcmp(app_display_name(p->display),"cells") || !strcmp(app_display_name(p->display),"ascii");
    app_cursor_show(0); if (cells) app_screen_clear(); p->slide=MAX(0,MIN(options->slide,(int)deck->slide_count-1));
    if (!app_deck_ok(deck)) tui_say(p,join(p,number(p,(long)error_count(p))," problem(s) in this deck: run charts --check",""),1);
    else if (options->verbose) tui_say(p,why.message,0);
    while (!p->quit) {
        lc_scratch_reset(p->work); app_display_grid(p->display,&c,&r);
        if (c!=cols || r!=rows) { cols=c; rows=r; p->dirty=1; }
        if (must_redraw(p)) p->dirty=1;
        poll_files(p);
        if (p->message_ttl>0 && --p->message_ttl==0) { free(p->message); p->message=NULL; p->dirty=1; }
        if (p->dirty) { tui_draw(p); p->dirty=0; }
        app_read_key(250,key); if (!*key || !strcmp(key,"unknown")) continue;
        handle(p,key); p->dirty=1;
    }
done:
    if (p->display) { app_display_close(p->display); app_cursor_show(1); if (cells) app_screen_clear(); }
    app_raw_leave(&raw); app_guard_restore(); undo_clear(p); free(p->undo); line_free(&p->edit);
    free(p->theme); free(p->message); app_spec_free(&p->cli); lc_context_destroy(p->ctx); lc_context_destroy(p->work);
    return result;
}
