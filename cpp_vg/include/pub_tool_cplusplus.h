// TODO: license!
void PushMallocCostCenter(const char *cc);
void PopMallocCostCenter();

class ScopedMallocCostCenter {
 public:
  ScopedMallocCostCenter(const char *cc) {
#ifndef NDEBUG  // What is the right macro? 
      PushMallocCostCenter(cc);
#endif
  }
  ~ScopedMallocCostCenter() {
#ifndef NDEBUG
      PopMallocCostCenter();
#endif
  }
};

// This allocator can be used to replace the standard allocator 
// in STL containers.
template <class T, const char **cc>
class CCAlloc : public std::allocator<T> {
 public:
  T* allocate(long n, const void *hint = 0) {
    ScopedMallocCostCenter cost_center(*cc);
    return std::allocator<T>::allocate(n, hint);
  }
};
