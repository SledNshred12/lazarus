#include <3ds.h>
#include <stdio.h>
static void lg(const char *m) { char p[48]; snprintf(p, sizeof p, "sdmc:/3ds/lztest%d.log", LEVEL); FILE *f = fopen(p, "a"); if (f) { fputs(m, f); fputc(10, f); fclose(f); } }
int main(void) {
  char b[96]; int sdok; { char p[48]; snprintf(p, sizeof p, "sdmc:/3ds/lztest%d.log", LEVEL); FILE *f = fopen(p, "w"); sdok = f != NULL; if (f) { fputs("main entered\n", f); fclose(f); } }
  gfxInitDefault(); consoleInit(GFX_TOP, NULL); lg("gfx ok");
  printf("LZ TEST %d\nsd write: %s\n", LEVEL, sdok ? "ok" : "FAIL");
#if LEVEL >= 2
  { Result r = romfsInit(); FILE *g = fopen("romfs:/hello.txt", "r"); snprintf(b, sizeof b, "romfs %08lx hello.txt %s", (unsigned long)r, g ? "ok" : "missing"); printf("%s\n", b); lg(b); if (g) fclose(g); }
#endif
#if LEVEL >= 3
  { Result n = ndspInit(); snprintf(b, sizeof b, "ndsp %08lx", (unsigned long)n); printf("%s\n", b); lg(b); if (R_SUCCEEDED(n)) ndspExit(); }
#endif
  printf("\nPress START to exit\n");
  while (aptMainLoop()) { hidScanInput(); if (hidKeysDown() & KEY_START) break; gfxFlushBuffers(); gfxSwapBuffers(); gspWaitForVBlank(); }
  gfxExit(); return 0;
}
