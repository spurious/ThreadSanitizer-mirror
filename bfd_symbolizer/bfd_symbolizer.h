/** BFD-based Symbolizer
 *  Copyright (c) 2011, Google Inc. All rights reserved.
 *  Author: Dmitry Vyukov (dvyukov)
 *
 *  It is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 3, or (at your option) any later
 *  version. See http://www.gnu.org/licenses/
 */

#ifndef BFD_SYMBOLIZER_H_INCLUDED
#define BFD_SYMBOLIZER_H_INCLUDED
#ifdef __cplusplus
extern "C" {
#endif


typedef enum bfds_opts_e {
  // Nothing special (for readability).
  bfds_opt_none                 = 0,
  // Do update map of dynamic libraries before address resolution.
  // If not specified map of dynamic libraries is updated
  // whenever an address is not resolved.
  // You may not specify it at all, however it can produce incorrect
  // results if a dynamic library is unloaded and then another library
  // is loaded at the same address.
  bfds_opt_update_libs          = 1 << 1,
  // Resolve data symbol (variable) instead of code symbol (default).
  bfds_opt_data                 = 1 << 2,
  // Do demangle symbol names into a short form (that is, only function names).
  // It includes function template arguments, though.
  bfds_opt_demangle             = 1 << 3,
  // Short form + function parameters.
  bfds_opt_demangle_params      = 1 << 4,
  // Do demangle symbol names into a verbose form
  // (that includes more templates, return types, etc).
  bfds_opt_demangle_verbose     = 1 << 5,
} bfds_opts_e;


/** Maps an address in the process' address space to symbolic information.
 *  All output parameters are optional, if a value is not requires pass in
 *  zero buffer size for strings or NULL pointer for integers.
 *
 *  @param addr          [in]  An address to resolve.
 *  @param opts          [in]  Various options (see bfds_opts description).
 *  @param symbol        [out] Symbol name.
 *  @param symbol_size   [in]  Size of the symbol buffer.
 *  @param module        [out] Module name which the address comes from.
 *  @param module_size   [in]  Size of the module buffer.
 *  @param filename      [out] Source filename.
 *  @param filename_size [in]  Size of the filename buffer.
 *  @param source_line   [out] Source line.
 *  @param symbol_offset [out] Address offset from a beginning of the symbol.
 *                             As of now offset is not calculated for functions
 *  @return                    0 - success, any other value - error.
 */
int   bfds_symbolize    (void*                  addr,
                         bfds_opts_e            opts,
                         char*                  symbol,
                         int                    symbol_size,
                         char*                  module,
                         int                    module_size,
                         char*                  filename,
                         int                    filename_size,
                         int*                   source_line,
                         int*                   symbol_offset);


/** Helpers for dynamic linking.
 */
#define BFDS_SYMBOLIZE_FUNC "bfds_symbolize"
typedef int (*bfds_symbolize_t)
                        (void*                  addr,
                         bfds_opts_e            opts,
                         char*                  symbol,
                         int                    symbol_size,
                         char*                  module,
                         int                    module_size,
                         char*                  filename,
                         int                    filename_size,
                         int*                   source_line,
                         int*                   symbol_offset);


/** Unwinds current call stack.
 *
 *  @param stack         [in/out] An array where to put the call stack.
 *  @param count         [in]     Number of entries in the stack array.
 *  @param count         [in]     Number of entries to skip from the stack top.
 *  @return                       Number of filled entries in the stack array,
 *                                Negative value means an error.
 */
int   bfds_unwind       (void**                 stack,
                         int                    count,
                         int                    skip);

#ifdef __cplusplus
}
#endif
#endif

