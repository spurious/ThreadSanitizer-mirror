/* Copyright (c) 2009, Google Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
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
 *
 * ---
 * Author: Konstantin Serebryany <opensource@google.com>
 *
 * This file contains unittests for a race detector for java.
 *
 */


// JUnit imports.
import junit.framework.TestCase;
import junit.framework.Test;
import junit.framework.TestSuite;
import junit.framework.Assert;
import org.junit.runner.JUnitCore;

// Other imports.
import java.util.TreeMap;

// All tests for a Java race detector.
public class ThreadSanitizerTest extends TestCase {

  public static void main (String[] args) {
    System.out.println("ThreadSanitizerTest:");
    org.junit.runner.JUnitCore.runClasses(ThreadSanitizerTest.class);
  }

  class ThreadRunner {
    protected int shared_var_ = 0;
    protected Integer shared_obj_;

    // Virtual functions. Overrride some of them in your test.
    public void thread1() { System.out.println("thread1"); }
    public void thread2() { System.out.println("thread2"); }
    public void thread3() { System.out.println("thread3"); }
    public void thread4() { System.out.println("thread4"); }
    public int  nThreads() { return 3; }
    public void setUp() { }
    public void tearDown() { }

    // Run threadN in separate threads, then join them all.
    public ThreadRunner() {
//      System.out.printf("Running %d threads\n", nThreads());
      class MyThread extends Thread {
        public MyThread(ThreadRunner runner) { runner_ = runner; }
        protected ThreadRunner runner_;
      }

      Thread threads[] = new Thread[4];
      threads[0] = new MyThread(this) { public void run() { runner_.thread1(); } };
      threads[1] = new MyThread(this) { public void run() { runner_.thread2(); } };
      threads[2] = new MyThread(this) { public void run() { runner_.thread3(); } };
      threads[3] = new MyThread(this) { public void run() { runner_.thread4(); } };

      try {
        setUp();
        for (int i = 0; i < nThreads(); i++) {
          threads[i].start();
          Thread.sleep(100);
        }
        for (int i = 0; i < nThreads(); i++) {
          threads[i].join();
        }
        tearDown();
      } catch (Exception e) {
      }
    }
  }

  class ThreadRunner1 extends ThreadRunner { public int  nThreads() { return 1; } }
  class ThreadRunner2 extends ThreadRunner { public int  nThreads() { return 2; } }
  class ThreadRunner3 extends ThreadRunner { public int  nThreads() { return 3; } }
  class ThreadRunner4 extends ThreadRunner { public int  nThreads() { return 4; } }

  private void describe(String str) {
    System.out.printf("---------- " + str + "\n");
  }

  private void shortSleep() {
    try { Thread.sleep(100); } catch (Exception e) { }
  }
  private void longSleep() {
    try { Thread.sleep(500); } catch (Exception e) { }
  }

  //------------------ Positive tests ---------------------
  public void testPositive1() throws Exception {
    describe("Simple race: two unlocked writes");
    new ThreadRunner2() {
      public void thread1() { shared_var_ = 1; }
      public void thread2() { shared_var_ = 2; }
    };
  }

  public void testPositive2() throws Exception {
    describe("Simple race: one locked and one unlocked write");
    new ThreadRunner2() {
      public void thread1() { shared_var_++; }
      public void thread2() { synchronized(this) { shared_var_++; } }
    };
  }

  //------------------ Negative tests ---------------------
  public void testNegative1() throws Exception {
    describe("Correct code: two locked updates");
    new ThreadRunner2() {
      public void thread1() { synchronized(this) { shared_var_++; } }
      public void thread2() { synchronized(this) { shared_var_++; } }
    };
  }

  public void testNegative2() throws Exception {
    describe("Correct code: two writes to a volatile int");
    new ThreadRunner2() {
      volatile int volatile_var_;
      public void thread1() { volatile_var_ = 1; }
      public void thread2() { volatile_var_ = 2; }
    };
  }

  public void testNegative3() throws Exception {
    describe("Correct code: sending a message via a locked object");
    new ThreadRunner2() {
      Integer locked_object_;

      public void thread1() {
        Integer message = new Integer(42);
        synchronized (this) {
          locked_object_ = message;
        }
      }

      public void thread2() {
        Integer message;
        while (true) {
          synchronized (this) {
            message = locked_object_;
            if (message != null) break;
          }
          shortSleep();
        }
        message++;
        Assert.assertNotNull(message);
        Assert.assertEquals(message.intValue(), 43);
      }
    };
  }

  public void testNegative4() throws Exception {
    describe("Correct code: passing ownership via a locked boolean");
    new ThreadRunner2() {
      private boolean signal_ = false;

      public void thread1() {
        Assert.assertEquals(shared_var_, 0);
        shared_var_ = 1;
        longSleep();
        synchronized(this) {
          signal_ = true;
        }
      }

      public void thread2() {
        while (true) {
          synchronized(this) {
            if (signal_) break;
          }
          shortSleep();
        }
        Assert.assertEquals(shared_var_, 1);
        shared_var_ = 2;
      }
    };
  }


