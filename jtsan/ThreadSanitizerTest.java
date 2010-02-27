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
 */

import java.lang.reflect.Method;
import java.util.TreeMap;
import java.util.TreeSet;
import java.util.regex.Pattern;
import java.util.regex.Matcher;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Semaphore;
import java.util.concurrent.locks.ReentrantReadWriteLock;
import java.util.concurrent.atomic.AtomicInteger;

// All tests for a Java race detector.
public class ThreadSanitizerTest {

  // Run tests.
  // If no arguments given, run all tests.
  // If there are parameters not starting with '-', run tests that match the regexp.
  // If there are parameters starting with '-', exclude tests that match the regexp.
  public static void main (String[] args) {
    System.out.println("ThreadSanitizerTest:");
    ThreadSanitizerTest t = new ThreadSanitizerTest();

    String tests_to_run = new String();
    String tests_to_exclude = new String();

    for (String arg: args) {
      if (arg.startsWith("--")) {
        // Parameter. Don't need them yet.
      } else if (arg.startsWith("-")) {
        // test to exclude.
        if (tests_to_exclude.length() > 0) {
          tests_to_exclude += "|";
        }
        tests_to_exclude += arg.substring(1);
      } else {
        // test to run.
        if (tests_to_run.length() > 0) {
          tests_to_run += "|";
        }
        tests_to_run += arg;
      }
    }

    if (tests_to_run.length() == 0) {
      tests_to_run = "test.*";
    } else {
      System.out.println("Running tests:  " + tests_to_run);
    }

    if (tests_to_exclude.length() > 0) {
      System.out.println("Excluded tests: " + tests_to_exclude);
    }

    // invoke all methods that start with 'test'
    Class test_class = ThreadSanitizerTest.class;
    Method[] methods = test_class.getDeclaredMethods();
    Pattern include_pattern = Pattern.compile(tests_to_run);
    Pattern exclude_pattern = Pattern.compile(tests_to_exclude);
    Pattern positive_test_pattern = Pattern.compile("testPositive_.*");
    for (Method method : methods) {
      String method_name = method.getName();
      if (method_name.startsWith("test") &&
          include_pattern.matcher(method_name).matches() &&
          !exclude_pattern.matcher(method_name).matches() ) {
        System.out.println("======================= " + method_name +
                           " =======================");
        boolean is_positive_test = positive_test_pattern.matcher(method_name).matches();
        if (is_positive_test) {
          RaceDetectorApi.ExpectRaceBegin();
        }
        try {
          method.invoke(t);
        } catch (Exception e) {
          assert false : e;
        }
        if (is_positive_test) {
          RaceDetectorApi.ExpectRaceEnd();
        }
      }
    }
  }

  protected static volatile boolean static_volatile_bool_var;

  class ThreadRunner {
    protected int shared_var = 0;
    protected short shared_short_var = 0;
    protected char shared_char_var = 0;
    protected long shared_long_var = 0;
    protected int shared_var2 = 0;
    protected Integer shared_obj;

    // Virtual functions. Overrride some of them in your test.
    public void thread1() { System.out.println("thread1"); }
    public void thread2() { System.out.println("thread2"); }
    public void thread3() { System.out.println("thread3"); }
    public void thread4() { System.out.println("thread4"); }
    public int  nThreads() { return 3; }
    public void setUp() { }
    public void tearDown() { }

    public void foo1() { thread1(); }
    public void foo2() { thread2(); }
    public void foo3() { thread3(); }
    public void foo4() { thread4(); }

