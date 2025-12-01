#include "defs.h"
#include "debug.h"
#include "version.h"

#include "dfsan/dfsan.h"

extern "C" {
#include "launch.h"
}

#include "parse-z3.h"

#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

using namespace __dfsan;

#define OPTIMISTIC 1

#undef AOUT
# define AOUT(...)                                      \
  do {                                                  \
    printf(__VA_ARGS__);                                \
  } while(false)

// for input
static char *input_buf;
static size_t input_size;

// for output
static const char* __output_dir = ".";
static uint32_t __instance_id = 0;
static uint32_t __session_id = 0;
static uint32_t __current_index = 0;
static z3::context __z3_context;

// z3parser
symsan::Z3ParserSolver *__z3_parser = nullptr;

static const char *pipe_msg_type_str(uint16_t msg_type) {
  switch (msg_type) {
    case cond_type: return "cond";
    case gep_type: return "gep";
    case memcmp_type: return "memcmp";
    case fsize_type: return "fsize";
    case memerr_type: return "memerr";
    default: return "unknown";
  }
}

static std::string pipe_msg_flags_str(const pipe_msg &msg) {
  std::vector<std::string> parts;
  auto push_flag = [&parts](const char *name) {
    parts.emplace_back(name);
  };

  switch (msg.msg_type) {
    case cond_type:
      if (msg.flags & F_ADD_CONS) push_flag("add_cons");
      if (msg.flags & F_LOOP_EXIT) push_flag("loop_exit");
      if (msg.flags & F_LOOP_LATCH) push_flag("loop_latch");
      if (msg.flags & LoopFlagMask) {
        std::stringstream ss;
        ss << "loop_bits=0x" << std::hex << (msg.flags & LoopFlagMask);
        parts.push_back(ss.str());
      }
      break;
    case memerr_type:
      if (msg.flags & F_MEMERR_UAF) push_flag("uaf");
      if (msg.flags & F_MEMERR_OLB) push_flag("olb");
      if (msg.flags & F_MEMERR_OUB) push_flag("oub");
      if (msg.flags & F_MEMERR_UBI) push_flag("ubi");
      if (msg.flags & F_MEMERR_NULL) push_flag("null");
      if (msg.flags & F_MEMERR_FREE) push_flag("double_free");
      break;
    default:
      break;
  }

  if (parts.empty()) {
    std::stringstream ss;
    ss << "0x" << std::hex << msg.flags;
    return ss.str();
  }

  std::stringstream ss;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) ss << "|";
    ss << parts[i];
  }
  return ss.str();
}

static void pretty_print_pipe_msg(const pipe_msg &msg) {
  AOUT("pipe_msg { type=%s(%u), flags=%s, instance=%u, addr=%p, ctx=%u, id=%d, label=%d, result=%llu (0x%llx) }\n",
       pipe_msg_type_str(msg.msg_type), msg.msg_type,
       pipe_msg_flags_str(msg).c_str(),
       msg.instance_id, (void*)msg.addr, msg.context,
       msg.id, msg.label,
       static_cast<unsigned long long>(msg.result),
       static_cast<unsigned long long>(msg.result));
}

