//===-- tsan_vector.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//

// Low-fat STL-like vector container.

#ifndef TSAN_VECTOR_H
#define TSAN_VECTOR_H

#include "tsan_defs.h"
#include "tsan_mman.h"

namespace __tsan {

template<typename T>
class Vector {
 public:
  explicit Vector(MBlockType typ)
      : typ_(typ)
      , begin_()
      , end_()
      , last_() {
  }

  ~Vector() {
    if (begin_)
      internal_free(begin_);
  }

  uptr Size() const {
    return end_ - begin_;
  }

  T &operator[](uptr i) {
    DCHECK_LT(i, end_ - begin_);
    return begin_[i];
  }

  const T &operator[](uptr i) const {
    DCHECK_LT(i, end_ - begin_);
    return begin_[i];
  }

  void PushBack(T v) {
    if (end_ == last_) {
      uptr cap0 = last_ - begin_;
      uptr cap = 2 * cap0;
      if (cap == 0)
        cap = 16;
      T *p = (T*)internal_alloc(typ_, cap * sizeof(T));
      if (cap0) {
        internal_memcpy(p, begin_, cap0 * sizeof(T));
        internal_free(begin_);
      }
      begin_ = p;
      end_ = begin_ + cap0;
      last_ = begin_ + cap;
    }
    end_[0] = v;
    end_++;
  }

 private:
  const MBlockType typ_;
  T *begin_;
  T *end_;
  T *last_;

  Vector(const Vector&);
  void operator=(const Vector&);
};
}

#endif  // #ifndef TSAN_VECTOR_H
