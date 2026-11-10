/* Lazarus for Nintendo 3DS / 2DS (homebrew). Buildings and crops use the game's own art, drawn by its own code. */
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <math.h>
#include "data.h"

#define GRID 12
#define MAXB 64
#define OXS 200.0f
#define OYS 14.0f
#define HWS 12.0f
#define HHS 9.0f

typedef struct { int type, lv, x, y, crop; long long t0; float st; } Bld;
typedef struct {
  unsigned magic; int nb; Bld b[MAXB];
  double credits, food; int seeds[NCROPS]; int cropSel; long long savedAt;
} Save;

static Save S;
static C2D_SpriteSheet sheets[NTYPES], cropSheet;
static C2D_TextBuf tbuf;
static int curx = 5, cury = 5;
static int buildMode = 0, buildType = 1;
static char note[96] = "";
static int noteT = 0;
static u64 lastMs;

#define SAVE_MAGIC 0x4C5A3332u
#define SAVE_PATH "sdmc:/3ds/lazarus3ds.sav"

static void say(const char *m) { snprintf(note, sizeof note, "%s", m); noteT = 180; }
static long long nowS(void) { return (long long)time(NULL); }

static void defaults(void) {
  memset(&S, 0, sizeof S);
  S.magic = SAVE_MAGIC; S.credits = 300; S.food = 60;
  S.seeds[0] = 10; S.seeds[1] = 5; S.seeds[2] = 3;
  static const int d[8][4] = {{0,1,4,4},{2,1,8,8},{3,1,0,4},{4,1,8,4},{5,1,5,1},{1,1,2,8},{1,1,8,1},{6,1,1,1}};
  for (int i = 0; i < 8; i++) { S.b[i].type = d[i][0]; S.b[i].lv = d[i][1]; S.b[i].x = d[i][2]; S.b[i].y = d[i][3]; S.b[i].crop = -1; }
  S.b[5].crop = 0; S.b[5].t0 = nowS() - 50;   /* like the game: one garden starts planted */
  S.nb = 8; S.savedAt = nowS();
}
static void saveGame(void) {
  S.savedAt = nowS();
  FILE *f = fopen(SAVE_PATH, "wb");
  if (!f) { say("Could not save"); return; }
  fwrite(&S, sizeof S, 1, f); fclose(f); say("Saved");
}
static int bestLv(int t) { int m = 0; for (int i = 0; i < S.nb; i++) if (S.b[i].type == t && S.b[i].lv > m) m = S.b[i].lv; return m; }
static float tradeRate(const Bld *b) { return 20.0f * b->lv / 60.0f; }
static float tradeCap(const Bld *b) { return 200.0f * b->lv; }
static void loadGame(void) {
  FILE *f = fopen(SAVE_PATH, "rb");
  if (f) {
    Save t;
    if (fread(&t, sizeof t, 1, f) == 1 && t.magic == SAVE_MAGIC && t.nb > 0 && t.nb <= MAXB) {
      S = t; fclose(f);
      long long away = nowS() - S.savedAt;   /* things earned while the app was closed */
      if (away > 0) for (int i = 0; i < S.nb; i++) if (S.b[i].type == 2) {
        S.b[i].st += tradeRate(&S.b[i]) * (float)away;
        if (S.b[i].st > tradeCap(&S.b[i])) S.b[i].st = tradeCap(&S.b[i]);
      }
      return;
    }
    fclose(f);
  }
  defaults();
}

static int hallLv(void) { for (int i = 0; i < S.nb; i++) if (S.b[i].type == 0) return S.b[i].lv; return 1; }
static double buildMult(void) { double m = S.credits / 100000.0; if (m > 2) m = 2; if (m < 0) m = 0; return 1 + m; }
static long upcost(int type, int lv) { return (long)floor(COSTS[type][lv - 1] * buildMult() / 10.0 + 0.5) * 10; }
static int countType(int t) { int n = 0; for (int i = 0; i < S.nb; i++) if (S.b[i].type == t) n++; return n; }
static long buildcost(int t) { return (long)floor(BTYPES[t].base * (1 + countType(t) * 0.5) * buildMult() / 10.0 + 0.5) * 10; }
static int limitFor(int t) { int h = hallLv(); if (h > 20) h = 20; return LIMS[t][h - 1]; }
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

