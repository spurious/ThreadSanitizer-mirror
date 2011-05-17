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


typedef enum bfds_opts {
  // Nothing special (for readability).
  bfds_opt_none                 = 0,
  // Do demangle symbol names.
  bfds_opt_demangle             = 1 << 0,
  // Include function parameters and return types when demangling.
  bfds_opt_func_params          = 1 << 1,
  // Include template arguments when demangling.
  bfds_opt_templates            = 1 << 2,
  // Do update map of dynamic libraries before address resolution.
  // If not specified map of dynamic libraries is updated
  // whenever an address is not resolved.
  // You may not specify it at all, however it can produce incorrect
  // results if a dynamic library is unloaded and then another library
  // is loaded at the same address.
  bfds_opt_update_libs          = 1 << 3
} bfds_opts;


/** Maps an address in the process' address space to symbolic information.
 *  All output parameters are optional, if a value is not requires pass in
 *  NULL pointer and/or zero size.
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
 *  @param is_function   [out] Determines as to symbol is a function or data.
 *  @return                    0 - success, any other value - error.
 */
int   bfds_symbolize    (void*                  addr,
                         bfds_opts              opts,
                         char*                  symbol,
                         int                    symbol_size,
                         char*                  module,
                         int                    module_size,
                         char*                  filename,
                         int                    filename_size,
                         int*                   source_line,
                         int*                   symbol_offset,
                         int*                   is_function);


#ifdef __cplusplus
}
#endif
#endif

