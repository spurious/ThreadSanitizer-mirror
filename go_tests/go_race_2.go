// $G $F.go && $L $F.$A && ./$A.out
// gccgo -g -c $F.go && gccgo -g -static-libgo -lpthread $F.o -o $F && ./$F

// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// A 'positive' test for race detector.
// A race detector should detect a race on 'racey'
// and tell that the lock: l1 is reader-held during a write in Thread2.

package main
import "time"
import "sync"

var l1 sync.RWMutex;

const second  = 1000000000;

var racey int;

func inc() {
 var t int;
 t = racey;
 racey = t + 1;
}

func Thread1() {
  l1.Lock();
  inc();
  l1.Unlock();
  time.Sleep(1 * second);
}

func Thread2() {
  l1.RLock();
  inc();
  l1.RUnlock();
  time.Sleep(1 * second);
}

func main() {
  go Thread1();
  go Thread2();
  time.Sleep(2 * second);
}