/* ---- farming ---- */
static float cropProg(const Bld *b) {
  if (b->crop < 0) return 0;
  float p = (float)(nowS() - b->t0) / CROPS[b->crop].sec; return p < 0 ? 0 : (p > 1 ? 1 : p);
}
static int cropReady(const Bld *b) { return b->type == 1 && b->crop >= 0 && cropProg(b) >= 1.0f; }
static int cropLeft(const Bld *b) { long long l = CROPS[b->crop].sec - (nowS() - b->t0); return l < 0 ? 0 : (int)l; }
static int cropYield(const Bld *b) { return (int)floor(CROPS[b->crop].food * (1 + 0.25 * (b->lv - 1)) + 0.5); }

static void doCollect(void) {
  long food = 0, cr = 0;
  for (int i = 0; i < S.nb; i++) {
    Bld *b = &S.b[i];
    if (cropReady(b)) { food += cropYield(b); b->crop = -1; b->t0 = 0; }
    if (b->type == 2) { int a = (int)floorf(b->st); if (a > 0) { cr += a; b->st -= a; } }
  }
  if (!food && !cr) { say("Nothing is ready yet"); return; }
  S.food += food; S.credits += cr;
  char m[64]; snprintf(m, sizeof m, "+%ld food, +%ld credits", food, cr); say(m);
}
static void doPlant(void) {
  int c = S.cropSel, n = 0, empty = 0;
  if (CROPS[c].hall > hallLv()) { say("Upgrade the Bunker for this crop"); return; }
  for (int i = 0; i < S.nb; i++) if (S.b[i].type == 1 && S.b[i].crop < 0) {
    empty++;
    if (S.seeds[c] > 0) { S.b[i].crop = c; S.b[i].t0 = nowS(); S.seeds[c]--; n++; }
  }
  if (!empty) { say("No empty gardens"); return; }
  if (!n) { say("Out of seeds. Buy some"); return; }
  char m[64]; snprintf(m, sizeof m, "Planted %d", n); say(m);
}
static void doBuySeeds(void) {
  int c = S.cropSel; long cost = CROPS[c].seed * 10L;
  if (CROPS[c].hall > hallLv()) { say("Upgrade the Bunker for this crop"); return; }
  if (S.credits < cost) { say("Not enough credits"); return; }
  S.credits -= cost; S.seeds[c] += 10; say("Bought 10 seeds");
}
static void doSell(void) {
  long f = (long)floor(S.food);
  if (f < 1) { say("No food to sell"); return; }
  double rate = 0.5 + 0.1 * bestLv(2);
  long c = (long)floor(f * rate); S.food -= f; S.credits += c;
  char m[64]; snprintf(m, sizeof m, "Sold food for %ld", c); say(m);
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
  if (countType(t) >= limitFor(t)) { say("Limit reached. Upgrade the Bunker"); return; }
  if (S.nb >= MAXB) { say("No room for more"); return; }
  if (!canPlace(t, curx, cury)) { say("Does not fit there"); return; }
  long c = buildcost(t);
  if (S.credits < c) { say("Not enough credits"); return; }
  S.credits -= c;
  Bld *b = &S.b[S.nb++]; memset(b, 0, sizeof *b); b->type = t; b->lv = 1; b->x = curx; b->y = cury; b->crop = -1;
  buildMode = 0; say("Built");
}
static void nextType(int d) { buildType += d; if (buildType < 1) buildType = NTYPES - 1; if (buildType >= NTYPES) buildType = 1; }
static void nextCrop(int d) { S.cropSel = (S.cropSel + d + NCROPS) % NCROPS; }

static void tick(void) {
  u64 now = osGetTime(); double dt = (now - lastMs) / 1000.0; lastMs = now;
  if (dt > 5) dt = 5;
  for (int i = 0; i < S.nb; i++) if (S.b[i].type == 2) {
    S.b[i].st += tradeRate(&S.b[i]) * (float)dt;
    if (S.b[i].st > tradeCap(&S.b[i])) S.b[i].st = tradeCap(&S.b[i]);
  }
}

