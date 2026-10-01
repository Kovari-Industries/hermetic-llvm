// Require the GNU DSO ABI, not an unversioned static compiler-rt fallback.
__asm__(".symver __gcc_personality_v0,__gcc_personality_v0@GCC_3.3.1");

static void count_cleanup(int** counter) { ++**counter; }

void call_through_c_cleanup(void (*callback)(int*), int* c_cleanup,
                            int* cpp_cleanup) {
  int* counter __attribute__((cleanup(count_cleanup))) = c_cleanup;
  callback(cpp_cleanup);
}
