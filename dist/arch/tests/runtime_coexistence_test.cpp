// Load real runtimes into one process and check that their session state stays independent.
#include <rime_api.h>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

RimeApi* load(const char* path, const char* symbol) {
  void* handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
  if (!handle) throw std::runtime_error(dlerror());
  auto get = reinterpret_cast<RimeApi* (*)()>(dlsym(handle, symbol));
  require(get != nullptr, "missing runtime API");
  // Keep the DSO loaded until process exit: plugin worker TLS may outlive finalize.
  return get();
}

void initialize(RimeApi* api, const std::string& dir, const char* name) {
  std::filesystem::create_directories(dir + "/build");
  std::ofstream(dir + "/build/default.yaml")
      << "schema_list:\n  - schema: coexistence\n"
         "ascii_composer:\n  switch_key:\n    Shift_L: commit_code\n";
  std::ofstream(dir + "/build/coexistence.schema.yaml")
      << "schema:\n  schema_id: coexistence\n  name: Coexistence\n  version: '1'\n"
         "engine:\n  processors: [ascii_composer, express_editor]\n"
         "  segmentors: [ascii_segmentor, fallback_segmentor]\n"
         "  translators: [echo_translator]\n";
  RIME_STRUCT(RimeTraits, traits);
  traits.shared_data_dir = dir.c_str();
  traits.user_data_dir = dir.c_str();
  traits.app_name = name;
  api->setup(&traits);
  api->initialize(&traits);
}

void checkDirectory(RimeApi* api, const std::string& expected) {
  require(RIME_API_AVAILABLE(api, get_user_data_dir_s), "runtime API is too old");
  char value[4096] = {};
  api->get_user_data_dir_s(value, sizeof(value));
  require(value == expected, "runtime user directory was replaced by the other engine");
}

void checkProperty(RimeApi* api, RimeSessionId session, const char* expected) {
  char value[128] = {};
  require(api->get_property(session, "coexistence", value, sizeof(value)),
          "session property disappeared");
  require(std::string(value) == expected, "session property leaked across runtimes");
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 5) {
    std::cerr << "usage: runtime_test UPSTREAM_SO BANGKE_SO NEW_WORK_DIR upstream-first|bangke-first\n";
    return 2;
  }
  try {
    const std::filesystem::path root(argv[3]);
    require(std::filesystem::create_directory(root), "work directory must not already exist");
    const bool upstreamFirst = std::string(argv[4]) == "upstream-first";
    require(upstreamFirst || std::string(argv[4]) == "bangke-first", "invalid load order");
    RimeApi* upstream;
    RimeApi* bangke;
    const auto upstreamDir = (root / "upstream").string();
    const auto bangkeDir = (root / "bangke").string();
    if (upstreamFirst) {
      upstream = load(argv[1], "rime_get_api");
      initialize(upstream, upstreamDir, "rime.coexist-upstream");
      bangke = load(argv[2], "bangke_rime_get_api");
      initialize(bangke, bangkeDir, "rime.coexist-bangke");
    } else {
      bangke = load(argv[2], "bangke_rime_get_api");
      initialize(bangke, bangkeDir, "rime.coexist-bangke");
      upstream = load(argv[1], "rime_get_api");
      initialize(upstream, upstreamDir, "rime.coexist-upstream");
    }
    require(upstream != bangke, "both addons resolved to the same API instance");
    checkDirectory(upstream, upstreamDir);
    checkDirectory(bangke, bangkeDir);
    const auto first = upstream->create_session();
    const auto second = bangke->create_session();
    require(first && second, "failed to create independent sessions");
    upstream->set_property(first, "coexistence", "upstream");
    bangke->set_property(second, "coexistence", "bangke");
    checkProperty(upstream, first, "upstream");
    checkProperty(bangke, second, "bangke");
    bangke->destroy_session(second);
    bangke->finalize();
    checkProperty(upstream, first, "upstream");
    upstream->destroy_session(first);
    upstream->finalize();
    std::cout << "Independent runtime state: OK\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
