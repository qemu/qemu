void log_syscall(uint64_t pc, uint64_t callno);

typedef uint64_t (*get_syscall_arg_t)(int argno, bool *error);
typedef bool (*should_log_t)(int argno);

uint64_t get_i386(int arg_no, bool *error);
uint64_t get_x86_64(int arg_no, bool *error);
uint64_t get_arm(int arg_no, bool *error);
uint64_t get_other(int arg_no, bool *error);

bool should_log_i386(int arg_no);
bool should_log_x86_64(int arg_no);
bool should_log_arm(int arg_no);
bool should_log_other(int arg_no);
