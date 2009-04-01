// TODO: license!

#include "pub_tool_basics.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_machine.h"
#include "pub_tool_options.h"
#include "pub_tool_debuginfo.h"
#include "pub_tool_stacktrace.h"

#define tool_printf VG_(printf)
typedef Int bool;
#include "drd_benchmark_simple.h"

static void tg_post_clo_init(void)
{
}

static void tg_fini(Int exitcode)
{
  Benchmark_OnExit(exitcode);
}

static
void evh__pre_mem_read ( CorePart part, ThreadId tid, Char* s,
                         Addr a, SizeT size) {
  Benchmark_OnMemAccess(0);
}

static
void evh__pre_mem_read_asciiz ( CorePart part, ThreadId tid,
                                Char* s, Addr a ) {
  Benchmark_OnMemAccess(0);
}

static
void evh__pre_mem_write ( CorePart part, ThreadId tid, Char* s,
                          Addr a, SizeT size ) {
  Benchmark_OnMemAccess(1);
}

static VG_REGPARM(2)
void evh__mem_help_read_1(Addr a, Addr sp) {
  Benchmark_OnMemAccess(0);
}

static VG_REGPARM(2)
void evh__mem_help_read_2(Addr a, Addr sp) {
  Benchmark_OnMemAccess(0);
}

static VG_REGPARM(2)
void evh__mem_help_read_4(Addr a, Addr sp) {
  Benchmark_OnMemAccess(0);
}

static VG_REGPARM(2)
void evh__mem_help_read_8(Addr a, Addr sp) {
  Benchmark_OnMemAccess(0);
}

static VG_REGPARM(3)
void evh__mem_help_read_N(Addr a, SizeT size, Addr sp) {
  Benchmark_OnMemAccess(0);
}

static VG_REGPARM(2)
void evh__mem_help_write_1(Addr a, Addr sp) {
  Benchmark_OnMemAccess(1);
}

static VG_REGPARM(2)
void evh__mem_help_write_2(Addr a, Addr sp) {
  Benchmark_OnMemAccess(1);
}

static VG_REGPARM(2)
void evh__mem_help_write_4(Addr a, Addr sp) {
  Benchmark_OnMemAccess(1);
}

static VG_REGPARM(2)
void evh__mem_help_write_8(Addr a, Addr sp) {
  Benchmark_OnMemAccess(1);
}

static VG_REGPARM(3)
void evh__mem_help_write_N(Addr a, SizeT size, Addr sp) {
  Benchmark_OnMemAccess(1);
}

