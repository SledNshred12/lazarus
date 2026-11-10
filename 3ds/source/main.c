/* Lazarus for Nintendo 3DS / 2DS (homebrew). Buildings use the game's own art, drawn by its own code. */
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "data.h"

#define GRID 12
#define MAXB 64
#define OXS 200.0f   /* screen x of tile (0,0) corner */
#define OYS 14.0f
#define HWS 12.0f    /* half tile width, 0.6 of the game's 20 */
#define HHS 9.0f     /* half tile height, 0.6 of the game's 15 */

typedef struct { int type, lv, x, y; } Bld;
typedef struct { unsigned magic; int nb; Bld b[MAXB]; double credits, food; } Save;

static Save S;
static C2D_SpriteSheet sheets[NTYPES];
static C2D_TextBuf tbuf;
static int curx = 5, cury = 5;
static int buildMode = 0, buildType = 1;
static char note[96] = "";
static int noteT = 0;
static u64 lastMs;

#define SAVE_MAGIC 0x4C5A3331u
#define SAVE_PATH "sdmc:/3ds/lazarus3ds.sav"

static void say(const char *m) { snprintf(note, sizeof note, "%s", m); noteT = 180; }

static void defaults(void) {
  memset(&S, 0, sizeof S);
  S.magic = SAVE_MAGIC;
  S.credits = 300; S.food = 60;
  static const int d[8][4] = {{0,1,4,4},{2,1,8,8},{3,1,0,4},{4,1,8,4},{5,1,5,1},{1,1,2,8},{1,1,8,1},{6,1,1,1}};
  for (int i = 0; i < 8; i++) { S.b[i].type = d[i][0]; S.b[i].lv = d[i][1]; S.b[i].x = d[i][2]; S.b[i].y = d[i][3]; }
  S.nb = 8;
}
static void saveGame(void) {
  FILE *f = fopen(SAVE_PATH, "wb");
  if (!f) { say("Could not save"); return; }
  fwrite(&S, sizeof S, 1, f); fclose(f); say("Saved");
}
static void loadGame(void) {
  FILE *f = fopen(SAVE_PATH, "rb");
  if (f) {
    Save t;
    if (fread(&t, sizeof t, 1, f) == 1 && t.magic == SAVE_MAGIC && t.nb > 0 && t.nb <= MAXB) { S = t; fclose(f); return; }
    fclose(f);
  }
  defaults();
}

static int hallLv(void) { for (int i = 0; i < S.nb; i++) if (S.b[i].type == 0) return S.b[i].lv; return 1; }
static long upcost(int type, int lv) {
  if (lv <= 9) return COSTS[type][lv - 1];
  long c = COSTS[type][8];
  for (int i = 10; i <= lv; i++) c = c * 13 / 10;
  return c;
}
static int countType(int t) { int n = 0; for (int i = 0; i < S.nb; i++) if (S.b[i].type == t) n++; return n; }
static int limitFor(int t) { int h = hallLv(); if (h > 10) h = 10; return LIMS[t][h - 1]; }
static int bldAt(int x, int y) {
  for (int i = 0; i < S.nb; i++) {
    const BType *d = &BTYPES[S.b[i].type];
    if (x >= S.b[i].x && x < S.b[i].x + d->w && y >= S.b[i].y && y < S.b[i].y + d->h) return i;
  }
  return -1;
}
static int canPlace(int t, int x, int y) {
  const BType *d = &BTYPES[t];
  if (x < 0 || y < 0 || x + d->w > GRID || y + d->h > GRID) return 0;
  for (int yy = y; yy < y + d->h; yy++) for (int xx = x; xx < x + d->w; xx++) if (bldAt(xx, yy) >= 0) return 0;
  return 1;
}

