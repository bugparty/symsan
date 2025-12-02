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
#include <algorithm>
#include <deque>
#include <fstream>

#include <nlohmann/json.hpp>

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
static char* __output_dir = nullptr;
static bool __output_dir_allocated = false;
static uint32_t __instance_id = 0;
static uint32_t __session_id = 0;
static uint32_t __current_index = 0;

static const char* get_output_dir() {
  return __output_dir ? __output_dir : ".";
}
static z3::context __z3_context;
static size_t max_seeds = 64;

// z3parser
symsan::Z3ParserSolver *__z3_parser = nullptr;

struct Seed {
  std::vector<uint8_t> data;
};

static std::deque<Seed> seed_queue;
static size_t seeds_processed = 0;
static Seed* current_seed = nullptr;

struct BranchMeta {
  int line;
  int symSanId;
};

struct ObservedCond {
  int symSanId;
  dfsan_label label;
  bool result;
};

struct ModelStep {
  int line;
  bool is_true;
};

struct ModelTrace {
  enum Answer {
    Reachable,
    Unreachable,
    Unknown
  };
  Answer answer;
  std::vector<ModelStep> steps;
};

struct StepMetrics {
  double precision = 0.0;
  double recall = 0.0;
  double f1 = 0.0;
};

struct RewardRow {
  double reward = 0.0;
  bool solver_sat = false;
  bool solver_unknown = false;
  StepMetrics metrics;
  ModelTrace::Answer answer = ModelTrace::Unknown;
  size_t provided_steps = 0;
};

// reward mode inputs
static bool reward_mode = false;
static std::string branch_meta_path;
static std::string traces_path;
static std::string reward_output_path;

// cached metadata/runtime info
static std::unordered_map<int, BranchMeta> line_to_branch;
static std::unordered_map<int, dfsan_label> symSanId_to_label;
static std::vector<ObservedCond> observed_conds;
static size_t branch_count_meta = 0;
static std::unordered_map<int, int> symSanId_to_line;

struct GTStep {
  int line;
  bool is_true;
};

struct GTBranchStats {
  uint32_t seen_true = 0;
  uint32_t seen_false = 0;
};

static bool target_reached = false;
static std::unordered_map<int, GTBranchStats> gt_branch_stats;
static uint32_t target_runs = 0;
static std::vector<GTStep> ground_truth_path;

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
      if (msg.flags & F_TARGET_HIT) push_flag("target_hit");
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

static ModelTrace::Answer parse_answer(const std::string &s) {
  if (s == "reachable") return ModelTrace::Reachable;
  if (s == "unreachable") return ModelTrace::Unreachable;
  return ModelTrace::Unknown;
}

static const char* answer_to_str(ModelTrace::Answer a) {
  switch (a) {
    case ModelTrace::Reachable: return "reachable";
    case ModelTrace::Unreachable: return "unreachable";
    default: return "unknown";
  }
}

static bool load_branch_metadata(const std::string &path) {
  std::ifstream in(path);
  if (!in) return false;
  nlohmann::json j;
  in >> j;
  if (!j.contains("branches")) return false;
  branch_count_meta = j["branches"].size();
  for (auto &b : j["branches"]) {
    if (!b.contains("line") || !b.contains("symSanId")) continue;
    BranchMeta bm;
    bm.line = b["line"];
    bm.symSanId = b["symSanId"];
    line_to_branch[bm.line] = bm;
    symSanId_to_line[bm.symSanId] = bm.line;
  }
  return true;
}

static bool parse_model_traces(const std::string &path,
                               int &target_line,
                               std::vector<ModelTrace> &traces) {
  std::ifstream in(path);
  if (!in) return false;
  nlohmann::json j;
  in >> j;
  if (j.contains("target") && j["target"].contains("line")) {
    target_line = j["target"]["line"];
  } else {
    target_line = 0;
  }

  if (!j.contains("traces")) return false;
  for (auto &t : j["traces"]) {
    ModelTrace mt;
    std::string ans = t.value("answer", "unknown");
    mt.answer = parse_answer(ans);
    for (auto &s : t["steps"]) {
      ModelStep st;
      st.line = s.value("line", 0);
      std::string dir = s.value("dir", "F");
      st.is_true = (dir == "T" || dir == "t" || dir == "true" || dir == "1");
      mt.steps.push_back(st);
    }
    traces.push_back(std::move(mt));
  }
  return true;
}

