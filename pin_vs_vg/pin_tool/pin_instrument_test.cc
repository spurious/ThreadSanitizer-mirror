#include "pin.H"
#include "sys/time.h"
#include "time.h"

#define tool_printf(A, B...) \
  do { \
    char buffer[1024] = ""; \
    sprintf(buffer, A, B); \
    LOG(buffer); \
  } while(0)

#include "../common/drd_benchmark_simple.h"

long GetTimeInMilliseconds() {
  struct timeval now;
  gettimeofday(&now, NULL);
  return now.tv_sec * 1000 + now.tv_usec / 1000;
}

//---------- Instrumentation functions ---------

void InsertBeforeEvent_MemoryAccessIf_READ(ADDRINT pc) {
  Benchmark_OnMemAccess(false);
}

void InsertBeforeEvent_MemoryAccessIf_WRITE(ADDRINT pc) {
  Benchmark_OnMemAccess(true);
}

//-------------- PIN callbacks ---------------

void CallbackForTRACE(TRACE trace, void *v) {
  for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
    for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) { 
      /*if (INS_IsStackRead(ins) || INS_IsStackWrite(ins)) 
        continue;*/
      if (INS_IsMemoryRead(ins)) {
        Benchmark_OnMemAccessInstrumentation(false);
        // TODO: use INS_InsertPredicatedCall instead?
        INS_InsertCall(ins, IPOINT_BEFORE,
                       (AFUNPTR)InsertBeforeEvent_MemoryAccessIf_READ,
                       IARG_INST_PTR, IARG_END);
      }
      if (INS_IsMemoryWrite(ins)) {
        Benchmark_OnMemAccessInstrumentation(true);
        INS_InsertCall(ins, IPOINT_BEFORE,
                       (AFUNPTR)InsertBeforeEvent_MemoryAccessIf_WRITE,
                       IARG_INST_PTR, IARG_END);
      }
    }
  }
}

static void CallbackForFini(INT32 code, void *v) {
  Benchmark_OnExit(code);
}

KNOB<int> KnobN(KNOB_MODE_WRITEONCE, "pintool", "N", "5", "Specify 'N'");
//---------------- main ---------------
int main(INT32 argc, CHAR **argv)
{
  PIN_InitSymbols();
  if (PIN_Init(argc, argv)) {
    printf("Error parsing command line\n");
    return 1;
  }
  PIN_AddFiniFunction(CallbackForFini, 0);
  TRACE_AddInstrumentFunction(CallbackForTRACE, 0);
  Benchmark_Initialize();
  Benchmark_SetNumDivisionsPerMemAccess(KnobN.Value());
  PIN_StartProgram();
  return 0;
}
