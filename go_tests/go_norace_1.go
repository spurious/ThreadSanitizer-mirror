// $G $F.go && $L $F.$A && ./$A.out
// gccgo -g -c $F.go && gccgo -g -static-libgo -lpthread $F.o -o $F && ./$F

// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// A 'negative' test for race detector.
// A race detector should be silent here.

package main
import "time"
import "sync"


const second  = 1000000000;

var obj int;
var c = make(chan int, 10)

func inc() {
 var t int;
 t = obj;
 obj = t + 1;
}

func Thread1() {
  inc();
  c <- 0;
  time.Sleep(1 * second);
}

func Thread2() {
  <-c;
  inc();
  time.Sleep(1 * second);
}

func main() {
  go Thread1();
  go Thread2();
  time.Sleep(2 * second);
  print(obj, "\n");
}
