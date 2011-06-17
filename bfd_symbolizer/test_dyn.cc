int dyn1_line = __LINE__; extern "C" void* dyn1(int, int) { lll: return &&lll; }
int dyn2_line = __LINE__; int dyn2; void* get_dyn2() { return &dyn2; }