static std::vector<symsan::trace_cond>
build_model_conds(const ModelTrace &mt) {
  std::vector<symsan::trace_cond> out;
  std::unordered_set<int> seen;
  out.reserve(mt.steps.size());
  for (auto &s : mt.steps) {
    auto it = line_to_branch.find(s.line);
    if (it == line_to_branch.end()) continue;
    int symId = it->second.symSanId;
    if (!seen.insert(symId).second) continue;
    auto itL = symSanId_to_label.find(symId);
    if (itL == symSanId_to_label.end()) continue;
    symsan::trace_cond tc;
    tc.label = itL->second;
    tc.is_true = s.is_true;
    out.push_back(tc);
  }
  return out;
}

static StepMetrics compute_step_metrics(size_t provided, size_t expected, bool solver_sat) {
  StepMetrics m;
  if (provided == 0 || expected == 0) return m;
  m.precision = solver_sat ? 1.0 : 0.0;
  m.recall = static_cast<double>(provided) / static_cast<double>(expected);
  if (m.precision + m.recall > 0.0) {
    m.f1 = 2.0 * m.precision * m.recall / (m.precision + m.recall);
  }
  return m;
}

static StepMetrics compute_step_metrics_vs_gt(const ModelTrace &mt) {
  StepMetrics m;
  if (!target_reached || ground_truth_path.empty() || mt.steps.empty())
    return m;

  std::unordered_map<int, bool> gt_map;
  for (auto &s : ground_truth_path) {
    gt_map[s.line] = s.is_true;
  }

  size_t gt_total = gt_map.size();
  size_t provided = mt.steps.size();
  size_t correct = 0;
  size_t wrong = 0;

  for (auto &s : mt.steps) {
    auto it = gt_map.find(s.line);
    if (it == gt_map.end()) {
      wrong++;
      continue;
    }
    if (it->second == s.is_true) correct++;
    else wrong++;
  }

  if (gt_total == 0 || provided == 0) return m;

  m.precision = static_cast<double>(correct) / static_cast<double>(provided);
  m.recall = static_cast<double>(correct) / static_cast<double>(gt_total);
  if (m.precision + m.recall > 0.0) {
    m.f1 = 2.0 * m.precision * m.recall / (m.precision + m.recall);
  }
  return m;
}

static double compute_reward(const ModelTrace &mt, bool sat, bool unknown,
                             const StepMetrics &m) {
  if (unknown) return -0.1; // timeout/unknown solver status

  double status_score = 0.0;
  if (target_reached) {
    if (mt.answer == ModelTrace::Reachable) status_score = 1.0;
    else if (mt.answer == ModelTrace::Unreachable) status_score = -1.0;
  } else {
    if (mt.answer == ModelTrace::Unreachable) status_score = 1.0;
    else if (mt.answer == ModelTrace::Reachable) status_score = -1.0;
  }

  double sat_score = 0.0;
  if (mt.answer == ModelTrace::Reachable) {
    sat_score = sat ? 0.5 : -0.5;
  }

  double path_score = m.f1; // already in [0,1]

  double reward = 0.6 * status_score + 0.2 * sat_score + 0.2 * path_score;
  if (!mt.steps.empty()) reward += 0.05; // small format bonus
  return reward;
}

static void write_rewards(const std::string &path,
                          const std::vector<RewardRow> &rows) {
  nlohmann::json out;
  out["rewards"] = nlohmann::json::array();
  for (auto &r : rows) {
    nlohmann::json entry;
    entry["reward"] = r.reward;
    entry["answer"] = answer_to_str(r.answer);
    entry["solver_sat"] = r.solver_sat;
    entry["solver_unknown"] = r.solver_unknown;
    entry["precision"] = r.metrics.precision;
    entry["recall"] = r.metrics.recall;
    entry["f1"] = r.metrics.f1;
    entry["provided_steps"] = r.provided_steps;
    out["rewards"].push_back(entry);
  }
  std::ofstream ofs(path);
  ofs << out.dump(2) << "\n";
}

