#gcc -c -fPIC -Wall -O0 -g3 -std=gnu99 earthquake.c
#gcc -c -fPIC -Wall -O0 -g3 -std=gnu99 earthquake_wrap.c
#gcc -c -fPIC -Wall -O0 -g3 -std=gnu99 earthquake_core.c
#gcc -shared earthquake.o earthquake_wrap.o earthquake_core.o -ldl -lpthread -lrt -o earthquake.so


gcc -shared -fPIC -Wall -O2 -g -std=gnu99 earthquake.c earthquake_wrap.c earthquake_core.c -ldl -lpthread -lrt -o earthquake.so

