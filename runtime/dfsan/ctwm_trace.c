#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static FILE *CTWMTraceFile;

static void ctwm_trace_open_file(void) {
  if (CTWMTraceFile)
    return;
  const char *Path = getenv("SYMSAN_CTWM_TRACE_PATH");
  if (!Path || !*Path)
    Path = "ctwm_trace.log";
  CTWMTraceFile = fopen(Path, "ab");
}

__attribute__((constructor)) static void ctwm_trace_constructor(void) {
#if SYMSAN_CTWM_ENABLE_BB_TRACE
  ctwm_trace_open_file();
#endif
}

__attribute__((destructor)) static void ctwm_trace_destructor(void) {
  if (CTWMTraceFile) {
    fclose(CTWMTraceFile);
    CTWMTraceFile = NULL;
  }
}

void __ctwm_trace_bb(int32_t BBId) {
  if (!CTWMTraceFile)
    ctwm_trace_open_file();
  if (!CTWMTraceFile)
    return;
  fwrite(&BBId, sizeof(BBId), 1, CTWMTraceFile);
}
