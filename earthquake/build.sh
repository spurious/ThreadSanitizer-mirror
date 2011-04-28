gcc -shared -fPIC -Wall -O2 -g -std=gnu99 earthquake.c earthquake_wrap.c earthquake_core.c -ldl -lpthread -lrt -o earthquake.so

