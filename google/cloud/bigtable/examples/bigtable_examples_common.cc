// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "google/cloud/bigtable/examples/bigtable_examples_common.h"
#include "google/cloud/internal/getenv.h"
#include <sstream>

namespace google {
namespace cloud {
namespace bigtable {
namespace examples {

Example::Example(std::map<std::string, CommandType> commands)
    : commands_(std::move(commands)) {
  // Force each command to generate its Usage string, so we can provide a good
  // usage string for the whole program.
  for (auto const& kv : commands_) {
    if (kv.first == "auto") continue;
    try {
      kv.second({});
    } catch (Usage const& u) {
      full_usage_ += "    ";
      full_usage_ += u.what();
      full_usage_ += "\n";
    }
  }
}

int Example::Run(int argc, char const* const argv[]) try {
  bool auto_run =
      google::cloud::internal::GetEnv("GOOGLE_CLOUD_CPP_AUTO_RUN_EXAMPLES")
          .value_or("") == "yes";
  if (argc == 1 && auto_run) {
    auto entry = commands_.find("auto");
    if (entry == commands_.end()) {
      PrintUsage(argv[0], "Requested auto run but there is no 'auto' command");
      return 1;
    }
    entry->second({});
    return 0;
  }

  if (argc < 2) {
    PrintUsage(argv[0], "Missing command");
    return 1;
  }

  std::string const command_name = argv[1];
  auto command = commands_.find(command_name);
  if (commands_.end() == command) {
    PrintUsage(argv[0], "Unknown command: " + command_name);
    return 1;
  }

  command->second({argv + 2, argv + argc});

  return 0;
} catch (Usage const& u) {
  PrintUsage(argv[0], u.what());
  return 1;
} catch (std::exception const& ex) {
  std::cerr << "Standard C++ exception raised: " << ex.what() << "\n";
  return 1;
}

void Example::PrintUsage(std::string const& cmd, std::string const& msg) {
  auto last_slash = cmd.find_last_of('/');
  auto program = cmd.substr(last_slash + 1);
  std::cerr << msg << "\nUsage: " << program << " <command> [arguments]\n\n"
            << "Commands:\n"
            << full_usage_ << "\n";
}

std::string TablePrefix(std::string const& prefix,
                        std::chrono::system_clock::time_point tp) {
  auto as_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch());
  return prefix + std::to_string(as_seconds.count()) + "-";
}

std::string RandomTableId(std::string const& prefix,
                          google::cloud::internal::DefaultPRNG& generator) {
  return TablePrefix(prefix, std::chrono::system_clock::now()) +
         google::cloud::internal::Sample(generator, 8,
                                         "abcdefhijklmnopqrstuvwxyz");
}

void CleanupOldTables(std::string const& prefix,
                      google::cloud::bigtable::TableAdmin admin) {
  auto const threshold =
      std::chrono::system_clock::now() - std::chrono::hours(48);
  auto const max_table_name = TablePrefix(prefix, threshold);

  namespace cbt = google::cloud::bigtable;
  auto tables = admin.ListTables(cbt::TableAdmin::NAME_ONLY);
  if (!tables) return;
  for (auto const& t : *tables) {
    if (t.name().rfind(prefix, 0) != 0) continue;
    // Eventually (I heard from good authority around year 2286) the date
    // formatted in seconds will gain an extra digit and this will no longer
    // work. If you are a programmer from the future, I (coryan) am (a) almost
    // certainly dead, (b) very confused that this code is still being
    // maintained or used, and (c) a bit sorry that this caused you problems.
    if (t.name() >= max_table_name) continue;
    // Failure to cleanup is not an error.
    (void)admin.DeleteTable(t.name());
  }
}

bool UsingEmulator() {
  return !google::cloud::internal::GetEnv("BIGTABLE_EMULATOR_HOST")
              .value_or("")
              .empty();
}

bool RunAdminIntegrationTests() {
  // When using the emulator we can always run the admin integration tests
  if (UsingEmulator()) return true;

  // In production, we run the admin integration tests only on the nightly
  // builds to stay below the quota limits. Only this build should set the
  // following environment variable.
  return google::cloud::internal::GetEnv(
             "ENABLE_BIGTABLE_ADMIN_INTEGRATION_TESTS")
             .value_or("") == "yes";
}

void CheckEnvironmentVariablesAreSet(std::vector<std::string> const& vars) {
  for (auto const& var : vars) {
    auto const value = google::cloud::internal::GetEnv(var.c_str());
    if (!value) {
      throw std::runtime_error("The " + var +
                               " environment variable is not set");
    }
    if (value->empty()) {
      throw std::runtime_error("The " + var +
                               " environment variable has an empty value");
    }
  }
}

using TableAdminCommandType = std::function<void(
    google::cloud::bigtable::TableAdmin, std::vector<std::string>)>;

google::cloud::bigtable::examples::Commands::value_type MakeCommandEntry(
    std::string const& name, std::vector<std::string> const& args,
    TableAdminCommandType const& function) {
  auto command = [=](std::vector<std::string> argv) {
    auto constexpr kFixedArguments = 2;
    if (argv.size() != args.size() + kFixedArguments) {
      std::ostringstream os;
      os << name << " <project-id> <instance-id>";
      char const* sep = " ";
      for (auto const& a : args) {
        os << sep << a;
      }
      throw google::cloud::bigtable::examples::Usage{std::move(os).str()};
    }
    google::cloud::bigtable::TableAdmin table(
        google::cloud::bigtable::CreateDefaultAdminClient(
            argv[0], google::cloud::bigtable::ClientOptions()),
        argv[1]);
    argv.erase(argv.begin(), argv.begin() + kFixedArguments);
    function(table, argv);
  };
  return {name, command};
}

}  // namespace examples
}  // namespace bigtable
}  // namespace cloud
}  // namespace google