static void generate_input(symsan::Z3ParserSolver::solution_t &solutions) {
  char path[PATH_MAX];
  snprintf(path, PATH_MAX, "%s/id-%d-%d-%d", get_output_dir(),
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

  // enqueue new seed in memory if budget allows
  if (seeds_processed + seed_queue.size() >= max_seeds)
    return;
  if (!current_seed)
    return;
  Seed new_seed;
  new_seed.data = current_seed->data;
  for (auto const& sol : solutions) {
    if (sol.offset < new_seed.data.size()) {
      new_seed.data[sol.offset] = sol.val;
    }
  }
  seed_queue.push_back(std::move(new_seed));
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

static std::vector<RewardRow>
evaluate_model_traces(const std::vector<ModelTrace> &traces) {
  std::vector<RewardRow> rows;
  rows.reserve(traces.size());

  for (auto &t : traces) {
    RewardRow row;
    row.answer = t.answer;

    auto conds = build_model_conds(t);
    row.provided_steps = conds.size();

    uint64_t task_id = 0;
    // When evaluating model traces, avoid nested deps recorded from the last concrete run.
    bool build_ok = (__z3_parser->build_trace_task(conds, /*add_nested=*/false, task_id) == 0);
    if (!build_ok) {
      row.solver_unknown = true;
      row.reward = compute_reward(t, false, true, row.metrics);
      rows.push_back(row);
      continue;
    }

    symsan::Z3ParserSolver::solution_t solutions;
    auto status = __z3_parser->solve_task(task_id, 5000U, solutions);

    if (status == symsan::Z3ParserSolver::opt_timeout ||
        status == symsan::Z3ParserSolver::opt_sat_nested_timeout) {
      row.solver_unknown = true;
    } else if (status == symsan::Z3ParserSolver::opt_unsat ||
               status == symsan::Z3ParserSolver::opt_sat_nested_unsat) {
      row.solver_sat = false;
    } else {
      row.solver_sat = !solutions.empty();
    }
    solutions.clear();

    if (target_reached && !ground_truth_path.empty()) {
      row.metrics = compute_step_metrics_vs_gt(t);
    } else {
      row.metrics = compute_step_metrics(row.provided_steps, branch_count_meta, row.solver_sat);
    }
    row.reward = compute_reward(t, row.solver_sat, row.solver_unknown, row.metrics);
    rows.push_back(row);
  }

  return rows;
}

int main(int argc, char* const argv[]) {

  if (argc != 6) {
    fprintf(stderr, "Usage: %s target input branch_meta.json traces.json rewards_out.json\n", argv[0]);
    fprintf(stderr, "\n");
    fprintf(stderr, "Parameters:\n");
    fprintf(stderr, "  target          - Path to the instrumented target program to test\n");
    fprintf(stderr, "  input           - Path to the initial seed input file\n");
    fprintf(stderr, "  branch_meta.json - JSON file containing branch metadata (line -> symSanId mapping)\n");
    fprintf(stderr, "                     Format: {\"branches\": [{\"line\": N, \"symSanId\": M}, ...]}\n");
    fprintf(stderr, "  traces.json     - JSON file containing model traces to evaluate\n");
    fprintf(stderr, "                     Format: {\"target\": {\"line\": N}, \"traces\": [...]}\n");
    fprintf(stderr, "                     Each trace has: {\"answer\": \"reachable\"|\"unreachable\", \"steps\": [...]}\n");
    fprintf(stderr, "  rewards_out.json - Output JSON file to write reward scores for each trace\n");
    fprintf(stderr, "\n");
    exit(1);
  }

  reward_mode = true;
  // Reward mode runs exploration first (nested constraints enabled) then scores hypothetical
  // model traces with nested constraints disabled to avoid reusing stale branch dependencies.
  branch_meta_path = argv[3];
  traces_path = argv[4];
  reward_output_path = argv[5];
  if (!load_branch_metadata(branch_meta_path)) {
    fprintf(stderr, "Failed to load branch metadata from %s\n", branch_meta_path.c_str());
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
      // Free previously allocated output_dir if any
      if (__output_dir_allocated && __output_dir) {
        free(__output_dir);
        __output_dir = nullptr;
        __output_dir_allocated = false;
      }
      char *new_dir = strndup(output, n);
      if (new_dir) {
        __output_dir = new_dir;
        __output_dir_allocated = true;
      } else {
        fprintf(stderr, "Warning: Failed to allocate memory for output_dir, using default\n");
      }
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

  // load initial seed into queue
  struct stat st;
  int input_fd = open(input, O_RDONLY);
  if (input_fd == -1) {
    fprintf(stderr, "Failed to open input file: %s\n", strerror(errno));
    exit(1);
  }
  fstat(input_fd, &st);
  Seed s0;
  s0.data.resize(st.st_size);
  if (read(input_fd, s0.data.data(), st.st_size) != st.st_size) {
    fprintf(stderr, "Failed to read seed input: %s\n", strerror(errno));
    exit(1);
  }
  close(input_fd);
  seed_queue.push_back(std::move(s0));

  // setup launcher
  void *shm_base = symsan_init(program, uniontable_size);
  if (shm_base == (void *)-1) {
    fprintf(stderr, "Failed to map shm: %s\n", strerror(errno));
    exit(1);
  }

  symsan_set_debug(debug);
  symsan_set_bounds_check(1);
  symsan_set_solve_ub(solve_ub);
  // exploration loop over queued seeds
  while (!seed_queue.empty() && seeds_processed < max_seeds) {
    Seed seed = std::move(seed_queue.front());
    seed_queue.pop_front();
    ++seeds_processed;
    std::vector<ObservedCond> run_conds;
    bool run_target_hit = false;

    // write seed to temp file
    char tmp_path[PATH_MAX];
    snprintf(tmp_path, PATH_MAX, "%s/.fgtest-tmp-%u", get_output_dir(), __current_index++);
    int fd = open(tmp_path, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd == -1) {
      fprintf(stderr, "Failed to create temp seed file: %s\n", strerror(errno));
      continue;
    }
    if (!seed.data.empty()) {
      if (write(fd, seed.data.data(), seed.data.size()) != (ssize_t)seed.data.size()) {
        fprintf(stderr, "Failed to write seed file: %s\n", strerror(errno));
        close(fd);
        continue;
      }
    }
    lseek(fd, 0, SEEK_SET);
    fstat(fd, &st);
    input_size = st.st_size;
    input_buf = (char *)mmap(NULL, input_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (input_buf == (void *)-1) {
      fprintf(stderr, "Failed to map seed file: %s\n", strerror(errno));
      close(fd);
      continue;
    }

    if (symsan_set_input(is_stdin ? "stdin" : tmp_path) != 0) {
      fprintf(stderr, "Failed to set input\n");
      munmap(input_buf, input_size);
      close(fd);
      exit(1);
    }

    char* args[3];
    args[0] = program;
    args[1] = tmp_path;
    args[2] = NULL;
    if (symsan_set_args(2, args) != 0) {
      fprintf(stderr, "Failed to set args\n");
      munmap(input_buf, input_size);
      close(fd);
      continue;
    }

    int ret = symsan_run(fd);
    if (ret < 0) {
      fprintf(stderr, "Failed to launch target: %s\n", strerror(errno));
      munmap(input_buf, input_size);
      close(fd);
      continue;
    } else if (ret > 0) {
      fprintf(stderr, "SymSan launch error %d\n", ret);
      munmap(input_buf, input_size);
      close(fd);
      continue;
    }

    if (!__z3_parser) {
      __z3_parser = new symsan::Z3ParserSolver(shm_base, uniontable_size, __z3_context);
    }
    std::vector<symsan::input_t> inputs;
    inputs.push_back({(uint8_t*)input_buf, input_size});
    if (__z3_parser->restart(inputs) != 0) {
      fprintf(stderr, "Failed to restart parser\n");
      munmap(input_buf, input_size);
      close(fd);
      continue;
    }

    current_seed = &seed;

    pipe_msg msg;
    gep_msg gmsg;
    size_t msg_size;
    memcmp_msg *mmsg = nullptr;

    while (symsan_read_event(&msg, sizeof(msg), 0) > 0) {
      pretty_print_pipe_msg(msg);
      switch (msg.msg_type) {
        case cond_type:
          symSanId_to_label[msg.id] = msg.label;
          observed_conds.push_back({static_cast<int>(msg.id), msg.label, msg.result != 0});
          run_conds.push_back({static_cast<int>(msg.id), msg.label, msg.result != 0});
          __solve_cond(msg.label, msg.result, msg.flags & F_ADD_CONS, (void*)msg.addr);
          break;
        case gep_type:
          if (symsan_read_event(&gmsg, sizeof(gmsg), 0) != sizeof(gmsg)) {
            fprintf(stderr, "Failed to receive gep msg: %s\n", strerror(errno));
            break;
          }
          if (msg.label != gmsg.index_label) {
            fprintf(stderr, "Incorrect gep msg: %d vs %d\n", msg.label, gmsg.index_label);
            break;
          }
          __handle_gep(gmsg.ptr_label, gmsg.ptr, gmsg.index_label, gmsg.index,
                       gmsg.num_elems, gmsg.elem_size, gmsg.current_offset, (void*)msg.addr);
          break;
        case memcmp_type:
          if (!msg.flags)
            break;
          msg_size = sizeof(memcmp_msg) + msg.result;
          mmsg = (memcmp_msg*)malloc(msg_size); // not freed until terminate
          if (symsan_read_event(mmsg, msg_size, 0) != msg_size) {
            fprintf(stderr, "Failed to receive memcmp msg: %s\n", strerror(errno));
            free(mmsg);
            break;
          }
          if (msg.label != mmsg->label) {
            fprintf(stderr, "Incorrect memcmp msg: %d vs %d\n", msg.label, mmsg->label);
            free(mmsg);
            break;
          }
          __z3_parser->record_memcmp(msg.label, mmsg->content, msg.result);
          free(mmsg);
          break;
        case memerr_type:
          if (msg.flags & F_TARGET_HIT) {
            run_target_hit = true;
          }
          break;
        case fsize_type:
          break;
        default:
          break;
      }
    }

    if (run_target_hit) {
      target_reached = true;
      ++target_runs;

      std::unordered_map<int, bool> run_line_dir;
      run_line_dir.reserve(run_conds.size());

      for (auto &rc : run_conds) {
        auto itLine = symSanId_to_line.find(rc.symSanId);
        if (itLine == symSanId_to_line.end()) continue;
        int line = itLine->second;
        run_line_dir[line] = rc.result;
      }

      for (auto &kv : run_line_dir) {
        auto &st = gt_branch_stats[kv.first];
        if (kv.second) st.seen_true++;
        else st.seen_false++;
      }
    }

    munmap(input_buf, input_size);
    input_buf = nullptr;
    input_size = 0;
    close(fd);
    current_seed = nullptr;
  }

  // Consolidate ground truth path from all target-reaching runs
  ground_truth_path.clear();
  if (target_reached && target_runs > 0) {
    ground_truth_path.reserve(gt_branch_stats.size());
    for (auto &kv : gt_branch_stats) {
      int line = kv.first;
      const auto &st = kv.second;
      if (st.seen_true > 0 && st.seen_false == 0) {
        ground_truth_path.push_back({line, true});
      } else if (st.seen_false > 0 && st.seen_true == 0) {
        ground_truth_path.push_back({line, false});
      }
    }
    std::sort(ground_truth_path.begin(), ground_truth_path.end(),
              [](const GTStep &a, const GTStep &b) { return a.line < b.line; });
  }

  if (reward_mode) {
    if (__z3_parser) {
      __z3_parser->set_strict_value_filtering(false);
    }
    int target_line = 0;
    std::vector<ModelTrace> traces;
    if (!parse_model_traces(traces_path, target_line, traces)) {
      fprintf(stderr, "Failed to parse traces from %s\n", traces_path.c_str());
      exit(1);
    }
    auto rows = evaluate_model_traces(traces);
    write_rewards(reward_output_path, rows);
  }

  symsan_destroy();
  
  // Clean up allocated output_dir
  if (__output_dir_allocated && __output_dir) {
    free(__output_dir);
    __output_dir = nullptr;
    __output_dir_allocated = false;
  }
  
  exit(0);
}