static void instrument_mem_access ( IRSB*   bbOut,
                                    IRExpr* addr,
                                    Int     szB,
                                    Bool    isStore,
                                    Int     hWordTy_szB,
                                    VexGuestLayout* layout )
{
   IRType   tyAddr   = Ity_INVALID;
   const char* hName = "";
   void*    hAddr    = NULL;
   Int      regparms = 0;
   IRExpr** argv     = NULL;
   IRDirty* di       = NULL;
   IRTemp   sp;
   IRExpr*  spE;

   tl_assert(isIRAtom(addr));
   tl_assert(hWordTy_szB == 4 || hWordTy_szB == 8);

   tyAddr = typeOfIRExpr( bbOut->tyenv, addr );
   tl_assert(tyAddr == Ity_I32 || tyAddr == Ity_I64);

   /* Get the guest's stack pointer, so we can pass it to the helper.
      How do we know this is up to date?  Presumably because SP is
      flushed to guest state before every memory reference. */
   tl_assert(sizeof(void*) == layout->sizeof_SP);
   tl_assert(sizeof(void*) == hWordTy_szB);
   if (layout->sizeof_SP == 4) {
      sp = newIRTemp(bbOut->tyenv, Ity_I32);
      addStmtToIRSB(
         bbOut,
         IRStmt_WrTmp( sp, IRExpr_Get( layout->offset_SP, Ity_I32 ) )
      );
   } else {
      tl_assert(layout->sizeof_SP == 8);
      sp = newIRTemp(bbOut->tyenv, Ity_I64);
      addStmtToIRSB(
         bbOut,
         IRStmt_WrTmp( sp, IRExpr_Get( layout->offset_SP, Ity_I64 ) )
      );
   }
   spE = IRExpr_RdTmp( sp );

   /* So the effective address is in 'addr' now. */
   regparms = 2; // unless stated otherwise
   if (isStore) {
      switch (szB) {
         case 1:
            hName = "evh__mem_help_write_1";
            hAddr = (void*)&evh__mem_help_write_1;
            argv = mkIRExprVec_2( addr, spE );
            break;
         case 2:
            hName = "evh__mem_help_write_2";
            hAddr = (void*)&evh__mem_help_write_2;
            argv = mkIRExprVec_2( addr, spE );
            break;
         case 4:
            hName = "evh__mem_help_write_4";
            hAddr = (void*)&evh__mem_help_write_4;
            argv = mkIRExprVec_2( addr, spE );
            break;
         case 8:
            hName = "evh__mem_help_write_8";
            hAddr = (void*)&evh__mem_help_write_8;
            argv = mkIRExprVec_2( addr, spE );
            break;
         default:
            tl_assert(szB > 8 && szB <= 512); /* stay sane */
            regparms = 3;
            hName = "evh__mem_help_write_N";
            hAddr = (void*)&evh__mem_help_write_N;
            argv = mkIRExprVec_3( addr, mkIRExpr_HWord( szB ), spE);
            break;
      }
   } else {
      switch (szB) {
         case 1:
            hName = "evh__mem_help_read_1";
            hAddr = (void*)&evh__mem_help_read_1;
            argv = mkIRExprVec_2( addr, spE );
            break;
         case 2:
            hName = "evh__mem_help_read_2";
            hAddr = (void*)&evh__mem_help_read_2;
            argv = mkIRExprVec_2( addr, spE );
            break;
         case 4:
            hName = "evh__mem_help_read_4";
            hAddr = (void*)&evh__mem_help_read_4;
            argv = mkIRExprVec_2( addr, spE );
            break;
         case 8:
            hName = "evh__mem_help_read_8";
            hAddr = (void*)&evh__mem_help_read_8;
            argv = mkIRExprVec_2( addr, spE );
            break;
         default:
            tl_assert(szB > 8 && szB <= 512); /* stay sane */
            regparms = 3;
            hName = "evh__mem_help_read_N";
            hAddr = (void*)&evh__mem_help_read_N;
            argv = mkIRExprVec_3( addr, mkIRExpr_HWord( szB ), spE);
            break;
      }
   }

   /* Add the helper. */
   tl_assert(hName);
   tl_assert(hAddr);
   tl_assert(argv);
   di = unsafeIRDirty_0_N( regparms,
                           (HChar*)hName, VG_(fnptr_to_fnentry)( hAddr ),
                           argv );
   addStmtToIRSB( bbOut, IRStmt_Dirty(di) );
   Benchmark_OnMemAccessInstrumentation(isStore);
}