static void doUpgrade(void) {
  int i = bldAt(curx, cury);
  if (i < 0) { say("Nothing here to upgrade"); return; }
  Bld *b = &S.b[i];
  if (b->lv >= 20) { say("Already at level 20"); return; }
  if (b->type != 0 && b->lv >= hallLv() + 1) { say("Upgrade the Bunker first"); return; }
  long c = upcost(b->type, b->lv);
  if (S.credits < c) { say("Not enough credits"); return; }
  S.credits -= c; b->lv++; say("Upgraded");
}
static void doPlace(void) {
  int t = buildType;
  if (countType(t) >= limitFor(t)) { say("Build limit reached. Upgrade the Bunker"); return; }
  if (S.nb >= MAXB) { say("No room for more"); return; }
  if (!canPlace(t, curx, cury)) { say("Does not fit there"); return; }
  if (S.credits < BTYPES[t].base) { say("Not enough credits"); return; }
  S.credits -= BTYPES[t].base;
  S.b[S.nb].type = t; S.b[S.nb].lv = 1; S.b[S.nb].x = curx; S.b[S.nb].y = cury; S.nb++;
  buildMode = 0; say("Built");
}
static void nextType(int d) { buildType += d; if (buildType < 1) buildType = NTYPES - 1; if (buildType >= NTYPES) buildType = 1; }

static void tick(void) {
  u64 now = osGetTime(); double dt = (now - lastMs) / 1000.0; lastMs = now;
  if (dt > 5) dt = 5;
  for (int i = 0; i < S.nb; i++) {
    if (S.b[i].type == 2) S.credits += 0.4 * S.b[i].lv * dt;
    if (S.b[i].type == 1) S.food += 0.25 * S.b[i].lv * dt;
  }
}

static void txt(float x, float y, float s, u32 col, const char *fmt, ...) {
  char buf[128]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
  C2D_Text t; C2D_TextParse(&t, tbuf, buf); C2D_TextOptimize(&t);
  C2D_DrawText(&t, C2D_WithColor, x, y, 0.6f, s, s, col);
}
static float sx(float gx, float gy) { return OXS + (gx - gy) * HWS; }
static float sy(float gx, float gy) { return OYS + (gx + gy) * HHS; }
static void quad(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy, u32 c) {
  C2D_DrawTriangle(ax, ay, c, bx, by, c, cx, cy, c, 0.1f);
  C2D_DrawTriangle(ax, ay, c, cx, cy, c, dx, dy, c, 0.1f);
}

static void drawGround(void) {
  u32 side1 = C2D_Color32(0x4B, 0x38, 0x24, 255), side2 = C2D_Color32(0x3A, 0x2A, 0x1A, 255);
  float th = 10;
  /* slab sides */
  quad(sx(0, GRID), sy(0, GRID), sx(GRID, GRID), sy(GRID, GRID), sx(GRID, GRID), sy(GRID, GRID) + th, sx(0, GRID), sy(0, GRID) + th, side1);
  quad(sx(GRID, GRID), sy(GRID, GRID), sx(GRID, 0), sy(GRID, 0), sx(GRID, 0), sy(GRID, 0) + th, sx(GRID, GRID), sy(GRID, GRID) + th, side2);
  for (int y = 0; y < GRID; y++) for (int x = 0; x < GRID; x++) {
    u32 c = ((x + y) & 1) ? C2D_Color32(0xC9, 0xAE, 0x8A, 255) : C2D_Color32(0xBA, 0x9E, 0x7A, 255);
    quad(sx(x, y), sy(x, y), sx(x + 1, y), sy(x + 1, y), sx(x + 1, y + 1), sy(x + 1, y + 1), sx(x, y + 1), sy(x, y + 1), c);
  }
}
static void outline(int gx, int gy, int w, int h, u32 c) {
  float x0 = sx(gx, gy), y0 = sy(gx, gy), x1 = sx(gx + w, gy), y1 = sy(gx + w, gy);
  float x2 = sx(gx + w, gy + h), y2 = sy(gx + w, gy + h), x3 = sx(gx, gy + h), y3 = sy(gx, gy + h);
  C2D_DrawLine(x0, y0, c, x1, y1, c, 2, 0.5f); C2D_DrawLine(x1, y1, c, x2, y2, c, 2, 0.5f);
  C2D_DrawLine(x2, y2, c, x3, y3, c, 2, 0.5f); C2D_DrawLine(x3, y3, c, x0, y0, c, 2, 0.5f);
}
static void drawBld(const Bld *b, float z) {
  const short *a = ANCH[b->type][b->lv - 1];
  C2D_Image im = C2D_SpriteSheetGetImage(sheets[b->type], b->lv - 1);
  C2D_DrawImageAt(im, sx(b->x, b->y) - a[0], sy(b->x, b->y) - a[1], z, NULL, 1.0f, 1.0f);
}
static int order[MAXB];
static void drawTop(void) {
  drawGround();
  for (int i = 0; i < S.nb; i++) order[i] = i;
  for (int i = 0; i < S.nb; i++) for (int j = i + 1; j < S.nb; j++) {
    const Bld *p = &S.b[order[i]], *q = &S.b[order[j]];
    int kp = p->x + p->y + BTYPES[p->type].w + BTYPES[p->type].h, kq = q->x + q->y + BTYPES[q->type].w + BTYPES[q->type].h;
    if (kq < kp) { int t = order[i]; order[i] = order[j]; order[j] = t; }
  }
  for (int i = 0; i < S.nb; i++) drawBld(&S.b[order[i]], 0.2f + i * 0.004f);
  if (buildMode) {
    int ok = canPlace(buildType, curx, cury);
    outline(curx, cury, BTYPES[buildType].w, BTYPES[buildType].h, ok ? C2D_Color32(120, 255, 120, 255) : C2D_Color32(255, 90, 90, 255));
  } else {
    int i = bldAt(curx, cury);
    if (i >= 0) outline(S.b[i].x, S.b[i].y, BTYPES[S.b[i].type].w, BTYPES[S.b[i].type].h, C2D_Color32(255, 255, 255, 255));
    else outline(curx, cury, 1, 1, C2D_Color32(255, 255, 255, 255));
  }
  C2D_DrawRectSolid(0, 0, 0.55f, 400, 18, C2D_Color32(20, 14, 10, 200));
  txt(6, 1, 0.5f, C2D_Color32(0xE8, 0xD8, 0xB8, 255), "LAZARUS");
  txt(150, 1, 0.5f, C2D_Color32(0xF2, 0xB9, 0x30, 255), "Credits %ld", (long)S.credits);
  txt(270, 1, 0.5f, C2D_Color32(0x9C, 0xD0, 0x6A, 255), "Food %ld", (long)S.food);
}

