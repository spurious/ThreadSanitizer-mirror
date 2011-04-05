/* Relite: GCC instrumentation plugin for ThreadSanitizer
 * Copyright (c) 2011, Google Inc. All rights reserved.
 * Author: Dmitry Vyukov (dvyukov)
 *
 * Relite is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3, or (at your option) any later
 * version. See http://www.gnu.org/licenses/
 */

//TODO(dvyukov): create a makefile

//TODO(dvyukov): collect per-function stats

//TODO(dvyukov): support loop-wide sblocks

//TODO(dvyukov): eliminate excessive aliasing mops

//TODO(dvyukov): create specialized tsan_rtl_mop: r/w, sblock, size

//TODO(dvyukov): move all shadow stack support code into callee function

#include <plugin.h>
#include <plugin-version.h>
#include <config.h>
#include <system.h>
#include <coretypes.h>
#include <tm.h>
#include <tree-pass.h>
#include <function.h>
#include <gimple.h>
#include <diagnostic.h>
#include <stdio.h>
#include "relite_pass.h"

// required by gcc plugin machinery
bool plugin_is_GPL_compatible = false;


// global contest - there is no way to pass a context to compilation passes
relite_context_t g_ctx;


static void             plugin_prepass      (void* event_data,
                                             void* user_data) {
  (void)event_data;
  (void)user_data;
  relite_prepass(&g_ctx);
}


static void             plugin_finish_unit  (void* event_data,
                                             void* user_data) {
  (void)event_data;
  (void)user_data;
  relite_finish(&g_ctx);
}


static tree             handle_ignore_attr  (tree* node,
                                             tree name,
                                             tree args,
                                             int flags,
                                             bool* no_add_attrs) {
  return NULL_TREE;
}


static void             register_attributes (void* event_data,
                                             void* user_data) {
  static struct attribute_spec ignore_attr = {
      "relite_ignore", 0, 0, 0, 0, 0, handle_ignore_attr
  };

  register_attribute(&ignore_attr);
}


static unsigned         instrumentation_pass() {
  if (errorcount != 0 || sorrycount != 0)
    return 0;
  gcc_assert(cfun != 0);
  relite_pass(&g_ctx);
  return 0;
}


int                     plugin_init         (struct plugin_name_args* info,
                                             struct plugin_gcc_version* ver) {
  if (strcmp(ver->basever, gcc_version.basever) != 0)
  {
    printf("relite: invalid gcc version\n");
    printf("relite: expected %s\n", gcc_version.basever);
    printf("relite: actual   %s\n", ver->basever);
    exit(1);
  }

  int do_pause = 0;
  g_ctx.opt_sblock_size  = 5;
  for (int i = 0; i != info->argc; i += 1) {
    if (strcmp(info->argv[i].key, "pause") == 0)
      do_pause = 1;
    else if (strcmp(info->argv[i].key, "debug") == 0)
      g_ctx.opt_debug = 1;
    else if (strcmp(info->argv[i].key, "sblock-size") == 0
        && atoi(info->argv[i].value) > 0)
      g_ctx.opt_sblock_size = atoi(info->argv[i].value);
    else if (strcmp(info->argv[i].key, "ignore") == 0)
      g_ctx.opt_ignore = xstrdup(info->argv[i].value);
  }

  if (do_pause) {
    char buf [16];
    printf("attach a debugger and press ENTER");
    scanf("%1c", buf);
  }

  static struct gimple_opt_pass pass_instrumentation = {{
    GIMPLE_PASS,
    "relite",                             /* name */
    NULL,                                 /* gate */
    instrumentation_pass,                 /* execute */
    NULL,                                 /* sub */
    NULL,                                 /* next */
    0,                                    /* static_pass_number */
    TV_NONE,                              /* tv_id */
    PROP_trees | PROP_cfg,                /* properties_required */
    0,                                    /* properties_provided */
    0,                                    /* properties_destroyed */
    0,                                    /* todo_flags_start */
    TODO_dump_cgraph | TODO_dump_func     /* todo_flags_finish */
  }};

  struct register_pass_info pass;
  pass.pass = &pass_instrumentation.pass;
  pass.reference_pass_name = "optimized";
  pass.ref_pass_instance_number = 1;
  pass.pos_op = PASS_POS_INSERT_AFTER;

  register_callback(info->base_name, PLUGIN_PASS_MANAGER_SETUP,
                    NULL, &pass);

  register_callback(info->base_name, PLUGIN_PRE_GENERICIZE,
                    plugin_prepass, 0);

  register_callback(info->base_name, PLUGIN_FINISH_UNIT,
                    &plugin_finish_unit, &g_ctx);

  register_callback(info->base_name, PLUGIN_ATTRIBUTES,
                    register_attributes, 0);

  return 0;
}