static void generate_input(symsan::Z3ParserSolver::solution_t &solutions) {
  char path[PATH_MAX];
  snprintf(path, PATH_MAX, "%s/id-%d-%d-%d", __output_dir,
           __instance_id, __session_id, __current_index++);
  int fd = open(path, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
  if (fd == -1) {
    AOUT("failed to open new input file for write");
    return;
  }

  if (write(fd, input_buf, input_size) == -1) {
    AOUT("failed to copy original input\n");
    close(fd);
    return;
  }
  AOUT("generate #%d output\n", __current_index - 1);

  for (auto const& sol : solutions) {
    uint8_t value = sol.val;
    AOUT("offset %d = %x\n", sol.offset, value);
    lseek(fd, sol.offset, SEEK_SET);
    write(fd, &value, sizeof(value));
  }

  close(fd);
}

static void __solve_cond(dfsan_label label, uint8_t r, bool add_nested, void *addr) {

  AOUT("solving label %d = %d, add_nested: %d\n", label, r, add_nested);
  std::vector<uint64_t> tasks;
  if (__z3_parser->parse_cond(label, r, add_nested, tasks)) {
    AOUT("WARNING: failed to parse condition %d @%p\n", label, addr);
    return;
  }

  for (auto id : tasks) {
    // solve
    symsan::Z3ParserSolver::solution_t solutions;
    auto status = __z3_parser->solve_task(id, 5000U, solutions);
    if (solutions.size() != 0) {
      AOUT("branch solved\n");
      generate_input(solutions);
    } else {
      AOUT("branch not solvable @%p\n", addr);
    }
    solutions.clear();
  }

}

static void __handle_gep(dfsan_label ptr_label, uptr ptr,
                         dfsan_label index_label, int64_t index,
                         uint64_t num_elems, uint64_t elem_size,
                         int64_t current_offset, void* addr) {

  AOUT("tainted GEP index: %ld = %d, ne: %ld, es: %ld, offset: %ld\n",
      index, index_label, num_elems, elem_size, current_offset);

  std::vector<uint64_t> tasks;
  if (__z3_parser->parse_gep(ptr_label, ptr, index_label, index, num_elems,
                             elem_size, current_offset, true, tasks)) {
    AOUT("WARNING: failed to parse gep %d @%p\n", index_label, addr);
    return;
  }

  for (auto id : tasks) {
    symsan::Z3ParserSolver::solution_t solutions;
    auto status = __z3_parser->solve_task(id, 5000U, solutions);
    if (solutions.size() != 0) {
      AOUT("gep solved\n");
      generate_input(solutions);
    } else {
      AOUT("gep not solvable @%p\n", addr);
    }
    solutions.clear();
  }
}

int main(int argc, char* const argv[]) {

  if (argc != 3) {
    fprintf(stderr, "Usage: %s target input\n", argv[0]);
    exit(1);
  }

  char *program = argv[1];
  char *input = argv[2];

  int is_stdin = 0;
  int solve_ub = 0;
  int debug = 0;
  char *options = getenv("TAINT_OPTIONS");
  if (options) {
    // setup output dir
    char *output = strstr(options, "output_dir=");
    if (output) {
      output += 11; // skip "output_dir="
      char *end = strchr(output, ':'); // try ':' first, then ' '
      if (end == NULL) end = strchr(output, ' ');
      size_t n = end == NULL? strlen(output) : (size_t)(end - output);
      __output_dir = strndup(output, n);
    }

    // check if input is stdin
    char *taint_file = strstr(options, "taint_file=");
    if (taint_file) {
      taint_file += strlen("taint_file="); // skip "taint_file="
      char *end = strchr(taint_file, ':');
      if (end == NULL) end = strchr(taint_file, ' ');
      size_t n = end == NULL? strlen(taint_file) : (size_t)(end - taint_file);
      if (n == 5 && !strncmp(taint_file, "stdin", 5))
        is_stdin = 1;
    }

    // check for debug
    char *debug_opt = strstr(options, "debug=");
    if (debug_opt) {
      debug_opt += strlen("debug="); // skip "debug="
      if (strcmp(debug_opt, "1") == 0 || strcmp(debug_opt, "true") == 0)
        debug = 1;
    }

    // check if solve_ub is enabled
    char *solve_ub_opt = strstr(options, "solve_ub=");
    if (solve_ub_opt) {
      solve_ub_opt += strlen("solve_ub="); // skip "solve_ub="
      if (strcmp(solve_ub_opt, "1") == 0 || strcmp(solve_ub_opt, "true") == 0)
        solve_ub = 1;
    }
  }

  // load input file
  struct stat st;
  int input_fd = open(input, O_RDONLY);
  if (input_fd == -1) {
    fprintf(stderr, "Failed to open input file: %s\n", strerror(errno));
    exit(1);
  }
  fstat(input_fd, &st);
  input_size = st.st_size;
  input_buf = (char *)mmap(NULL, input_size, PROT_READ, MAP_PRIVATE, input_fd, 0);
  if (input_buf == (void *)-1) {
    fprintf(stderr, "Failed to map input file: %s\n", strerror(errno));
    exit(1);
  }

  // setup launcher
  void *shm_base = symsan_init(program, uniontable_size);
  if (shm_base == (void *)-1) {
    fprintf(stderr, "Failed to map shm: %s\n", strerror(errno));
    exit(1);
  }

  if (symsan_set_input(is_stdin ? "stdin" : input) != 0) {
    fprintf(stderr, "Failed to set input\n");
    exit(1);
  }

  char* args[3];
  args[0] = program;
  args[1] = input;
  args[2] = NULL;
  if (symsan_set_args(2, args) != 0) {
    fprintf(stderr, "Failed to set args\n");
    exit(1);
  }

  symsan_set_debug(debug);
  symsan_set_bounds_check(1);
  symsan_set_solve_ub(solve_ub);

  // launch the target
  int ret = symsan_run(input_fd);
  if (ret < 0) {
    fprintf(stderr, "Failed to launch target: %s\n", strerror(errno));
    exit(1);
  } else if (ret > 0) {
    fprintf(stderr, "SymSan launch error %d\n", ret);
    exit(1);
  }
  close(input_fd);

  // setup z3 parser
  __z3_parser = new symsan::Z3ParserSolver(shm_base, uniontable_size, __z3_context);
  std::vector<symsan::input_t> inputs;
  inputs.push_back({(uint8_t*)input_buf, input_size});
  if (__z3_parser->restart(inputs) != 0) {
    fprintf(stderr, "Failed to restart parser\n");
    exit(1);
  }

  pipe_msg msg;
  gep_msg gmsg;
  size_t msg_size;
  memcmp_msg *mmsg = nullptr;

  while (symsan_read_event(&msg, sizeof(msg), 0) > 0) {
    pretty_print_pipe_msg(msg);
    // solve constraints
    switch (msg.msg_type) {
      case cond_type:
        __solve_cond(msg.label, msg.result, msg.flags & F_ADD_CONS, (void*)msg.addr);
        break;
      case gep_type:
        if (symsan_read_event(&gmsg, sizeof(gmsg), 0) != sizeof(gmsg)) {
          fprintf(stderr, "Failed to receive gep msg: %s\n", strerror(errno));
          break;
        }
        // double check
        if (msg.label != gmsg.index_label) {
          fprintf(stderr, "Incorrect gep msg: %d vs %d\n", msg.label, gmsg.index_label);
          break;
        }
        __handle_gep(gmsg.ptr_label, gmsg.ptr, gmsg.index_label, gmsg.index,
                     gmsg.num_elems, gmsg.elem_size, gmsg.current_offset, (void*)msg.addr);
        break;
      case memcmp_type:
        // flags = 0 means both operands are symbolic thus no content to read
        if (!msg.flags)
          break;
        msg_size = sizeof(memcmp_msg) + msg.result;
        mmsg = (memcmp_msg*)malloc(msg_size); // not freed until terminate
        if (symsan_read_event(mmsg, msg_size, 0) != msg_size) {
          fprintf(stderr, "Failed to receive memcmp msg: %s\n", strerror(errno));
          free(mmsg);
          break;
        }
        // double check
        if (msg.label != mmsg->label) {
          fprintf(stderr, "Incorrect memcmp msg: %d vs %d\n", msg.label, mmsg->label);
          free(mmsg);
          break;
        }
        // save the content
        __z3_parser->record_memcmp(msg.label, mmsg->content, msg.result);
        free(mmsg);
        break;
      case fsize_type:
        break;
      default:
        break;
    }
  }

  symsan_destroy();
  exit(0);
}
