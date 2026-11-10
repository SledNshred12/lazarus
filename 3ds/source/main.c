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
#include "colonies.h"

#define GRID 12
#define MAXB 64
#define OXS 200.0f
#define OYS 14.0f
#define HWS 12.0f
#define HHS 9.0f

typedef struct { int type, lv, x, y, crop; long long t0; float st; } Bld;
typedef struct { char name[24]; int vec, ci, tr[5], samples; } Strain;
typedef struct {
  unsigned magic; int nb; Bld b[MAXB];
  double credits, food; int seeds[NCROPS]; int cropSel; long long savedAt;
  Strain str[6]; int nstr, trophies, peak, bossWins;
  int infected, wins, losses, raidAt[10];
  int reserved[499];   /* room for later stages so saves keep working */
} Save;

static Save S;
static C2D_SpriteSheet sheets[NTYPES], cropSheet, viSheet;
static int labMode = 0, labSel = 0, labRow = 0;
static C2D_TextBuf tbuf;
static int curx = 5, cury = 5;
static int buildMode = 0, buildType = 1;
static char note[96] = "";
static int noteT = 0;
static u64 lastMs;

#define SAVE_MAGIC 0x4C5A3333u   /* stays the same: later stages only use the reserved space */
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
  Strain *v = &S.str[0]; snprintf(v->name, sizeof v->name, "Alpha-1"); v->vec = 0; v->ci = 0; v->tr[0] = 4; v->tr[1] = 3; v->samples = 5; S.nstr = 1;
}
static void saveGame(void) {
  S.savedAt = nowS();
  FILE *f = fopen(SAVE_PATH, "wb");
  if (!f) { say("Could not save"); return; }
  fwrite(&S, sizeof S, 1, f); fclose(f); say("Saved");
}
static int bestLv(int t) { int m = 0; for (int i = 0; i < S.nb; i++) if (S.b[i].type == t && S.b[i].lv > m) m = S.b[i].lv; return m; }
static float infectMult(void) { if (!S.infected) return 1.0f; float m = 0.4f + 0.1f * bestLv(5); return m > 1 ? 1 : m; }
static float tradeRate(const Bld *b) { return 20.0f * b->lv / 60.0f * infectMult(); }
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

/* ---- farming ---- */
static float cropProg(const Bld *b) {
  if (b->crop < 0) return 0;
  float p = (float)(nowS() - b->t0) / CROPS[b->crop].sec; return p < 0 ? 0 : (p > 1 ? 1 : p);
}
static int cropReady(const Bld *b) { return b->type == 1 && b->crop >= 0 && cropProg(b) >= 1.0f; }
static int cropLeft(const Bld *b) { long long l = CROPS[b->crop].sec - (nowS() - b->t0); return l < 0 ? 0 : (int)l; }
static int cropYield(const Bld *b) { return (int)floor(CROPS[b->crop].food * (1 + 0.25 * (b->lv - 1)) * infectMult() + 0.5); }

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

/* ---- virus lab ---- */
static int strainPts(const Strain *v) { int n = 0; for (int k = 0; k < 5; k++) n += v->tr[k]; return n; }
static int labPoints(void) { return 8 + 3 * bestLv(4); }
static int traitCap(void) { int c = 4 + bestLv(4); return c > 10 ? 10 : c; }
static int maxStrains(void) { int m = 2 + hallLv(); return m > 6 ? 6 : m; }
static int sampleCap(void) { int c = 0; for (int i = 0; i < S.nb; i++) if (S.b[i].type == 3) c += 4 + 2 * S.b[i].lv; return c; }
static int sampleCount(void) { int n = 0; for (int i = 0; i < S.nstr; i++) n += S.str[i].samples; return n; }
static int cultureCost(const Strain *v) { double m = 1 + (S.food > 0 ? S.food : 0) / 10000.0; return (int)floor((8 + 2 * strainPts(v)) * m + 0.5); }
static int traitOpen(int k) { return S.peak >= TRAITS[k].unlock; }
static int vecOpen(int v) { return S.peak >= VECS[v].unlock; }
static void randName(char *out, int n) { snprintf(out, n, "%s %s", PRE[rand() % 12], SUF[rand() % 10]); }
static Strain *curStrain(void) { if (labSel >= S.nstr) labSel = S.nstr - 1; if (labSel < 0) labSel = 0; return &S.str[labSel]; }
static void labAdj(int d) {
  Strain *v = curStrain(); int k = labRow;
  if (d > 0) {
    if (!traitOpen(k)) { char m[48]; snprintf(m, sizeof m, "Needs %d trophies", TRAITS[k].unlock); say(m); return; }
    if (v->tr[k] >= traitCap()) { say("Research Lab limit reached"); return; }
    if (strainPts(v) >= labPoints()) { say("No design points left"); return; }
    v->tr[k]++;
  } else if (v->tr[k] > 0) v->tr[k]--;
}
static void labVec(void) { Strain *v = curStrain(); for (int i = 1; i <= 5; i++) { int n = (v->vec + i) % 5; if (vecOpen(n)) { v->vec = n; return; } } }
static void labCulture(int many) {
  Strain *v = curStrain(); int cost = cultureCost(v), space = sampleCap() - sampleCount(), afford = (int)(S.food / cost);
  int n = many ? 1000 : 1; if (n > space) n = space; if (n > afford) n = afford;
  if (n <= 0) { say(space <= 0 ? "Culture Vaults are full" : "Not enough food"); return; }
  S.food -= (double)n * cost; v->samples += n;
  char m[48]; snprintf(m, sizeof m, "Cultured %d", n); say(m);
}
static void labNew(void) {
  if (S.nstr >= maxStrains()) { say("Strain limit. Upgrade the Bunker"); return; }
  int used[6] = {0}, ci = 0; for (int i = 0; i < S.nstr; i++) used[S.str[i].ci % 6] = 1;
  while (used[ci] && ci < 5) ci++;
  Strain *v = &S.str[S.nstr]; memset(v, 0, sizeof *v); randName(v->name, sizeof v->name); v->ci = ci; v->tr[0] = 3; v->tr[1] = 2;
  labSel = S.nstr++; say("New strain");
}
static void labDel(void) {
  if (S.nstr < 2) { say("Keep at least one strain"); return; }
  for (int i = labSel; i < S.nstr - 1; i++) S.str[i] = S.str[i + 1];
  S.nstr--; if (labSel >= S.nstr) labSel = S.nstr - 1; say("Deleted");
}