    // Run threadN in separate threads, then join them all.
    public ThreadRunner() {
//      System.out.printf("Running %d threads\n", nThreads());
      class MyThread extends Thread {
        public MyThread(ThreadRunner thread_runner) { runner = thread_runner; }
        protected ThreadRunner runner;
      }

      Thread threads[] = new Thread[4];
      threads[0] = new MyThread(this) { public void run() { runner.foo1(); } };
      threads[1] = new MyThread(this) { public void run() { runner.foo2(); } };
      threads[2] = new MyThread(this) { public void run() { runner.foo3(); } };
      threads[3] = new MyThread(this) { public void run() { runner.foo4(); } };

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
      } catch (java.lang.InterruptedException e) {
        System.out.println("InterruptedException");
      }
    }
  }

  class ThreadRunner1 extends ThreadRunner { public int  nThreads() { return 1; } }
  class ThreadRunner2 extends ThreadRunner { public int  nThreads() { return 2; } }
  class ThreadRunner3 extends ThreadRunner { public int  nThreads() { return 3; } }
  class ThreadRunner4 extends ThreadRunner { public int  nThreads() { return 4; } }

  private void describe(String str) {
    System.out.println(str);
  }

  private void shortSleep() {
    try { Thread.sleep(100); } catch (Exception e) { }
  }
  private void longSleep() {
    try { Thread.sleep(500); } catch (Exception e) { }
  }


  //------------------ Positive tests ---------------------
  public void testPositive_WW_NoLocks() {
    describe("Race: two unlocked writes");
    new ThreadRunner2() {
      public void thread1() { shared_var = 1; }
      public void thread2() { shared_var = 2; }
    };
  }

  public void testPositive_WW_NoLocksShort() {
    describe("Race: two unlocked writesl; short");
    new ThreadRunner2() {
      public void thread1() { shared_short_var = 1; }
      public void thread2() { shared_short_var = 2; }
    };
  }

  public void testPositive_WW_NoLocksChar() {
    describe("Race: two unlocked writesl; char");
    new ThreadRunner2() {
      public void thread1() { shared_char_var = 1; }
      public void thread2() { shared_char_var = 2; }
    };
  }

  public void testPositive_WW_NoLocksLong() {
    describe("Race: two unlocked writesl; long");
    new ThreadRunner2() {
      public void thread1() { shared_long_var = 1; }
      public void thread2() { shared_long_var = 2; }
    };
  }

  public void testPositive_WW_NoLocksObj() {
    describe("Race: two unlocked writesl; long");
    new ThreadRunner2() {
      public void thread1() { shared_obj = new Integer(0); }
      public void thread2() { shared_obj = new Integer(1); }
    };
  }

  public void testPositive_WW_LockedVsUnlocked() {
    describe("Race: one locked and one unlocked write");
    new ThreadRunner2() {
      public void thread1() { shared_var++; }
      public void thread2() { synchronized(this) { shared_var++; } }
    };
  }

  public void testPositive_WW_DifferentLocks() {
    describe("Race: two writes locked with different locks");
    final Object lock = new Object();
    new ThreadRunner2() {
      public void thread1() { synchronized(lock) { shared_var++; } }
      public void thread2() { synchronized(this) { shared_var++; } }
    };
  }

  public void testPositive_WW_LockInBetween() {
    describe("Race: two unlocked writes, ciritcal sections between them");
    new ThreadRunner2() {
      public void thread1() {
        shared_var = 1;
        synchronized(this) {}
      }
      public void thread2() {
        longSleep();
        synchronized(this) {}
        shared_var = 2;
      }
    };
  }

  public void testPositive_WritingUnderReaderLock() {
    describe("Race: writing under a reader lock");
    final ReentrantReadWriteLock lock = new ReentrantReadWriteLock();
    new ThreadRunner4() {
      public void tearDown() {
        // may print 4, 3, or even 2
        System.out.println("shared_var = " + shared_var);
      }
      public void thread1() {
        // writing with a reader lock held
        lock.readLock().lock();
        int t = shared_var;
        shortSleep();  // Put this sleep here so that the race may actually happen.
        shared_var = t + 1;
        lock.readLock().unlock();
      }
      public void thread2() { thread1(); }
      public void thread3() { thread1(); }

      public void thread4() {
        // writing with a writer lock held
        lock.writeLock().lock();
        shared_var++;
        lock.writeLock().unlock();
      }
    };
  }


  //------------------ Negative tests ---------------------
  public void testNegative_NoOp() {
    Object obj = new Integer(0);
    RaceDetectorApi.NoOp(obj);
  }
  public void testNegative1() {
    describe("Correct code: two locked updates");
    new ThreadRunner2() {
      public void thread1() { synchronized(this) { shared_var++; } }
      public void thread2() { synchronized(this) { shared_var++; } }
    };
  }

  public void testNegative2() {
    describe("Correct code: distinct fields of the same object are written");
    new ThreadRunner2() {
      public void thread1() { shared_var = 1; }
      public void thread2() { shared_var2 = 2; }
    };
  }

  public void testNegative_LocalVolatile() {
    describe("Correct code: two accesses to a volatile boolean");
    new ThreadRunner2() {
      volatile boolean volatile_bool = false;
      public void thread1() { volatile_bool = true; }
      public void thread2() { while(!volatile_bool); }
    };
  }

  public void testNegative_SyncWithLocalVolatile() {
    describe("Correct code: volatile boolean is used as a synchronization");
    new ThreadRunner2() {
      volatile boolean volatile_bool = false;
      public void thread1() {
        shared_var = 1;
        volatile_bool = true;
      }
      public void thread2() {
        while(!volatile_bool);
        shared_var = 2;
      }
    };
  }

  public void testNegative_SyncWithStaticVolatile() {
    describe("Correct code: static volatile boolean is used as a synchronization");
    new ThreadRunner2() {
      public void setUp() {
        static_volatile_bool_var = false;
      }
      public void thread1() {
        shared_var = 1;
        static_volatile_bool_var = true;
      }
      public void thread2() {
        while(!static_volatile_bool_var);
        shared_var = 2;
      }
    };
  }



  public void testNegative3() {
    describe("Correct code: sending a message via a locked object");
    new ThreadRunner2() {
      Integer locked_object;

      public void thread1() {
        Integer message = new Integer(42);
        synchronized (this) {
          locked_object = message;
        }
      }

      public void thread2() {
        Integer message;
        while (true) {
          synchronized (this) {
            message = locked_object;
            if (message != null) break;
          }
          shortSleep();
        }
        message++;
        assert message.intValue() == 43;
      }
    };
  }

  public void testNegative4() {
    describe("Correct code: passing ownership via a locked boolean");
    new ThreadRunner2() {
      private boolean signal = false;

      public void thread1() {
        assert shared_var == 0;
        shared_var = 1;
        longSleep();
        synchronized(this) {
          signal = true;
        }
      }

      public void thread2() {
        while (true) {
          synchronized(this) {
            if (signal) break;
          }
          shortSleep();
        }
        assert shared_var == 1;
        shared_var = 2;
      }
    };
  }


  public void testNegative5() {
    describe("Correct code: passing ownership via a locked map");
    new ThreadRunner2() {
      private TreeMap<Integer, Integer> map;

      public void setUp() {
        map = new TreeMap<Integer, Integer>();
        shared_obj = new Integer(0);
      }

      public void thread1() {
        shared_obj = 42;
        synchronized (this) {
          map.put(1, shared_obj);
        }
      }

      public void thread2() {
        Integer message;
        while (true) {
          synchronized (this) {
            message = map.get(1);
            if (message != null) break;
          }
          shortSleep();
        }
        message++;
        assert message == 43;
      }
    };
  }

  public void testNegative6() {
    describe("Correct code: passing object ownership via a locked boolean; 4 threads");
    new ThreadRunner4() {
      Object lock;
      int counter;

      public void setUp() {
        shared_obj = new Integer(0);
        lock = new Object();
        counter = 2;
      }

      public void thread1() {
        synchronized (lock) {
          shared_obj++;
        }
        synchronized (this)  {
          counter--;
        }
      }

      public void thread2() { thread1(); }

      public void thread3() {
        Integer message;
        while (true) {
          synchronized (this) {
            if (counter == 0) break;
          }
          shortSleep();
        }
        assert shared_obj == 2;
      }
      public void thread4() { thread3(); }
    };
  }


  public void testNegative7() {
    describe("Correct code: reg test for MTRAT (broken as of 11/06/09)");
    new ThreadRunner3() {
      Object lock;

      public void setUp() {
        shared_obj = new Integer(0);
        lock = new Object();
      }

      public void thread1() {
        synchronized (lock) {
          shared_obj++;
        }
        synchronized (this)  {
          shared_var--;
        }
      }

      public void thread2() { thread1(); }
      public void thread3() { }
    };
  }

  public void testNegative8() {
    describe("Correct code: accessing different fields of class by different threads");
    new ThreadRunner4() {
      int a, b;
      Integer c, d;
      public void setUp() {
        c = new Integer(0);
        d = new Integer(0);
      }

      public void thread1() { a++; }
      public void thread2() { b++; }
      public void thread3() { c++; }
      public void thread4() { d++; }
    };
  }

  public void testNegative9() {
    describe("Correct code: notify/wait");
    new ThreadRunner2() {
      boolean done;
      public void setUp() {
        done = false;
      }

      public synchronized void send() {
        done = true;
        notify();
      }

      public synchronized void receive() {
        while(!done) {
          try { wait(); } catch (Exception e) {}
        }
      }

      public void thread1() {
        shortSleep();
        shared_var = 1;
        send();
      }
      public void thread2() {
        receive();
        shared_var++;
      }
    };
  }

  public void testNegative10() {
    describe("Correct code: notify/wait; 4 threads");
    new ThreadRunner4() {
      int counter;
      Object lock1;
      Object lock2;
      public void setUp() {
        counter = 2;
        lock1 = new Object();
        lock2 = new Object();
      }

      public void tearDown() {
        assert shared_var == 3;
      }

      public synchronized void send() {
        counter--;
        notifyAll();
      }

      public synchronized void receive() {
        while (counter != 0) {
          try { wait(); } catch (Exception e) {}
        }
      }

      public void thread1() {
        shortSleep();
        synchronized (lock1) {
          shared_var = 1;
        }
        send();
      }

      public void thread2() {
        receive();
        synchronized (lock2) {
          shared_var++;
        }
      }

      public void thread3() { thread1(); }
      public void thread4() { thread2(); }
    };
  }

  /*
  public void testNegative11() {
    describe("Correct code: synchronization via thread create/join");
    class Foo { public int a; }
    final Foo foo = new Foo();
    foo.a++;
    Thread t = new Thread() { public void run () { foo.a++; } };
    t.start();
    try { t.join(); } catch (Exception e) { assert false; }
    foo.a++;
    assert foo.a == 3;
  }*/

  public void testNegative_CountDownLatch() {
    describe("Correct code: CountDownLatch");
    new ThreadRunner4() {
      CountDownLatch latch;
      public void setUp() {
        latch = new CountDownLatch(3);
        shared_var = 0;
      }

      public void thread1() {
        try {
          latch.await();
        } catch (InterruptedException ex) { }
        assert shared_var == 3;
        shared_var = 4;
      }

      public void thread2() {
        synchronized (this) {
          shared_var++;
        }
        latch.countDown();
      }
      public void thread3() { thread2(); }
      public void thread4() { thread2(); }
    };
  }

  public void testNegative_Semaphore() {
    describe("Correct code: Semaphore");
    final Semaphore sem = new Semaphore(0);
    new ThreadRunner2() {
      public void tearDown() {
        assert shared_var == 2;
      }

      public void thread1() {
        longSleep();
        shared_var = 1;
        sem.release();
      }

      public void thread2() {
        try { sem.acquire(); } catch (Exception e) { assert false : e; }
        shared_var = 2;
      }
    };
  }

  public void testNegative_ReadWriteLock() {
    describe("Correct code: ReadWriteLock");
    final ReentrantReadWriteLock lock = new ReentrantReadWriteLock();
    new ThreadRunner3() {
      public void thread1() {
        // reading with a reader lock held
        lock.readLock().lock();
        int v = shared_var;
        lock.readLock().unlock();
      }
      public void thread2() {
        // reading with a writer lock held
        lock.writeLock().lock();
        int v = shared_var;
        lock.writeLock().unlock();
      }
      public void thread3() {
        // writing with a writer lock held
        lock.writeLock().lock();
        shared_var++;
        lock.writeLock().unlock();
      }
    };
  }

  public void testNegative_AtomicInteger() {
    describe("Correct code: AtomicInteger");
    final AtomicInteger i = new AtomicInteger();
    new ThreadRunner4() {
      public void tearDown() { assert i.intValue() == 4; }
      public void thread1() { i.incrementAndGet(); }
      public void thread2() { i.incrementAndGet(); }
      public void thread3() { i.incrementAndGet(); }
      public void thread4() { i.incrementAndGet(); }
    };
  }

  public void testNegative_NoReportOnLocks() {
    describe("Correct code: reg test for a bug (???)");
    new ThreadRunner2() {
      public void thread1() {
        shared_var = 1;
      }
      public void thread2() {
        longSleep();
      }
    };
  }

}