typedef struct { int x, y, w, h; const char *label; int id; } Btn;
static Btn btns[4]; static int nbtn;
static void drawBtn(const Btn *b, u32 col) {
  C2D_DrawRectSolid(b->x, b->y, 0.3f, b->w, b->h, col);
  txt(b->x + 8, b->y + b->h / 2 - 9, 0.55f, C2D_Color32(255, 255, 255, 255), "%s", b->label);
}
static void drawBottom(void) {
  nbtn = 0;
  u32 white = C2D_Color32(255, 255, 255, 255), dim = C2D_Color32(0xB0, 0xA8, 0x98, 255);
  if (!buildMode) {
    int i = bldAt(curx, cury);
    if (i >= 0) {
      const Bld *b = &S.b[i];
      txt(10, 8, 0.7f, white, "%s", BTYPES[b->type].name);
      txt(10, 34, 0.55f, dim, "Level %d of 20", b->lv);
      if (b->lv < 20) txt(10, 54, 0.55f, white, "Upgrade: %ld credits", upcost(b->type, b->lv));
      else txt(10, 54, 0.55f, white, "Max level");
    } else txt(10, 8, 0.6f, dim, "Empty ground");
    btns[nbtn++] = (Btn){10, 110, 145, 50, "Upgrade (A)", 1};
    btns[nbtn++] = (Btn){165, 110, 145, 50, "Build (X)", 2};
    btns[nbtn++] = (Btn){10, 170, 145, 50, "Save (Y)", 3};
    drawBtn(&btns[0], C2D_Color32(0x28, 0x58, 0x90, 255));
    drawBtn(&btns[1], C2D_Color32(0x30, 0x70, 0x50, 255));
    drawBtn(&btns[2], C2D_Color32(0x55, 0x55, 0x55, 255));
    txt(165, 182, 0.45f, dim, "D-pad moves");
    txt(165, 198, 0.45f, dim, "START quits");
  } else {
    int n = countType(buildType), lim = limitFor(buildType);
    txt(10, 8, 0.7f, white, "Build: %s", BTYPES[buildType].name);
    txt(10, 34, 0.55f, dim, "Cost %d credits", BTYPES[buildType].base);
    txt(10, 54, 0.55f, dim, "Owned %d of %d", n, lim);
    txt(10, 76, 0.5f, dim, "L / R switch type, D-pad moves");
    btns[nbtn++] = (Btn){10, 110, 145, 50, "Place (A)", 4};
    btns[nbtn++] = (Btn){165, 110, 145, 50, "Next (R)", 5};
    btns[nbtn++] = (Btn){10, 170, 145, 50, "Cancel (B)", 6};
    drawBtn(&btns[0], C2D_Color32(0x30, 0x70, 0x50, 255));
    drawBtn(&btns[1], C2D_Color32(0x28, 0x58, 0x90, 255));
    drawBtn(&btns[2], C2D_Color32(0x90, 0x38, 0x30, 255));
  }
  if (noteT > 0) { noteT--; txt(10, 228 - 14, 0.5f, C2D_Color32(0xF2, 0xB9, 0x30, 255), "%s", note); }
}
static void act(int id) {
  switch (id) {
    case 1: doUpgrade(); break;
    case 2: buildMode = 1; break;
    case 3: saveGame(); break;
    case 4: doPlace(); break;
    case 5: nextType(1); break;
    case 6: buildMode = 0; break;
  }
}