static
IRSB* tg_instrument ( VgCallbackClosure* closure,
                      IRSB* bbIn,
                      VexGuestLayout* layout,
                      VexGuestExtents* vge,
                      IRType gWordTy, IRType hWordTy )
{
   Int   i;
   IRSB* bbOut;

   if (gWordTy != hWordTy) {
      /* We don't currently support this case. */
      VG_(tool_panic)((Char*)"host/guest word size mismatch");
   }

   /* Set up BB */
   bbOut           = emptyIRSB();
   bbOut->tyenv    = deepCopyIRTypeEnv(bbIn->tyenv);
   bbOut->next     = deepCopyIRExpr(bbIn->next);
   bbOut->jumpkind = bbIn->jumpkind;

   // Copy verbatim any IR preamble preceding the first IMark
   i = 0;
   while (i < bbIn->stmts_used && bbIn->stmts[i]->tag != Ist_IMark) {
      addStmtToIRSB( bbOut, bbIn->stmts[i] );
      i++;
   }

   for (/*use current i*/; i < bbIn->stmts_used; i++) {
      IRStmt* st = bbIn->stmts[i];
      tl_assert(st);
      tl_assert(isFlatIRStmt(st));
      switch (st->tag) {
         case Ist_NoOp:
         case Ist_AbiHint:
         case Ist_Put:
         case Ist_PutI:
         case Ist_IMark:
         case Ist_Exit:
         case Ist_MBE:
            /* None of these can contain any memory references. */
            break;

         case Ist_Store:
            instrument_mem_access(
               bbOut,
               st->Ist.Store.addr,
               sizeofIRType(typeOfIRExpr(bbIn->tyenv, st->Ist.Store.data)),
               True/*isStore*/,
               sizeofIRType(hWordTy),
               layout
            );
            break;

         case Ist_WrTmp: {
            IRExpr* data = st->Ist.WrTmp.data;
            if (data->tag == Iex_Load) {
               instrument_mem_access(
                  bbOut,
                  data->Iex.Load.addr,
                  sizeofIRType(data->Iex.Load.ty),
                  False/*!isStore*/,
                  sizeofIRType(hWordTy),
                  layout
               );
            }
            break;
         }

         case Ist_Dirty: {
            Int      dataSize;
            IRDirty* d = st->Ist.Dirty.details;
            if (d->mFx != Ifx_None) {
               /* This dirty helper accesses memory.  Collect the
                  details. */
               tl_assert(d->mAddr != NULL);
               tl_assert(d->mSize != 0);
               dataSize = d->mSize;
               if (d->mFx == Ifx_Read || d->mFx == Ifx_Modify) {
                  instrument_mem_access(
                     bbOut, d->mAddr, dataSize, False/*!isStore*/,
                     sizeofIRType(hWordTy), layout
                  );
               }
               if (d->mFx == Ifx_Write || d->mFx == Ifx_Modify) {
                  instrument_mem_access(
                     bbOut, d->mAddr, dataSize, True/*isStore*/,
                     sizeofIRType(hWordTy), layout
                  );
               }
            } else {
               tl_assert(d->mAddr == NULL);
               tl_assert(d->mSize == 0);
            }
            break;
         }

         default:
            tl_assert(0);

      } /* switch (st->tag) */

      addStmtToIRSB( bbOut, st );
   } /* iterate over bbIn->stmts */

   return bbOut;
}

static Bool tg_process_cmd_line_option(Char* arg)
{
  if (VG_CLO_STREQN(4, arg, (const Char*)"--N=")) {
    UInt N = VG_(atoll)(&arg[4]);
    tl_assert(N >= 0);
    Benchmark_SetNumDivisionsPerMemAccess(N);
    return True;
  }
  return False;
}

static void tg_pre_clo_init(void)
{
   VG_(details_name)            ((Char*)"testgrind");
   VG_(details_version)         ((Char*)0);
   VG_(details_description)     ((Char*)"a binary JIT-compiler");
   VG_(details_copyright_author)((Char*)
      "Copyright (C) 2002-2008, and GNU GPL'd, by Nicholas Nethercote.");
   VG_(details_bug_reports_to)  ((Char*)VG_BUGS_TO);

   VG_(basic_tool_funcs)        (tg_post_clo_init,
                                 tg_instrument,
                                 tg_fini);

   VG_(needs_command_line_options)(tg_process_cmd_line_option, NULL, NULL);


   VG_(track_pre_mem_read)        ( evh__pre_mem_read );
   VG_(track_pre_mem_read_asciiz) ( evh__pre_mem_read_asciiz );
   VG_(track_pre_mem_write)       ( evh__pre_mem_write );

   Benchmark_Initialize();
   /* No needs, no core events to track */
}

VG_DETERMINE_INTERFACE_VERSION(tg_pre_clo_init)

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
