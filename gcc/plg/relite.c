/* Relite
 * Copyright (c) 2011, Google Inc.
 * All rights reserved.
 * Author: Dmitry Vyukov (dvyukov@google.com)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <gcc-plugin.h>
#include <plugin-version.h>
#include <plugin.h>
#include <config.h>
#include <system.h>
#include <coretypes.h>
#include <tm.h>
#include <tree-pass.h>
#include <diagnostic.h>
#include <stdio.h>
#include "relite_pass.h"

#define RELITE_USE_PASS_MANAGER

// required by gcc plugin machinery
bool plugin_is_GPL_compatible = false;


// global contest - there is no way to pass contest to compilation passes
relite_context_t g_ctx;


static void plugin_finish_unit(void* event_data, void* user_data)
{
  (void)event_data;
  (void)user_data;
  relite_finish(&g_ctx);

}


static tree handle_ignore_attribute(tree* node, tree name, tree args,
                                  int flags, bool* no_add_attrs)
{
  return NULL_TREE;
}


static struct attribute_spec ignore_attr = {
    "relite_ignore",
    0,
    0,
    0,
    0,
    0,
    handle_ignore_attribute
};


static void register_attributes(void* event_data, void* user_data) {
  register_attribute(&ignore_attr);
}


#ifdef RELITE_USE_PASS_MANAGER
static unsigned instrumentation_pass() {
  if (errorcount != 0 || sorrycount != 0)
    return 0;
  gcc_assert(cfun != 0);
  relite_pass(&g_ctx, cfun);
  return 0;
}
#else
static void instrumentation_pass(void* event_data, void* user_data)
{
  (void)event_data;
  (void)user_data;
  if (errorcount != 0 || sorrycount != 0)
    return;
  gcc_assert(cfun != 0);
  relite_pass(&g_ctx, cfun);
}
#endif


int plugin_init(struct plugin_name_args* info,
                struct plugin_gcc_version* ver) {
  if (strcmp(ver->basever, gcc_version.basever) != 0)
  {
    printf("relite: invalid gcc version\n");
    printf("relite: expected %s\n", gcc_version.basever);
    printf("relite: actual   %s\n", ver->basever);
    exit(1);
  }

  setup_context(&g_ctx);

  int do_pause = 0;
  for (int i = 0; i != info->argc; i += 1) {
    if (strcmp(info->argv[i].key, "pause") == 0)
      do_pause = 1;
    else if (strcmp(info->argv[i].key, "debug") == 0)
      g_ctx.opt_debug = 1;
  }

  if (do_pause) {
    char buf [16];
    printf("attach a debugger and press ENTER");
    scanf("%1c", buf);
  }

#ifdef RELITE_USE_PASS_MANAGER
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
    0/*
    ((unsigned)-1)
      &~TODO_cleanup_cfg
      &~TODO_update_address_taken
      &~TODO_rebuild_alias
      &~TODO_rebuild_frequencies
      &~TODO_remove_functions
      &~TODO_dump_cgraph
      &~TODO_ggc_collect
      //&~TODO_df_finish
      //TODO_verify_all | TODO_update_address_taken | TODO_update_ssa
       */
  }};

  struct register_pass_info pass;
  pass.pass = &pass_instrumentation.pass;
  //pass.reference_pass_name = "*warn_unused_result";
  pass.reference_pass_name = "optimized";
  pass.ref_pass_instance_number = 1;
  pass.pos_op = PASS_POS_INSERT_AFTER;

  register_callback(info->base_name, PLUGIN_PASS_MANAGER_SETUP,
                    NULL, &pass);
#else
  register_callback(info->base_name, PLUGIN_ALL_PASSES_END,
                    &instrumentation_pass, 0);
#endif

  register_callback(info->base_name, PLUGIN_FINISH_UNIT,
                    &plugin_finish_unit, &g_ctx);

  register_callback(info->base_name, PLUGIN_ATTRIBUTES,
                    register_attributes, 0);

  return 0;
}