  public void testNegative5() throws Exception {
    describe("Correct code: passing ownership via a locked map");
    new ThreadRunner2() {
      private TreeMap<Integer, Integer> map_;

      public void setUp() {
        map_ = new TreeMap<Integer, Integer>();
        shared_obj_ = new Integer(0);
      }

      public void thread1() {
        shared_obj_ = 42;
        synchronized (this) {
          map_.put(1, shared_obj_);
        }
      }

      public void thread2() {
        Integer message;
        while (true) {
          synchronized (this) {
            message = map_.get(1);
            if (message != null) break;
          }
          shortSleep();
        }
        message++;
        Assert.assertNotNull(message);
        Assert.assertEquals(message.intValue(), 43);
      }
    };
  }

  public void testNegative6() throws Exception {
    describe("Correct code: passing object ownership via a locked boolean; 4 threads");
    new ThreadRunner4() {
      Object lock_;
      int counter_;

      public void setUp() {
        shared_obj_ = new Integer(0);
        lock_ = new Object();
        counter_ = 2;
      }

      public void thread1() {
        synchronized (lock_) {
          shared_obj_++;
        }
        synchronized (this)  {
          counter_--;
        }
      }

      public void thread2() { thread1(); }

      public void thread3() {
        Integer message;
        while (true) {
          synchronized (this) {
            if (counter_ == 0) break;
          }
          shortSleep();
        }
        Assert.assertNotNull(shared_obj_);
        Assert.assertEquals(shared_obj_.intValue(), 2);
      }
      public void thread4() { thread3(); }
    };
  }


  public void testNegative7() throws Exception {
    describe("Correct code: reg test for MTRAT (broken as of 11/06/09)");
    new ThreadRunner3() {
      Object lock_;

      public void setUp() {
        shared_obj_ = new Integer(0);
        lock_ = new Object();
      }

      public void thread1() {
        synchronized (lock_) {
          shared_obj_++;
        }
        synchronized (this)  {
          shared_var_--;
        }
      }

      public void thread2() { thread1(); }
      public void thread3() { }
    };
  }



  public void testNegative8() throws Exception {
    describe("Correct code: accessing different fields of class by different threads");
    new ThreadRunner4() { 
      int a_, b_;
      Integer c_, d_;
      public void setUp() {
        c_ = new Integer(0);
        d_ = new Integer(0);
      }

      public void thread1() { a_++; }
      public void thread2() { b_++; }
      public void thread3() { c_++; }
      public void thread4() { d_++; }
    };
  }

  public void testNegative9() throws Exception {
    describe("Correct code: notify/wait");
    new ThreadRunner2() {
      boolean done_;
      public void setUp() {
        done_ = false; 
      }

      public synchronized void send() {
        done_ = true;
        notify();
      }

      public synchronized void receive() {
        while(!done_) {
          try { wait(); } catch (Exception e) {}
        }
      }

      public void thread1() { 
        shortSleep();
        shared_var_ = 1;
        send();
      }
      public void thread2() { 
        receive();
        shared_var_++;
      }
    };
  }

  public void testNegative10() throws Exception {
    describe("Correct code: notify/wait; 4 threads");
    new ThreadRunner4() {
      int counter_;
      Object lock1_;
      Object lock2_;
      public void setUp() {
        counter_ = 2;
        lock1_ = new Object();
        lock2_ = new Object();
      }

      public void tearDown() {
        Assert.assertEquals(shared_var_, 4);
      }

      public synchronized void send() {
        counter_--;
        notifyAll();
      }

      public synchronized void receive() {
        while (counter_ != 0) {
          try { wait(); } catch (Exception e) {}
        }
      }

      public void thread1() { 
        shortSleep();
        synchronized (lock1_) {
          shared_var_ = 1;
        }
        send();
      }

      public void thread2() {
        receive();
        synchronized (lock2_) {
          shared_var_++;
        }
      }

      public void thread3() { thread1(); }
      public void thread4() { thread2(); }
    };
  }


  public void testNegative11() throws Exception {
    describe("Correct code: synchronization via thread create/join");

    class ThreadThatAccessesInteger extends Thread {
      public ThreadThatAccessesInteger(Integer obj) { obj_ = obj; }
      private Integer obj_;
      public void run () { obj_++; }
    }

    Integer obj = new Integer(0);
    obj++;
    Thread t = new ThreadThatAccessesInteger(obj);
    t.start();
    try { t.join(); } catch (Exception e) { }
    obj++;
    Assert.assertEquals(obj.intValue(), 3);
  }
}
