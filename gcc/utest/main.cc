#include <pthread.h>
#include <stdio.h>
#include <memory.h>


struct data_t
{
    pthread_mutex_t     mtx [2];
    int                 var [2];
};


struct context_t
{
    pthread_t           th;
    int                 test;
    int                 thread;
    data_t*             data;
};


void* thread_func(void* p)
{
    context_t* ctx = (context_t*)p;
    data_t* data = ctx->data;

    // no race
    if (ctx->test == 0)
    {
        if (ctx->thread)
        {
            data->var[0] = 1;
        }
        else
        {
            data->var[1] = 1;
        }
    }

    // store-store race
    else if (ctx->test == 1)
    {
      if (ctx->thread)
      {
        data->var[0] = 1;
      }
      else
      {
        memset(&data->var[0], 0, sizeof(data->var[0]));
      }
    }

    // load-store race
    else if (ctx->test == 2)
    {
        if (ctx->thread)
        {
            data->var[0] = 1;
        }
        else
        {
            int tmp = data->var[0];
            (void)tmp;
        }
    }

    // load-load no race
    else if (ctx->test == 3)
    {
        int tmp = data->var[0];
        (void)tmp;
    }

    // mutex sync, no race
    else if (ctx->test == 4)
    {
        pthread_mutex_lock(&data->mtx[0]);
        data->var[0] = 1;
        pthread_mutex_unlock(&data->mtx[0]);
    }

    // incorrect mutex sync, race
    else if (ctx->test == 5)
    {
        if (ctx->thread)
        {
            pthread_mutex_lock(&data->mtx[0]);
            data->var[0] = 1;
            pthread_mutex_unlock(&data->mtx[0]);
        }
        else
        {
            pthread_mutex_lock(&data->mtx[1]);
            data->var[0] = 2;
            pthread_mutex_unlock(&data->mtx[1]);
        }
    }

    return 0;
}





int main()
{
    for (int test = 0; test <= 5; test += 1)
    {
        printf("test #%d\n", test);

        data_t* data = new data_t;
        pthread_mutex_init(&data->mtx[0], 0);
        pthread_mutex_init(&data->mtx[1], 0);

        context_t* ctx = new context_t [2];
        for (int thread = 0; thread != 2; thread += 1)
        {
            ctx[thread].test = test;
            ctx[thread].thread = thread;
            ctx[thread].data = data;
            pthread_create(&ctx[thread].th, 0, thread_func, &ctx[thread]);
        }

        for (int thread = 0; thread != 2; thread += 1)
        {
          void* res;
          pthread_join(ctx[thread].th, &res);
        }
        printf("\n");
    }

    return 0;
}



/*
struct relite_call_desc_t {
  char const*                   func;
  char const*                   file;
  int                           line;
  int                           pos;
};


struct relite_mop_desc_t {
  char const*                   var;
  char const*                   file;
  int                           line;
  int                           pos;
};


struct relite_func_desc_t {
  int                               call_count;
  struct relite_call_desc_t const*  calls;
  int                               mop_count;
  struct relite_mop_desc_t const*   mops;
};

static relite_call_desc_t relite_calls [] = {
    {"x", "aaaaa", 1, 1},
    {"y", "bbbbb", 2, 2},
};

static relite_mop_desc_t relite_mops [] = {
    {"ccccc", 1, 1},
    {"ddddd", 2, 2},
};

static relite_func_desc_t relite_func_desc = {
    888, relite_calls, 999, relite_mops
};
*/

/*

void bar() {

}

void foo(int* x) {
  *x += 1;
  bar();
  memset(x, 0, sizeof(*x));
}


int main() {
  int x = 0;
  foo(&x);
  //printf("%d/%d", qweqweqwe.call_count, qweqweqwe.mop_count);
}
*/



