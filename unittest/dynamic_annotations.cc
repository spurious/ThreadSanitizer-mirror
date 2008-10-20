#define DYNAMIC_ANNOTATIONS_HERE // see dynamic_annotations.h
#include "dynamic_annotations.h"  

#include <pthread.h>
#include <stdio.h>
#include <assert.h>
// Hack for our experiments with multi-threaded detector.
extern "C" void *DetectorThreadFunc(void *) {
//  printf("Hey there! I am DetectorThreadFunc()\n");
  return NULL;
}

struct DetectorThread {
 public:
  DetectorThread() {
    pthread_create(&t_, NULL, DetectorThreadFunc, NULL);
  };
  ~DetectorThread() {
    pthread_join(t_, NULL);
  }
 private:
  pthread_t t_;
};

static DetectorThread the_detector_thread;