/* ---- drawing ---- */
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
  C2D_DrawLine(x0, y0, c, x1, y1, c, 2, 0.58f); C2D_DrawLine(x1, y1, c, x2, y2, c, 2, 0.58f);
  C2D_DrawLine(x2, y2, c, x3, y3, c, 2, 0.58f); C2D_DrawLine(x3, y3, c, x0, y0, c, 2, 0.58f);
}
static void drawBld(const Bld *b, float z) {
  const short *a = ANCH[b->type][b->lv - 1];
  C2D_Image im = C2D_SpriteSheetGetImage(sheets[b->type], b->lv - 1);
  float px = sx(b->x, b->y) - a[0], py = sy(b->x, b->y) - a[1];
  C2D_DrawImageAt(im, px, py, z, NULL, 1.0f, 1.0f);
  if (b->type == 1 && b->crop >= 0) {
    float p = cropProg(b); int stg = p < 0.34f ? 0 : (p < 0.8f ? 1 : 2);
    const short *ca = CROPANCH[b->crop][stg];
    C2D_Image ci = C2D_SpriteSheetGetImage(cropSheet, b->crop * 3 + stg);
    C2D_DrawImageAt(ci, sx(b->x, b->y) - ca[0], sy(b->x, b->y) - ca[1], z + 0.002f, NULL, 1.0f, 1.0f);
  }
  int bubble = 0; u32 col = 0;
  if (cropReady(b)) { bubble = 1; col = C2D_Color32(0x8F, 0xD0, 0x4A, 255); }
  else if (b->type == 2 && b->st >= 10) { bubble = 1; col = C2D_Color32(0xF2, 0xB9, 0x30, 255); }
  if (bubble) {
    float cx = px + (float)im.subtex->width / 2, cy = py - 5;
    C2D_DrawCircleSolid(cx, cy, 0.56f, 6, C2D_Color32(255, 255, 255, 255));
    C2D_DrawCircleSolid(cx, cy, 0.57f, 4, col);
  }
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
  txt(110, 1, 0.5f, C2D_Color32(0xF2, 0xB9, 0x30, 255), "Credits %ld", (long)S.credits);
  txt(250, 1, 0.5f, C2D_Color32(0x9C, 0xD0, 0x6A, 255), "Food %ld", (long)S.food);
}