/* ---- raids: the outbreak simulation, ported from the game ---- */
typedef struct { float grow, trans, lethal, radius, vdet; int res, stealth, mut, vec; } SP;
typedef struct { float hp, max, inf, den; int sid, fallen; } BB;
static struct {
  int col, n, ended, win, pct, stars, coreDown, breach, sel, steering;
  float t, dur, energy, cap, regen, alert, cure, surgeCd, mutCd, mutT, idleT, cx, cy, steerX, steerY, warnT, seedFlash;
  int avail[6]; BB b[48]; int order[48]; SP sp[6];
  long loot; int dT; char reason[48];
  struct { float x, y, t; int ci; } fx[16];
} Bt;
static C2D_SpriteSheet colSp, colBg;
static int colLoaded = -1, raidMode = 0, raidSel = 0;

static void loadCol(int t) {
  if (colLoaded == t) return;
  if (colSp) C2D_SpriteSheetFree(colSp);
  if (colBg) C2D_SpriteSheetFree(colBg);
  char p[64];
  snprintf(p, sizeof p, "romfs:/gfx/colsp_%d.t3x", t); colSp = C2D_SpriteSheetLoad(p);
  snprintf(p, sizeof p, "romfs:/gfx/colbg_%d.t3x", t); colBg = C2D_SpriteSheetLoad(p);
  colLoaded = t;
}
static void sortOrder(int t) {
  const ColDef *c = &COLS[t];
  for (int i = 0; i < c->n; i++) Bt.order[i] = i;
  for (int i = 0; i < c->n; i++) for (int j = i + 1; j < c->n; j++)
    if (c->b[Bt.order[j]].x + c->b[Bt.order[j]].y < c->b[Bt.order[i]].x + c->b[Bt.order[i]].y) { int q = Bt.order[i]; Bt.order[i] = Bt.order[j]; Bt.order[j] = q; }
}
static void calcParams(void) {
  for (int i = 0; i < S.nstr; i++) {
    const Strain *v = &S.str[i]; const Vec *vc = &VECS[v->vec]; float pen = S.infected ? 0.85f : 1.0f; SP *p = &Bt.sp[i];
    p->grow = (0.10f + 0.045f * v->tr[0]) * pen; p->trans = (0.04f + 0.03f * v->tr[0]) * vc->trans * pen;
    p->lethal = 1.0f + 2.0f * v->tr[1]; p->radius = (float)vc->radius; p->vdet = vc->detect;
    p->res = v->tr[2]; p->stealth = v->tr[3]; p->mut = v->tr[4]; p->vec = v->vec;
  }
}
static int firstAvail(void) { for (int i = 0; i < S.nstr; i++) if (Bt.avail[i] > 0) return i; return -1; }
static void recalc(void) {
  float tot = 0, lost = 0; int fallen = 0;
  for (int i = 0; i < Bt.n; i++) { tot += Bt.b[i].max; lost += Bt.b[i].max - Bt.b[i].hp; if (Bt.b[i].fallen) fallen++; }
  Bt.pct = (int)floorf(lost / tot * 100);
  Bt.stars = (Bt.pct >= 50 ? 1 : 0) + (Bt.coreDown ? 1 : 0) + (fallen == Bt.n ? 1 : 0);
}
static void addFx(float x, float y, int ci) {
  for (int i = 0; i < 16; i++) if (Bt.fx[i].t <= 0) { Bt.fx[i].x = x; Bt.fx[i].y = y; Bt.fx[i].t = 0.5f; Bt.fx[i].ci = ci; return; }
}
static void endBattle(const char *why) {
  if (Bt.ended) return;
  Bt.ended = 1; snprintf(Bt.reason, sizeof Bt.reason, "%s", why); recalc();
  const ColDef *c = &COLS[Bt.col];
  for (int i = 0; i < S.nstr; i++) S.str[i].samples = Bt.avail[i];
  int win = Bt.stars > 0; long loot = 0;
  long long now = nowS(); int fresh = !(S.raidAt[Bt.col] && now - S.raidAt[Bt.col] < 600); double mult = fresh ? 1.0 : 0.4;
  if (win) {
    for (int i = 0; i < Bt.n; i++) if (Bt.b[i].fallen) loot += c->b[i].loot;
    loot = (long)floor(loot * (1 + 0.1 * (hallLv() - 1)) * mult + 0.5);
    Bt.dT = (int)floor((6 + 6 * Bt.stars + c->tier * 2) * mult + 0.5); if (Bt.dT < 1) Bt.dT = 1;
    S.trophies += Bt.dT; S.wins++; S.infected = 0; S.credits += loot; S.raidAt[Bt.col] = (int)now;
  } else {
    int l = 8 + c->tier / 2; if (l > S.trophies) l = S.trophies; Bt.dT = -l; S.trophies += Bt.dT; S.losses++; S.infected = 1;
  }
  if (S.trophies > S.peak) S.peak = S.trophies;
  Bt.win = win; Bt.loot = loot; raidMode = 3; Bt.cx = 0;
}
static void fallB(int i) {
  BB *b = &Bt.b[i]; if (b->fallen) return;
  b->fallen = 1; b->hp = 0; b->inf = 0;
  const EB *e = &COLS[Bt.col].b[i]; addFx(e->x, e->y, b->sid >= 0 ? S.str[b->sid].ci : 0);
  if (e->k == 0) Bt.coreDown = 1;
  if (e->wall && COLS[Bt.col].walled && !Bt.breach) { Bt.breach = 1; say("The wall is down! Your virus can get inside"); }
  recalc();
  if (Bt.pct >= 100) endBattle("All buildings collapsed.");
}
static void startBattle(int t) {
  loadCol(t); memset(&Bt, 0, sizeof Bt); Bt.col = t; Bt.n = COLS[t].n; sortOrder(t);
  for (int i = 0; i < Bt.n; i++) { Bt.b[i].hp = Bt.b[i].max = COLS[t].b[i].hp; Bt.b[i].den = COLS[t].b[i].den; Bt.b[i].sid = -1; }
  int total = sampleCount(), tt = total > 12 ? 12 : total;
  for (int i = 0; i < S.nstr; i++) Bt.avail[i] = S.str[i].samples;
  Bt.cap = 70 + 9 * tt; Bt.regen = 8 + 0.6f * tt; Bt.energy = Bt.cap < 55 ? Bt.cap : 55; Bt.dur = 150;
  Bt.cx = 240; Bt.cy = 200; Bt.sel = firstAvail(); calcParams(); raidMode = 2;
}
static int nearestB(float x, float y, float maxd) {
  int best = -1; float bd = 1e9f;
  for (int i = 0; i < Bt.n; i++) {
    if (Bt.b[i].fallen) continue;
    const EB *e = &COLS[Bt.col].b[i]; float d = hypotf(e->x - x, e->y - y) - e->r;
    if (d < bd) { bd = d; best = i; }
  }
  return (best >= 0 && bd <= maxd) ? best : -1;
}
static void applySteer(float dt) {
  int i = nearestB(Bt.steerX, Bt.steerY, 26); if (i < 0) return;
  BB *b = &Bt.b[i]; const EB *e = &COLS[Bt.col].b[i];
  if (COLS[Bt.col].walled && !Bt.breach && !e->wall) { if (Bt.t - Bt.warnT > 3) { Bt.warnT = Bt.t; say("Break a gap in the wall first"); } return; }
  if (b->inf >= 0.05f) {
    if (Bt.energy > 0) { b->inf += 0.7f * dt; if (b->inf > 0.92f) b->inf = 0.92f; Bt.energy -= 16 * dt; if (Bt.energy < 0) Bt.energy = 0; }
    return;
  }
  if (Bt.sel < 0 || Bt.avail[Bt.sel] <= 0) Bt.sel = firstAvail();
  if (Bt.sel < 0 || Bt.energy < 20) return;
  Bt.avail[Bt.sel]--; Bt.energy -= 20; if (b->inf < 0.45f) b->inf = 0.45f; if (b->sid < 0) b->sid = Bt.sel;
  addFx(e->x, e->y, S.str[Bt.sel].ci);
  if (Bt.sp[Bt.sel].vec == 4) {   /* spore burst also seeds the 2 nearest */
    for (int n = 0; n < 2; n++) {
      int bj = -1; float bd = 1e9f;
      for (int j = 0; j < Bt.n; j++) { if (j == i || Bt.b[j].fallen || Bt.b[j].inf >= 0.25f) continue; float d = hypotf(COLS[Bt.col].b[j].x - e->x, COLS[Bt.col].b[j].y - e->y); if (d < bd) { bd = d; bj = j; } }
      if (bj >= 0) { Bt.b[bj].inf = 0.25f; if (Bt.b[bj].sid < 0) Bt.b[bj].sid = Bt.sel; }
    }
  }
  if (Bt.avail[Bt.sel] <= 0) Bt.sel = firstAvail();
}
static void doSurge(void) {
  if (Bt.surgeCd > 0) { say("Surge is recharging"); return; }
  if (Bt.energy < 30) { say("Not enough energy to surge"); return; }
  int hot[48], nh = 0; for (int i = 0; i < Bt.n; i++) if (!Bt.b[i].fallen && Bt.b[i].inf > 0.22f && Bt.b[i].sid >= 0) hot[nh++] = i;
  if (!nh) { say("Infect a building first"); return; }
  Bt.energy -= 30; Bt.surgeCd = 6;
  for (int h = 0; h < nh; h++) {
    int o = hot[h], bj = -1; float bd = 1e9f; const EB *eo = &COLS[Bt.col].b[o];
    for (int j = 0; j < Bt.n; j++) { if (j == o || Bt.b[j].fallen || Bt.b[j].inf >= 0.2f) continue; float d = hypotf(COLS[Bt.col].b[j].x - eo->x, COLS[Bt.col].b[j].y - eo->y); if (d < bd) { bd = d; bj = j; } }
    if (bj >= 0) { if (Bt.b[bj].inf < 0.35f) Bt.b[bj].inf = 0.35f; if (Bt.b[bj].sid < 0) Bt.b[bj].sid = Bt.b[o].sid; addFx(COLS[Bt.col].b[bj].x, COLS[Bt.col].b[bj].y, S.str[Bt.b[o].sid].ci); }
  }
  say("The outbreak surges outward!");
}
static void doMutate(void) {
  if (Bt.mutCd > 0) { say("Mutation is recharging"); return; }
  if (Bt.energy < 25) { say("Not enough energy to mutate"); return; }
  Bt.energy -= 25; Bt.mutCd = 8; Bt.mutT = 5; Bt.cure -= 0.22f; if (Bt.cure < 0) Bt.cure = 0; say("Mutation! The rival cure is set back");
}
static float frand(void) { return (float)(rand() & 0xFFFF) / 65536.0f; }
static void stepBattle(float dt) {
  if (Bt.ended) return;
  const ColDef *C = &COLS[Bt.col]; const EB *E = C->b; int N = Bt.n;
  Bt.t += dt; Bt.energy += Bt.regen * dt; if (Bt.energy > Bt.cap) Bt.energy = Bt.cap;
  if (Bt.surgeCd > 0) Bt.surgeCd -= dt; if (Bt.mutCd > 0) Bt.mutCd -= dt; if (Bt.mutT > 0) Bt.mutT -= dt;
  for (int i = 0; i < 16; i++) if (Bt.fx[i].t > 0) Bt.fx[i].t -= dt;
  if (Bt.steering) applySteer(dt);
  int clin[48], nc = 0, san[48], ns = 0, vacc = 0;
  for (int i = 0; i < N; i++) { if (Bt.b[i].fallen) continue; if (E[i].k == 4) clin[nc++] = i; else if (E[i].k == 5) san[ns++] = i; else if (E[i].k == 6) vacc++; }
  /* growth, recovery, lethality */
  for (int i = 0; i < N; i++) {
    BB *b = &Bt.b[i]; if (b->fallen || b->inf <= 0) continue;
    if (b->sid < 0) { b->inf = 0; continue; }
    const SP *p = &Bt.sp[b->sid]; float rec = 0.012f;
    for (int j = 0; j < nc; j++) if (hypotf(E[clin[j]].x - E[i].x, E[clin[j]].y - E[i].y) < 100) rec += 0.06f * (1 - 0.07f * p->res) * (Bt.mutT > 0 ? 0.5f : 1.0f);
    b->inf += (p->grow * b->den * b->inf * (1 - b->inf) - rec) * dt; if (b->inf < 0) b->inf = 0; if (b->inf > 1) b->inf = 1;
    if (b->inf < 0.003f) { b->inf = 0; b->sid = -1; continue; }
    b->hp -= p->lethal * b->inf * dt;
    if (b->hp <= 0) fallB(i);
    if (Bt.ended) return;
  }
  /* spread between buildings */
  for (int i = 0; i < N; i++) {
    BB *o = &Bt.b[i]; if (o->fallen || o->inf < 0.12f || o->sid < 0) continue;
    const SP *p = &Bt.sp[o->sid];
    for (int j = 0; j < N; j++) {
      BB *b = &Bt.b[j]; if (j == i || b->fallen) continue;
      float d = hypotf(E[j].x - E[i].x, E[j].y - E[i].y); if (d >= p->radius) continue;
      if (C->walled && !Bt.breach && !E[j].wall && E[i].wall) continue;
      float m = p->trans * o->inf * (1 - d / p->radius) * b->den;
      if (p->vec == 2 && (E[j].k == 2 || E[j].k == 3)) m *= 2;
      for (int s2 = 0; s2 < ns; s2++) if (hypotf(E[san[s2]].x - E[j].x, E[san[s2]].y - E[j].y) < 95) m *= 1 - 0.45f * (1 - 0.07f * p->res);
      b->inf += m * (1 - b->inf) * dt; if (b->inf > 1) b->inf = 1;
      if (b->sid < 0 && b->inf > 0) b->sid = o->sid;
    }
  }
  /* random jumps: vermin and mutation */
  int act[6] = {0}; for (int i = 0; i < N; i++) if (!Bt.b[i].fallen && Bt.b[i].inf > 0.3f && Bt.b[i].sid >= 0) act[Bt.b[i].sid] = 1;
  for (int sidx = 0; sidx < S.nstr; sidx++) if (act[sidx]) {
    const SP *p = &Bt.sp[sidx]; float rate = (p->vec == 3 ? 0.33f : 0) + 0.04f * p->mut;
    if (rate > 0 && frand() < rate * dt) {
      int cand[48], nc2 = 0;
      for (int i = 0; i < N; i++) if (!Bt.b[i].fallen && Bt.b[i].inf < 0.3f && E[i].k != 0 && !(C->walled && !Bt.breach && !E[i].wall)) cand[nc2++] = i;
      if (nc2) { int t = cand[rand() % nc2]; if (Bt.b[t].inf < 0.15f) Bt.b[t].inf = 0.15f; if (Bt.b[t].sid < 0) Bt.b[t].sid = sidx; addFx(E[t].x, E[t].y, S.str[sidx].ci); }
    }
  }
  /* rival alert and cure */
  float vis = 0, mw = 0, mt = 0;
  for (int i = 0; i < N; i++) {
    BB *b = &Bt.b[i]; if (b->sid < 0 && !b->fallen) continue;
    int sd = b->sid; if (sd < 0) continue;
    const SP *p = &Bt.sp[sd]; float x = b->fallen ? 1.0f : b->inf; if (x <= 0) continue;
    vis += x * (1 - 0.075f * p->stealth) * p->vdet; mw += x * p->mut; mt += x;
  }
  vis /= N;
  Bt.alert += 0.14f * vis * (1 + C->tier * 0.05f) * dt; if (Bt.alert > 1) Bt.alert = 1;
  if (Bt.alert >= 1) {
    float mut = mt > 0 ? mw / mt : 0;
    Bt.cure += (0.006f + 0.003f * C->tier + 0.0035f * vacc) * (1 - 0.06f * mut) * (Bt.mutT > 0 ? 0.5f : 1.0f) * dt;
    if (Bt.cure >= 1) { Bt.cure = 1; endBattle("The rival finished a cure."); return; }
  }
  int left = 0; for (int i = 0; i < S.nstr; i++) left += Bt.avail[i];
  int anyInf = 0; for (int i = 0; i < N; i++) if (!Bt.b[i].fallen && Bt.b[i].inf > 0) { anyInf = 1; break; }
  if (left == 0 && !anyInf) { Bt.idleT += dt; if (Bt.idleT > 1.5f) endBattle("The outbreak burned out."); } else Bt.idleT = 0;
  if (Bt.t >= Bt.dur) endBattle("Time ran out.");
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

static u32 rgbOf(unsigned c) { return C2D_Color32((c >> 16) & 255, (c >> 8) & 255, c & 255, 255); }
static void drawIcon(int vec, int ci, float x, float y, float sc, float z) {
  C2D_Image im = C2D_SpriteSheetGetImage(viSheet, (vec % 5) * 6 + (ci % 6));
  C2D_DrawImageAt(im, x, y, z, NULL, sc, sc);
}
static void drawLabTop(void) {
  Strain *v = curStrain();
  u32 white = C2D_Color32(255, 255, 255, 255), dim = C2D_Color32(0xB0, 0xA8, 0x98, 255);
  C2D_DrawRectSolid(0, 0, 0.1f, 400, 240, C2D_Color32(0x1B, 0x1F, 0x1B, 255));
  C2D_DrawRectSolid(0, 0, 0.55f, 400, 18, C2D_Color32(20, 14, 10, 200));
  txt(6, 1, 0.5f, C2D_Color32(0xE8, 0xD8, 0xB8, 255), "VIRUS LAB");
  txt(110, 1, 0.5f, C2D_Color32(0xF2, 0xB9, 0x30, 255), "Credits %ld", (long)S.credits);
  txt(250, 1, 0.5f, C2D_Color32(0x9C, 0xD0, 0x6A, 255), "Food %ld", (long)S.food);
  for (int i = 0; i < S.nstr; i++) {
    float x = 12 + i * 34;
    C2D_DrawRectSolid(x - 2, 24, 0.3f, 32, 32, i == labSel ? C2D_Color32(0x55, 0x55, 0x55, 255) : C2D_Color32(0x2A, 0x2E, 0x2A, 255));
    drawIcon(S.str[i].vec, S.str[i].ci, x, 26, 0.45f, 0.4f);
  }
  drawIcon(v->vec, v->ci, 24, 74, 1.5f, 0.4f);
  txt(130, 62, 0.75f, rgbOf(STRAIN_RGB[v->ci % 6]), "%s", v->name);
  txt(130, 90, 0.5f, white, "%s", VECS[v->vec].name);
  txt(130, 106, 0.42f, dim, "Range %d   Spread x%.2f   Spotted x%.2f", VECS[v->vec].radius, VECS[v->vec].trans, VECS[v->vec].detect);
  for (int k = 0; k < 5; k++) {
    float y = 130 + k * 20;
    txt(12, y, 0.45f, traitOpen(k) ? white : dim, "%s", TRAITS[k].name);
    C2D_DrawRectSolid(110, y + 4, 0.3f, 200, 8, C2D_Color32(0x30, 0x34, 0x30, 255));
    if (v->tr[k] > 0) C2D_DrawRectSolid(110, y + 4, 0.35f, 200.0f * v->tr[k] / 10.0f, 8, rgbOf(STRAIN_RGB[v->ci % 6]));
    txt(318, y, 0.45f, traitOpen(k) ? white : dim, traitOpen(k) ? "%d" : "locked", v->tr[k]);
  }
  txt(12, 226 - 8, 0.42f, dim, "Points %d / %d     Samples %d / %d     Culture: %d food each", strainPts(v), labPoints(), sampleCount(), sampleCap(), cultureCost(v));
}

typedef struct { int x, y, w, h; char label[24]; int id; u32 col; } Btn;
static Btn btns[16]; static int nbtn;
static void addBtn(int slot, const char *label, int id, u32 col) {
  Btn *b = &btns[nbtn++];
  b->x = 8 + (slot % 2) * 156; b->y = 66 + (slot / 2) * 30; b->w = 148; b->h = 26; b->id = id; b->col = col;
  snprintf(b->label, sizeof b->label, "%s", label);
  C2D_DrawRectSolid(b->x, b->y, 0.3f, b->w, b->h, col);
  txt(b->x + 8, b->y + 5, 0.5f, C2D_Color32(255, 255, 255, 255), "%s", b->label);
}
static void addBtnXY(int x, int y, int w, int h, const char *label, int id, u32 col) {
  Btn *b = &btns[nbtn++]; b->x = x; b->y = y; b->w = w; b->h = h; b->id = id; b->col = col;
  snprintf(b->label, sizeof b->label, "%s", label);
  C2D_DrawRectSolid(x, y, 0.3f, w, h, col);
  txt(x + 6, y + (h > 28 ? 5 : 3), 0.5f, C2D_Color32(255, 255, 255, 255), "%s", label);
}
static void drawLabBottom(void) {
  nbtn = 0;
  Strain *v = curStrain();
  u32 white = C2D_Color32(255, 255, 255, 255), dim = C2D_Color32(0xB0, 0xA8, 0x98, 255);
  u32 blue = C2D_Color32(0x28, 0x58, 0x90, 255), green = C2D_Color32(0x30, 0x70, 0x50, 255), grey = C2D_Color32(0x55, 0x55, 0x55, 255), red = C2D_Color32(0x90, 0x38, 0x30, 255);
  for (int k = 0; k < 5; k++) {
    int y = 4 + k * 26;
    if (k == labRow) C2D_DrawRectSolid(0, y - 1, 0.2f, 320, 26, C2D_Color32(0x2C, 0x30, 0x38, 255));
    txt(8, y + 2, 0.5f, traitOpen(k) ? white : dim, "%s", TRAITS[k].name);
    txt(150, y + 2, 0.5f, white, "%d", v->tr[k]);
    addBtnXY(190, y, 56, 22, "  -", 100 + k * 2, grey);
    addBtnXY(254, y, 56, 22, "  +", 101 + k * 2, blue);
  }
  addBtnXY(8, 140, 148, 26, "Vector (Y)", 20, blue);
  addBtnXY(164, 140, 148, 26, "Culture 1 (A)", 21, green);
  addBtnXY(8, 170, 148, 26, "Culture max (R)", 22, green);
  addBtnXY(164, 170, 148, 26, "New strain (X)", 23, grey);
  addBtnXY(8, 200, 148, 26, "Delete strain", 24, red);
  addBtnXY(164, 200, 148, 26, "Back (B)", 25, grey);
  if (noteT > 0) { noteT--; txt(8, 228, 0.45f, C2D_Color32(0xF2, 0xB9, 0x30, 255), "%s", note); }
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
    addBtn(4, "Sell food", 9, gold);
    snprintf(lab, sizeof lab, "Seeds x10: %dcr", CROPS[S.cropSel].seed * 10); addBtn(5, lab, 10, grey);
    snprintf(lab, sizeof lab, "Crop: %s (L/R)", CROPS[S.cropSel].name); addBtn(6, lab, 11, grey);
    addBtn(7, "Virus lab (SEL)", 13, blue);
    addBtn(8, "Raid (ZR)", 30, red);
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

/* ---- raid screens ---- */
static const u32 *dummy_;
static void drawColScene(int t, int battle) {
  loadCol(t); const ColDef *c = &COLS[t];
  if (colBg) C2D_DrawImageAt(C2D_SpriteSheetGetImage(colBg, 0), 56, 0, 0.1f, NULL, 1.0f, 1.0f);
  if (!colSp) return;
  if (!battle) sortOrder(t);
  for (int r = 0; r < c->n; r++) {
    int i = Bt.order[r]; const EB *e = &c->b[i]; int fallen = battle && Bt.b[i].fallen;
    C2D_Image im = C2D_SpriteSheetGetImage(colSp, 2 * i + (fallen ? 1 : 0));
    float px = 56 + (fallen ? e->fx : e->sx), py = fallen ? e->fy : e->sy;
    C2D_DrawImageAt(im, px, py, 0.2f + r * 0.004f, NULL, 1.0f, 1.0f);
    if (battle && !fallen) {
      const BB *b = &Bt.b[i];
      if (b->inf > 0.02f) {
        u32 g = C2D_Color32(0x6C, 0xD0, 0x4A, (int)(70 + 120 * b->inf));
        C2D_DrawCircleSolid(56 + e->cx, e->cy - 6, 0.55f, 6 + 12 * b->inf, g);
      }
      if (b->hp < b->max) {
        float w = 24, f = b->hp / b->max; if (f < 0) f = 0;
        C2D_DrawRectSolid(56 + e->cx - 12, e->cy - 22, 0.56f, w, 3, C2D_Color32(0, 0, 0, 200));
        C2D_DrawRectSolid(56 + e->cx - 12, e->cy - 22, 0.57f, w * f, 3, C2D_Color32(0xE0, 0x50, 0x40, 255));
      }
    }
  }
  if (battle) for (int i = 0; i < 16; i++) if (Bt.fx[i].t > 0) {
    float k = 1 - Bt.fx[i].t / 0.5f;
    C2D_DrawCircleSolid(56 + (Bt.fx[i].x * 0.6f), Bt.fx[i].y * 0.6f, 0.58f, 4 + 22 * k, C2D_Color32(255, 255, 255, (int)(160 * (1 - k))));
  }
}
static void vbar(float x, float y, float w, float h, float f, u32 col) {
  if (f < 0) f = 0; if (f > 1) f = 1;
  C2D_DrawRectSolid(x, y, 0.5f, w, h, C2D_Color32(0, 0, 0, 160));
  C2D_DrawRectSolid(x, y + h * (1 - f), 0.55f, w, h * f, col);
}
static void drawRaidTop(void) {
  u32 white = C2D_Color32(255, 255, 255, 255), gold = C2D_Color32(0xF2, 0xB9, 0x30, 255), dim = C2D_Color32(0xB0, 0xA8, 0x98, 255);
  if (raidMode == 1) {
    drawColScene(raidSel, 0); const ColDef *c = &COLS[raidSel];
    C2D_DrawRectSolid(0, 0, 0.59f, 400, 18, C2D_Color32(0, 0, 0, 170));
    txt(6, 1, 0.5f, white, "%s  -  %s, %s  (tier %d%s)", c->name, c->kind, c->biome, c->tier, c->walled ? ", walled" : "");
    return;
  }
  drawColScene(Bt.col, 1);
  vbar(8, 20, 12, 200, Bt.alert, C2D_Color32(0xE0, 0x50, 0x40, 255));
  vbar(28, 20, 12, 200, Bt.cure, C2D_Color32(0x40, 0x90, 0xE0, 255));
  txt(6, 222, 0.38f, dim, "Alert"); txt(26, 222, 0.38f, dim, "Cure");
  float left = Bt.dur - Bt.t; if (left < 0) left = 0;
  txt(348, 8, 0.5f, white, "Time %d:%02d", (int)left / 60, (int)left % 60);
  txt(348, 30, 0.5f, gold, "Down %d%%", Bt.pct);
  char st[8]; for (int i = 0; i < 3; i++) st[i] = i < Bt.stars ? '*' : '-'; st[3] = 0;
  txt(348, 52, 0.7f, gold, "%s", st);
  txt(348, 90, 0.45f, white, "Energy");
  C2D_DrawRectSolid(348, 106, 0.5f, 46, 8, C2D_Color32(0, 0, 0, 160));
  C2D_DrawRectSolid(348, 106, 0.55f, 46 * (Bt.energy / Bt.cap), 8, C2D_Color32(0x6C, 0xD0, 0x4A, 255));
  if (Bt.mutT > 0) txt(348, 124, 0.42f, gold, "Mutating");
  if (Bt.breach) txt(348, 140, 0.42f, gold, "Wall down");
  if (noteT > 0) { noteT--; txt(60, 222, 0.5f, gold, "%s", note); }
  if (raidMode == 3) {
    C2D_DrawRectSolid(70, 50, 0.7f, 260, 130, C2D_Color32(0, 0, 0, 220));
    u32 c = Bt.win ? C2D_Color32(0x6C, 0xD0, 0x4A, 255) : C2D_Color32(0xE0, 0x50, 0x40, 255);
    C2D_DrawRectSolid(70, 50, 0.71f, 260, 4, c);
    #define T2(y, ...) { C2D_Text tt; char bb[96]; snprintf(bb, sizeof bb, __VA_ARGS__); C2D_TextParse(&tt, tbuf, bb); C2D_TextOptimize(&tt); C2D_DrawText(&tt, C2D_WithColor, 86, y, 0.8f, 0.55f, 0.55f, white); }
    { C2D_Text tt; C2D_TextParse(&tt, tbuf, Bt.win ? "RAID WON" : "RAID LOST"); C2D_TextOptimize(&tt); C2D_DrawText(&tt, C2D_WithColor, 86, 60, 0.8f, 0.9f, 0.9f, c); }
    T2(92, "%s", Bt.reason);
    T2(110, "Stars %d   Collapsed %d%%", Bt.stars, Bt.pct);
    if (Bt.win) T2(128, "Loot +%ld credits", Bt.loot) else T2(128, "Your colony is infected");
    T2(146, "Trophies %+d  (now %d)", Bt.dT, S.trophies);
  }
}
static void drawRaidBottom(void) {
  nbtn = 0;
  u32 white = C2D_Color32(255, 255, 255, 255), dim = C2D_Color32(0xB0, 0xA8, 0x98, 255), gold = C2D_Color32(0xF2, 0xB9, 0x30, 255);
  u32 blue = C2D_Color32(0x28, 0x58, 0x90, 255), green = C2D_Color32(0x30, 0x70, 0x50, 255), grey = C2D_Color32(0x55, 0x55, 0x55, 255), red = C2D_Color32(0x90, 0x38, 0x30, 255);
  if (raidMode == 1) {
    txt(8, 4, 0.5f, gold, "Choose a target   Trophies %d   Samples %d", S.trophies, sampleCount());
    for (int i = 0; i < NCOLS; i++) {
      char l[24]; snprintf(l, sizeof l, "%c%s", i == raidSel ? '>' : ' ', COLS[i].name);
      Btn *b = &btns[nbtn++]; b->x = 8 + (i % 2) * 156; b->y = 22 + (i / 2) * 30; b->w = 148; b->h = 26; b->id = 50 + i; b->col = i == raidSel ? green : grey;
      C2D_DrawRectSolid(b->x, b->y, 0.3f, b->w, b->h, b->col); txt(b->x + 6, b->y + 4, 0.5f, white, "%s", l);
    }
    addBtnXY(8, 180, 148, 26, "Launch (A)", 41, red); addBtnXY(164, 180, 148, 26, "Back (B)", 42, grey);
    if (noteT > 0) { noteT--; txt(8, 214, 0.5f, gold, "%s", note); }
    return;
  }
  C2D_DrawRectSolid(8, 8, 0.2f, 240, 200, C2D_Color32(0x10, 0x10, 0x0E, 255));
  const ColDef *c = &COLS[Bt.col];
  static const u32 kc[8] = {0xFFE0B040, 0xFFB0B0B0, 0xFF50A050, 0xFFD0A040, 0xFF4040E0, 0xFF40D0D0, 0xFFE040E0, 0xFF707070};
  for (int i = 0; i < Bt.n; i++) {
    const EB *e = &c->b[i]; float x = 8 + e->x * 0.5f, y = 8 + e->y * 0.5f, r = e->r * 0.5f;
    u32 col = Bt.b[i].fallen ? C2D_Color32(0x40, 0x40, 0x40, 255) : (u32)kc[e->k % 8];
    col = (col & 0xFF000000) | ((col & 0xFF) << 16 & 0xFF0000) | (col & 0xFF00) | ((col >> 16) & 0xFF);
    C2D_DrawCircleSolid(x, y, 0.25f, r, col);
    if (!Bt.b[i].fallen && Bt.b[i].inf > 0.02f) C2D_DrawCircleSolid(x, y, 0.26f, r * (0.4f + Bt.b[i].inf), C2D_Color32(0x6C, 0xD0, 0x4A, 230));
  }
  float cx = 8 + Bt.cx * 0.5f, cy = 8 + Bt.cy * 0.5f;
  C2D_DrawLine(cx - 8, cy, white, cx + 8, cy, white, 1, 0.28f); C2D_DrawLine(cx, cy - 8, white, cx, cy + 8, white, 1, 0.28f);
  if (raidMode == 2) {
    addBtnXY(252, 8, 64, 36, "Surge", 60, blue); addBtnXY(252, 50, 64, 36, "Mutate", 61, green);
    addBtnXY(252, 92, 64, 36, "Strain", 62, grey); addBtnXY(252, 134, 64, 36, "Retreat", 63, red);
    if (Bt.sel >= 0) txt(8, 210, 0.45f, white, "Strain: %s  x%d", S.str[Bt.sel].name, Bt.avail[Bt.sel]);
    else txt(8, 210, 0.45f, dim, "No samples left");
    txt(8, 224, 0.4f, dim, "Touch the map or hold A to infect. X surge, Y mutate");
    if (Bt.surgeCd > 0) txt(256, 176, 0.4f, dim, "Surge %.0fs", Bt.surgeCd);
    if (Bt.mutCd > 0) txt(256, 190, 0.4f, dim, "Mut %.0fs", Bt.mutCd);
  } else {
    addBtnXY(252, 8, 64, 36, "Done (A)", 70, green);
  }
}
static void cycleStrain(void) {
  if (S.nstr < 1) return;
  int s0 = Bt.sel < 0 ? 0 : Bt.sel;
  for (int k = 1; k <= S.nstr; k++) { int i = (s0 + k) % S.nstr; if (Bt.avail[i] > 0) { Bt.sel = i; return; } }
}
static void launchRaid(void) {
  if (S.nstr < 1 || sampleCount() < 1) { say("Culture some samples in the Virus lab first"); return; }
  startBattle(raidSel);
}
static void act(int id) {
  switch (id) {
    case 30: raidMode = 1; break;
    case 41: launchRaid(); break;
    case 42: raidMode = 0; break;
    case 60: doSurge(); break;
    case 61: doMutate(); break;
    case 62: cycleStrain(); break;
    case 63: endBattle("You retreated."); break;
    case 70: raidMode = 0; break;
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
    case 13: labMode = 1; break;
    case 20: labVec(); break;
    case 21: labCulture(0); break;
    case 22: labCulture(1); break;
    case 23: labNew(); break;
    case 24: labDel(); break;
    case 25: labMode = 0; break;
    default: if (id >= 50 && id < 60) { raidSel = id - 50; } else if (id >= 100 && id < 110) { labRow = (id - 100) / 2; labAdj((id & 1) ? 1 : -1); } break;
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
  viSheet = C2D_SpriteSheetLoad("romfs:/gfx/vicons.t3x");
  if (!viSheet) { gfxExit(); consoleInit(GFX_TOP, NULL); printf("Missing vicons\n"); while (aptMainLoop()) { hidScanInput(); if (hidKeysDown() & KEY_START) break; gspWaitForVBlank(); } return 1; }
  srand((unsigned)time(NULL));
  loadGame(); lastMs = osGetTime();
  u64 lastSave = lastMs, prevMs = lastMs;
  while (aptMainLoop()) {
    hidScanInput();
    u32 kd = hidKeysDown();
    if (kd & KEY_START) break;
    u64 nowMs = osGetTime(); float dt = (nowMs - prevMs) / 1000.0f; if (dt > 0.05f) dt = 0.05f; prevMs = nowMs;
    if (!labMode && !raidMode) {
      if (kd & KEY_DUP) { if (cury > 0) cury--; }
      if (kd & KEY_DDOWN) { if (cury < GRID - 1) cury++; }
      if (kd & KEY_DLEFT) { if (curx > 0) curx--; }
      if (kd & KEY_DRIGHT) { if (curx < GRID - 1) curx++; }
    }
    if (raidMode == 1) {
      if (kd & KEY_UP) raidSel = (raidSel + NCOLS - 2) % NCOLS;
      if (kd & KEY_DOWN) raidSel = (raidSel + 2) % NCOLS;
      if (kd & (KEY_LEFT | KEY_RIGHT)) raidSel ^= 1;
      if (kd & KEY_A) launchRaid();
      if (kd & KEY_B) raidMode = 0;
    } else if (raidMode == 2) {
      circlePosition cp; hidCircleRead(&cp);
      if (abs(cp.dx) > 12) Bt.cx += cp.dx * 1.6f * dt * 3; if (abs(cp.dy) > 12) Bt.cy -= cp.dy * 1.6f * dt * 3;
      u32 kh = hidKeysDown() | hidKeysHeld();
      if (kh & KEY_DLEFT) Bt.cx -= 160 * dt; if (kh & KEY_DRIGHT) Bt.cx += 160 * dt;
      if (kh & KEY_DUP) Bt.cy -= 160 * dt; if (kh & KEY_DDOWN) Bt.cy += 160 * dt;
      int th = 0;
      if (hidKeysHeld() & KEY_TOUCH) { touchPosition tq; hidTouchRead(&tq); if (tq.px >= 8 && tq.px < 248 && tq.py >= 8 && tq.py < 208) { Bt.cx = (tq.px - 8) * 2.0f; Bt.cy = (tq.py - 8) * 2.0f; th = 1; } }
      if (Bt.cx < 0) Bt.cx = 0; if (Bt.cx > 480) Bt.cx = 480; if (Bt.cy < 0) Bt.cy = 0; if (Bt.cy > 400) Bt.cy = 400;
      Bt.steering = th || (hidKeysHeld() & KEY_A) ? 1 : 0; Bt.steerX = Bt.cx; Bt.steerY = Bt.cy;
      if (kd & KEY_X) doSurge();
      if (kd & KEY_Y) doMutate();
      if (kd & (KEY_L | KEY_R)) cycleStrain();
      if (kd & KEY_B) endBattle("You retreated.");
      stepBattle(dt);
    } else if (raidMode == 3) {
      if (kd & (KEY_A | KEY_B)) raidMode = 0;
    } else if (labMode) {
      if (kd & KEY_UP) { labRow = (labRow + 4) % 5; }
      if (kd & KEY_DOWN) { labRow = (labRow + 1) % 5; }
      if (kd & KEY_LEFT) labAdj(-1);
      if (kd & KEY_RIGHT) labAdj(1);
      if (kd & KEY_L) { labSel = (labSel + S.nstr - 1) % S.nstr; }
      if (kd & KEY_R) labCulture(1);
      if (kd & KEY_A) labCulture(0);
      if (kd & KEY_X) labNew();
      if (kd & KEY_Y) labVec();
      if (kd & KEY_SELECT) { Strain *v = curStrain(); randName(v->name, sizeof v->name); }
      if (kd & KEY_B) labMode = 0;
    } else if (buildMode) {
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
      if (kd & KEY_SELECT) labMode = 1;
      if (kd & KEY_ZR) raidMode = 1;
      if (kd & KEY_ZL) labMode = 1;
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
    C2D_SceneBegin(top); if (raidMode) drawRaidTop(); else if (labMode) drawLabTop(); else { drawTop(); if (S.infected) txt(150, 4, 0.5f, C2D_Color32(0x6C, 0xD0, 0x4A, 255), "INFECTED"); }
    C2D_TargetClear(bot, C2D_Color32(0x1E, 0x1A, 0x16, 255));
    C2D_SceneBegin(bot); if (raidMode) drawRaidBottom(); else if (labMode) drawLabBottom(); else drawBottom();
    C3D_FrameEnd(0);
  }
  saveGame();
  for (int i = 0; i < NTYPES; i++) if (sheets[i]) C2D_SpriteSheetFree(sheets[i]);
  C2D_SpriteSheetFree(cropSheet); C2D_SpriteSheetFree(viSheet);
  C2D_TextBufDelete(tbuf); C2D_Fini(); C3D_Fini(); romfsExit(); gfxExit();
  return 0;
}
