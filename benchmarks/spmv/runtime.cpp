#include "runtime.hpp"

namespace util {
namespace runtime {

size_t get_threads() {
  const char *threads_env = getenv("OMP_NUM_THREADS");
  int ret = 1;

  if (threads_env) {
    ret = atoi(threads_env);
    if (ret < 0)
      ret = 1;
  }

  return ret;
}

void setaffinity_oncpu(unsigned int cpu) {
  cpu_set_t cpu_mask;

  CPU_ZERO(&cpu_mask);
  CPU_SET(cpu, &cpu_mask);

  int err = sched_setaffinity(0, sizeof(cpu_set_t), &cpu_mask);
  if (err) {
    std::cout << "sched_setaffinity() failed" << std::endl;
    exit(1);
  }
}

} // end of namespace runtime
} // end of namespace util
