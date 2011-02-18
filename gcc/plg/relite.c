#include <gcc-plugin.h>
#include <plugin-version.h>
#include <config.h>
#include <system.h>
#include <coretypes.h>
#include <tm.h>
#include <tree-pass.h>
#include <diagnostic.h>
#include <stdio.h>
#include "relite_pass.h"

//#define RELITE_USE_PASS_MANAGER

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


#if RELITE_USE_PASS_MANAGER
static unsigned instrumentation_pass() {
  if (errorcount != 0 || sorrycount != 0)
    return;
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

#if RELITE_USE_PASS_MANAGER
  static struct gimple_opt_pass pass_instrumentation = {{
    GIMPLE_PASS,
    "relite",                             /* name */
    NULL,                                 /* gate */
    instrumentation_pass,                 /* execute */
    NULL,                                 /* sub */
    NULL,                                 /* next */
    0,                                    /* static_pass_number */
    TV_NONE,                              /* tv_id */
    PROP_ssa | PROP_cfg,                  /* properties_required */
    0,                                    /* properties_provided */
    0,                                    /* properties_destroyed */
    0,                                    /* todo_flags_start */
    TODO_verify_all | TODO_update_address_taken | TODO_update_ssa
  }};

  struct register_pass_info pass;
  pass.pass = &pass_instrumentation.pass;
  pass.reference_pass_name = "ssa";
  pass.ref_pass_instance_number = 1;
  pass.pos_op = PASS_POS_INSERT_BEFORE;

  register_callback(info->base_name, PLUGIN_PASS_MANAGER_SETUP,
                    NULL, &pass);
#else
  register_callback(info->base_name, PLUGIN_ALL_PASSES_START,
                    &instrumentation_pass, 0);
#endif

  register_callback(info->base_name, PLUGIN_FINISH_UNIT,
                    &plugin_finish_unit, &g_ctx);

  return 0;
}