int main(void) {
  gfxInitDefault(); C3D_Init(C3D_DEFAULT_CMDBUF_SIZE); C2D_Init(C2D_DEFAULT_MAX_OBJECTS); C2D_Prepare();
  romfsInit();
  C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT), *bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
  tbuf = C2D_TextBufNew(4096);
  for (int i = 0; i < NTYPES; i++) {
    char p[64]; snprintf(p, sizeof p, "romfs:/gfx/%s.t3x", BTYPES[i].id);
    sheets[i] = C2D_SpriteSheetLoad(p);
    if (!sheets[i]) { gfxExit(); consoleInit(GFX_TOP, NULL); printf("Missing %s\n", p); while (aptMainLoop()) { hidScanInput(); if (hidKeysDown() & KEY_START) break; gspWaitForVBlank(); } return 1; }
  }
  loadGame(); lastMs = osGetTime();
  u64 lastSave = lastMs;
  while (aptMainLoop()) {
    hidScanInput();
    u32 kd = hidKeysDown();
    if (kd & KEY_START) break;
    if (kd & KEY_DUP) { if (cury > 0) cury--; }
    if (kd & KEY_DDOWN) { if (cury < GRID - 1) cury++; }
    if (kd & KEY_DLEFT) { if (curx > 0) curx--; }
    if (kd & KEY_DRIGHT) { if (curx < GRID - 1) curx++; }
    if (buildMode) {
      if (kd & KEY_L) nextType(-1);
      if (kd & KEY_R) nextType(1);
      if (kd & KEY_A) doPlace();
      if (kd & KEY_B) buildMode = 0;
    } else {
      if (kd & KEY_A) doUpgrade();
      if (kd & KEY_X) buildMode = 1;
      if (kd & KEY_Y) saveGame();
    }
    if (kd & KEY_TOUCH) {
      touchPosition tp; hidTouchRead(&tp);
      for (int i = 0; i < nbtn; i++) if (tp.px >= btns[i].x && tp.px < btns[i].x + btns[i].w && tp.py >= btns[i].y && tp.py < btns[i].y + btns[i].h) { act(btns[i].id); break; }
    }
    tick();
    if (osGetTime() - lastSave > 30000) { saveGame(); lastSave = osGetTime(); noteT = 0; }
    C2D_TextBufClear(tbuf);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(top, C2D_Color32(0x5A, 0x3A, 0x30, 255));
    C2D_SceneBegin(top); drawTop();
    C2D_TargetClear(bot, C2D_Color32(0x1E, 0x1A, 0x16, 255));
    C2D_SceneBegin(bot); drawBottom();
    C3D_FrameEnd(0);
  }
  saveGame();
  for (int i = 0; i < NTYPES; i++) if (sheets[i]) C2D_SpriteSheetFree(sheets[i]);
  C2D_TextBufDelete(tbuf); C2D_Fini(); C3D_Fini(); romfsExit(); gfxExit();
  return 0;
}
