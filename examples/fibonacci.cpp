#include <taskflow/taskflow.hpp>

tf::Executor& get_executor() {
  static tf::Executor executor;
  return executor;
}

// ------------------------------------------------------------------------------------------------
// implementation using subflow
// ------------------------------------------------------------------------------------------------

size_t spawn_subflow(size_t n, tf::Subflow& sbf) {
  
  if (n < 2) {
    return n;
  }

  size_t res1, res2;

  // compute f(n-1)
  sbf.emplace([&res1, n] (tf::Subflow& sbf_n_1) { res1 = spawn_subflow(n - 1, sbf_n_1); } )
     .name(std::to_string(n-1));

  // compute f(n-2)
  sbf.emplace([&res2, n] (tf::Subflow& sbf_n_2) { res2 = spawn_subflow(n - 2, sbf_n_2); } )
     .name(std::to_string(n-2));

  sbf.join();
  return res1 + res2;
}

size_t fibonacci_subflow(size_t N) {

  size_t res;  // result

  tf::Taskflow taskflow("fibonacci");

  taskflow.emplace([&res, N] (tf::Subflow& sbf) {
    res = spawn_subflow(N, sbf);
  }).name(std::to_string(N));

  get_executor().run(taskflow).wait();
  
  return res;
}

// ------------------------------------------------------------------------------------------------
// implementation using async
// ------------------------------------------------------------------------------------------------

size_t spawn_async(size_t N) {

  if (N < 2) {
    return N; 
  }

  auto& executor = get_executor();

  auto fu1 = executor.async([=](){ return spawn_async(N-1); });
  auto fu2 = executor.async([=](){ return spawn_async(N-2); });

  // use corun to avoid blocking the worker from waiting the two children tasks to finish
  executor.corun_until([&](){ 
    return fu1.wait_for(std::chrono::nanoseconds(0)) == std::future_status::ready && 
           fu2.wait_for(std::chrono::nanoseconds(0)) == std::future_status::ready; 
  });

  return fu1.get() + fu2.get();
}

size_t fibonacci_async(size_t N) {
  return get_executor().async([=](){ return spawn_async(N); }).get();
}

int main(int argc, char* argv[]) {

  if(argc != 3) {
    std::cerr << "usage: ./fibonacci N [subflow|async]\n";
    std::exit(EXIT_FAILURE);
  }

  size_t N = std::atoi(argv[1]);

  if(N < 0) {
    throw std::runtime_error("N must be non-negative");
  }

  auto tbeg = std::chrono::steady_clock::now();
  if(std::strcmp(argv[2], "subflow") == 0) {
    printf("fib[%zu] (with subflow) = %zu\n", N, fibonacci_subflow(N));
  }
  else if(std::strcmp(argv[2], "async") == 0) {
    printf("fib[%zu] (with async) = %zu\n", N, fibonacci_async(N));
  }
  else {
    std::cerr << "unrecognized method " << argv[2] << '\n';
    std::exit(EXIT_FAILURE);
  }
  auto tend = std::chrono::steady_clock::now();

  std::cout << "elapsed time: " 
            << std::chrono::duration_cast<std::chrono::milliseconds>(tend-tbeg).count()
            << " ms\n";

  return 0;
}









