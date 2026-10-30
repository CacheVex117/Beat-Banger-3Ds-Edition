/**
 * @file extras.c
 * @brief Loader for the shop / collection / lore / gallery / dialogue text data (see extras.h).
 */
#include "extras.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXTRAS_DIR "romfs:/extras/"

/* Split a line in place on '|' (max_fields fields); returns the field count. */
static int split_fields(char* line, char** f, int max_fields) {
    int n = 0;
    char* p = line;
    while (n < max_fields) {
        f[n++] = p;
        char* bar = strchr(p, '|');
        if (!bar) break;
        *bar = '\0';
        p = bar + 1;
    }
    return n;
}

static void chomp(char* s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = '\0';
}

static FILE* open_extra(const char* name) {
    char path[96];
    snprintf(path, sizeof(path), EXTRAS_DIR "%s", name);
    return fopen(path, "r");
}

bool bb_extras_load(bb_extras_t* e) {
    memset(e, 0, sizeof(*e));
    char line[1024];
    char* f[10];
    FILE* fp = open_extra("shop.txt");
    if (!fp) {
        return false;
    }
    while (e->shop_count < BB_MAX_SHOP && fgets(line, sizeof(line), fp)) {
        chomp(line);
        if (split_fields(line, f, 10) < 6) continue;
        bb_shop_item_t* s = &e->shop[e->shop_count++];
        s->cat = f[0][0];
        s->item = atoi(f[1]);
        snprintf(s->name, sizeof(s->name), "%.39s", f[2]);
        s->price = atoi(f[3]);
        s->req_exp = atoi(f[4]);
        snprintf(s->desc, sizeof(s->desc), "%.99s", f[5]);
    }
    fclose(fp);

    if ((fp = open_extra("collect.txt"))) {
        while (e->collect_count < BB_MAX_COLLECT && fgets(line, sizeof(line), fp)) {
            chomp(line);
            if (split_fields(line, f, 10) < 5) continue;
            bb_collect_t* c = &e->collect[e->collect_count++];
            c->item = atoi(f[0]);
            snprintf(c->name, sizeof(c->name), "%.39s", f[1]);
            snprintf(c->desc, sizeof(c->desc), "%.109s", f[2]);
            snprintf(c->unlock, sizeof(c->unlock), "%.89s", f[3]);
            c->icon = atoi(f[4]);
        }
        fclose(fp);
    }
    if ((fp = open_extra("lore.txt"))) {
        while (e->lore_count < BB_MAX_LORE && fgets(line, sizeof(line), fp)) {
            chomp(line);
            if (split_fields(line, f, 10) < 8) continue;
            bb_lore_t* l = &e->lore[e->lore_count++];
            snprintf(l->name, sizeof(l->name), "%.15s", f[0]);
            snprintf(l->caption, sizeof(l->caption), "%.47s", f[1]);
            snprintf(l->occupation, sizeof(l->occupation), "%.39s", f[2]);
            snprintf(l->height, sizeof(l->height), "%.11s", f[3]);
            snprintf(l->lore, sizeof(l->lore), "%.519s", f[4]);
            snprintf(l->residence, sizeof(l->residence), "%.169s", f[5]);
            snprintf(l->hobbies, sizeof(l->hobbies), "%.199s", f[6]);
            snprintf(l->t3x, sizeof(l->t3x), "%.31s", f[7]);
        }
        fclose(fp);
    }
    if ((fp = open_extra("gallery.txt"))) {
        while (e->gallery_count < BB_MAX_GALLERY && fgets(line, sizeof(line), fp)) {
            chomp(line);
            if (split_fields(line, f, 10) < 6) continue;
            bb_gallery_t* g = &e->gallery[e->gallery_count++];
            g->item = atoi(f[0]);
            snprintf(g->title, sizeof(g->title), "%.39s", f[1]);
            snprintf(g->sub, sizeof(g->sub), "%.79s", f[2]);
            snprintf(g->t3x, sizeof(g->t3x), "%.23s", f[3]);
            snprintf(g->unlock, sizeof(g->unlock), "%.89s", f[4]);
            g->price = atoi(f[5]);
        }
        fclose(fp);
    }
    if ((fp = open_extra("dialogue.txt"))) {
        while (fgets(line, sizeof(line), fp)) {
            chomp(line);
            if (line[0] == 'L' && line[1] == '|' && e->dlg_count < BB_MAX_DLG_LINES) {
                if (split_fields(line + 2, f, 4) < 3) continue;
                bb_dlg_line_t* d = &e->dlg[e->dlg_count++];
                snprintf(d->kind, sizeof(d->kind), "%.23s", f[0]);
                d->key = atoi(f[1]);
                snprintf(d->text, sizeof(d->text), "%.109s", f[2]);
            } else if (line[0] == 'T' && line[1] == '|' && e->talk_count < BB_MAX_TALK) {
                if (split_fields(line + 2, f, 5) < 4) continue;
                bb_talk_t* t = &e->talk[e->talk_count++];
                snprintf(t->set, sizeof(t->set), "%.31s", f[0]);
                snprintf(t->input, sizeof(t->input), "%.63s", f[1]);
                snprintf(t->next, sizeof(t->next), "%.31s", f[2]);
                snprintf(t->responses, sizeof(t->responses), "%.239s", f[3]);
            }
        }
        fclose(fp);
    }
    e->loaded = e->shop_count > 0;
    return e->loaded;
}

const char* bb_extras_pool_line(const bb_extras_t* e, const char* kind, uint32_t* rng) {
    int idx[BB_MAX_DLG_LINES], n = 0;
    for (int i = 0; i < e->dlg_count; i++) {
        if (e->dlg[i].key == 0 && strcmp(e->dlg[i].kind, kind) == 0) idx[n++] = i;
    }
    if (n == 0) return "";
    *rng = *rng * 1664525u + 1013904223u;
    return e->dlg[idx[(*rng >> 8) % (uint32_t)n]].text;
}

C2D_SpriteSheet bb_extras_load_sheet(const char* name) {
    char path[96];
    snprintf(path, sizeof(path), EXTRAS_DIR "%s", name);
    return C2D_SpriteSheetLoad(path);
}