typedef struct { int x, y, w, h; char label[24]; int id; u32 col; } Btn;
static Btn btns[8]; static int nbtn;
static void addBtn(int slot, const char *label, int id, u32 col) {
  Btn *b = &btns[nbtn++];
  b->x = 8 + (slot % 2) * 156; b->y = 72 + (slot / 2) * 36; b->w = 148; b->h = 32; b->id = id; b->col = col;
  snprintf(b->label, sizeof b->label, "%s", label);
  C2D_DrawRectSolid(b->x, b->y, 0.3f, b->w, b->h, col);
  txt(b->x + 8, b->y + 5, 0.5f, C2D_Color32(255, 255, 255, 255), "%s", b->label);
}
static void drawBottom(void) {
  nbtn = 0;
  u32 white = C2D_Color32(255, 255, 255, 255), dim = C2D_Color32(0xB0, 0xA8, 0x98, 255);
  u32 blue = C2D_Color32(0x28, 0x58, 0x90, 255), green = C2D_Color32(0x30, 0x70, 0x50, 255), grey = C2D_Color32(0x55, 0x55, 0x55, 255);
  u32 gold = C2D_Color32(0x8A, 0x6A, 0x20, 255), red = C2D_Color32(0x90, 0x38, 0x30, 255);
  char lab[24];
  if (!buildMode) {
    int i = bldAt(curx, cury);
    if (i >= 0) {
      const Bld *b = &S.b[i];
      txt(8, 2, 0.62f, white, "%s  Lv %d", BTYPES[b->type].name, b->lv);
      if (b->lv < 20) txt(8, 22, 0.48f, white, "Upgrade: %ld credits", upcost(b->type, b->lv)); else txt(8, 22, 0.48f, white, "Max level");
      if (b->type == 1) {
        if (b->crop < 0) txt(8, 38, 0.48f, dim, "Empty. Plant a crop");
        else if (cropReady(b)) txt(8, 38, 0.48f, C2D_Color32(0x8F, 0xD0, 0x4A, 255), "%s ready: %d food", CROPS[b->crop].name, cropYield(b));
        else txt(8, 38, 0.48f, dim, "%s %d%%, %ds left", CROPS[b->crop].name, (int)(cropProg(b) * 100), cropLeft(b));
      } else if (b->type == 2) txt(8, 38, 0.48f, dim, "Stored %d of %d credits", (int)b->st, (int)tradeCap(b));
    } else txt(8, 2, 0.55f, dim, "Empty ground");
    txt(8, 54, 0.42f, dim, "Seeds  Wheat %d  Corn %d  Berries %d", S.seeds[0], S.seeds[1], S.seeds[2]);
    addBtn(0, "Collect all (B)", 7, gold);
    addBtn(1, "Upgrade (A)", 1, blue);
    snprintf(lab, sizeof lab, "Plant %s (Y)", CROPS[S.cropSel].name); addBtn(2, lab, 8, green);
    addBtn(3, "Build (X)", 2, green);
    addBtn(4, "Sell food (SEL)", 9, gold);
    snprintf(lab, sizeof lab, "Seeds x10: %dcr", CROPS[S.cropSel].seed * 10); addBtn(5, lab, 10, grey);
    snprintf(lab, sizeof lab, "Crop: %s (L/R)", CROPS[S.cropSel].name); addBtn(6, lab, 11, grey);
    addBtn(7, "Save", 3, grey);
  } else {
    txt(8, 2, 0.62f, white, "Build: %s", BTYPES[buildType].name);
    txt(8, 22, 0.48f, dim, "Cost %ld credits", buildcost(buildType));
    txt(8, 38, 0.48f, dim, "Owned %d of %d", countType(buildType), limitFor(buildType));
    txt(8, 54, 0.42f, dim, "D-pad moves. L / R switch type");
    addBtn(0, "Place (A)", 4, green);
    addBtn(1, "Next (R)", 5, blue);
    addBtn(2, "Previous (L)", 12, blue);
    addBtn(3, "Cancel (B)", 6, red);
  }
  if (noteT > 0) { noteT--; txt(8, 218, 0.5f, C2D_Color32(0xF2, 0xB9, 0x30, 255), "%s", note); }
}
static void act(int id) {
  switch (id) {
    case 1: doUpgrade(); break;
    case 2: buildMode = 1; break;
    case 3: saveGame(); break;
    case 4: doPlace(); break;
    case 5: nextType(1); break;
    case 6: buildMode = 0; break;
    case 7: doCollect(); break;
    case 8: doPlant(); break;
    case 9: doSell(); break;
    case 10: doBuySeeds(); break;
    case 11: nextCrop(1); break;
    case 12: nextType(-1); break;
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
  cropSheet = C2D_SpriteSheetLoad("romfs:/gfx/crops.t3x");
  if (!cropSheet) { gfxExit(); consoleInit(GFX_TOP, NULL); printf("Missing crops\n"); while (aptMainLoop()) { hidScanInput(); if (hidKeysDown() & KEY_START) break; gspWaitForVBlank(); } return 1; }
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
      if (kd & KEY_L) nextCrop(-1);
      if (kd & KEY_R) nextCrop(1);
      if (kd & KEY_A) doUpgrade();
      if (kd & KEY_B) doCollect();
      if (kd & KEY_X) buildMode = 1;
      if (kd & KEY_Y) doPlant();
      if (kd & KEY_SELECT) doSell();
    }
    if (kd & KEY_TOUCH) {
      touchPosition tp; hidTouchRead(&tp);
      for (int i = 0; i < nbtn; i++) if (tp.px >= btns[i].x && tp.px < btns[i].x + btns[i].w && tp.py >= btns[i].y && tp.py < btns[i].y + btns[i].h) { act(btns[i].id); break; }
    }
    tick();
    if (osGetTime() - lastSave > 30000) { int keep = noteT; saveGame(); lastSave = osGetTime(); noteT = keep; }
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
  C2D_SpriteSheetFree(cropSheet);
  C2D_TextBufDelete(tbuf); C2D_Fini(); C3D_Fini(); romfsExit(); gfxExit();
  return 0;
}
