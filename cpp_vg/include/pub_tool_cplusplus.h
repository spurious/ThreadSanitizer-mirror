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
