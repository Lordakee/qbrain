#include "qbrain/cli/app.hpp"
#include "qbrain/core/brain.hpp"
#include "qbrain/ops/registry.hpp"
#include "qbrain/ingest/import.hpp"
#include "qbrain/ai/embed.hpp"
#include "qbrain/cycle/dream.hpp"
#include "qbrain/jobs/minions.hpp"
#include "qbrain/mcp/server.hpp"
#include "qbrain/service/inbox_watch.hpp"
#include "qbrain/service/live_sync.hpp"
#include "qbrain/util/paths.hpp"
#include "qbrain/util/string_util.hpp"
#include "qbrain/util/log.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <functional>
#include <algorithm>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#define Sleep(ms) usleep((ms) * 1000)
#endif

namespace qbrain::cli {
namespace {

std::string brain_id_from_args(const std::vector<std::string>& args) {
  for (size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == "--brain") return args[i + 1];
  }
  auto c = load_file_config();
  return c.brain_id.empty() ? "default" : c.brain_id;
}

bool flag(const std::vector<std::string>& args, const std::string& name) {
  for (auto& a : args)
    if (a == name) return true;
  return false;
}

std::string opt(const std::vector<std::string>& args, const std::string& name,
                const std::string& def = {}) {
  for (size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == name) return args[i + 1];
  }
  return def;
}

std::string join_positional(const std::vector<std::string>& args,
                            const std::vector<std::string>& skip_flags) {
  std::string out;
  for (size_t i = 0; i < args.size(); ++i) {
    bool is_flag = false;
    for (auto& f : skip_flags) {
      if (args[i] == f) {
        is_flag = true;
        if (f.rfind("--", 0) == 0 && f != "--json" && f != "--save" && f != "--stdin" &&
            f != "--all" && f != "--help" && f != "--no-vector" && f != "--rerank" &&
            f != "--rerank-llm" && f != "--apply" && f != "--once" && f != "--watch")
          ++i;
        break;
      }
    }
    if (is_flag) continue;
    if (args[i].rfind("--", 0) == 0) continue;
    if (!out.empty()) out.push_back(' ');
    out += args[i];
  }
  return util::trim(out);
}

void print_help() {
  std::cout <<
      "Qbrain — Windows-native personal knowledge brain (C++)\n\n"
      "Usage:\n"
      "  qbrain <command> [args]\n\n"
      "Commands:\n"
      "  init [--brain id]              Create local brain (SQLite, no WSL)\n"
      "  doctor [--remediate] [--json]   Health check; --remediate auto-fixes\n"
      "  config get|set <key> [value]   Configuration\n"
      "  put --slug s --title t [--file f] [--type note]\n"
      "  get <slug>\n"
      "  list [--limit N] [--type t]\n"
      "  capture \"text\" | --file f | --stdin\n"
      "  import <path>\n"
      "  search \"query\" [--limit N] [--json] [--no-vector] [--mode m] [--rerank]\n"
      "  think \"question\" [--json] [--save]\n"
      "  graph <slug> [--depth N]\n"
      "  delete <slug> [--source default]\n"
      "  embed --all | --slug s | --drain   (drain: process embed jobs queue)\n"
      "  serve [--brain id] [--allow-write] [--http] [--port N]\n"
      "  inbox [--watch]                     process %LOCALAPPDATA%\\Qbrain\\inbox\n"
      "  sync <notes-dir> [--watch] [--once]  live-sync notes (mtime state)\n"
      "  worker [--once]                     claim/complete minion jobs + inbox\n"
      "  dream [--apply] [--phase p] [--json]  multi-phase cycle\n"
      "  version\n"
      "  help\n\n"
      "MCP:\n"
      "  claude mcp add qbrain -- qbrain serve\n"
      "  claude mcp add qbrain -- qbrain serve --allow-write\n"
      "  HTTP: set QBRAIN_MCP_TOKEN then qbrain serve --http --port 7420\n\n"
      "Data: %LOCALAPPDATA%\\Qbrain\\\n";
}

int cmd_init(const std::vector<std::string>& args) {
  auto id = brain_id_from_args(args);
  util::ensure_dir(util::qbrain_root());
  util::ensure_dir(util::brain_dir(id));
  util::ensure_dir(util::audit_dir());
  Brain b(id);
  b.open();
  auto cfg = load_file_config();
  cfg.brain_id = id;
  save_file_config(cfg);
  std::cout << "Initialized brain '" << id << "' at "
            << util::path_to_utf8(util::brain_db_path(id)) << "\n";
  return 0;
}

int with_brain(const std::vector<std::string>& args,
               const std::function<int(Brain&)>& fn) {
  Brain b(brain_id_from_args(args));
  try {
    b.open();
  } catch (const std::exception& e) {
    std::cerr << "open brain failed: " << e.what() << "\nRun: qbrain init\n";
    return 2;
  }
  return fn(b);
}

int cmd_doctor(const std::vector<std::string>& args) {
  return with_brain(args, [&](Brain& b) {
    ops::OpContext ctx;
    ctx.brain = &b;
    ctx.remote = false;
    ctx.allow_write = true;
    if (flag(args, "--remediate")) {
      auto r = ops::global_registry().call("doctor_remediate", ctx);
      if (flag(args, "--json"))
        std::cout << r.json << "\n";
      else
        std::cout << r.text;
      if (!r.ok) return 1;
      auto h = ops::global_registry().call("get_health", ctx);
      if (flag(args, "--json"))
        std::cout << h.json << "\n";
      else
        std::cout << h.text;
      return h.ok ? 0 : 1;
    }
    auto r = ops::global_registry().call("get_health", ctx);
    if (flag(args, "--json"))
      std::cout << r.json << "\n";
    else
      std::cout << r.text;
    return r.ok ? 0 : 1;
  });
}

int cmd_config(const std::vector<std::string>& args) {
  if (args.size() < 2) {
    std::cerr << "usage: qbrain config get|set <key> [value]\n";
    return 1;
  }
  return with_brain(args, [&](Brain& b) {
    // args = [get|set, key, value?]
    if (args[0] == "get") {
      if (args.size() < 2) return 1;
      const auto& key = args[1];
      auto v = b.get_config_value(key);
      if (!v) {
        auto c = b.config();
        if (key == "embedding.model") std::cout << c.embedding_model << "\n";
        else if (key == "embedding.base_url") std::cout << c.embedding_base_url << "\n";
        else if (key == "embedding.dimensions") std::cout << c.embedding_dimensions << "\n";
        else if (key == "chat.model") std::cout << c.chat_model << "\n";
        else if (key == "chat.base_url") std::cout << c.chat_base_url << "\n";
        else if (key == "embedding.api_key" || key == "chat.api_key") {
          // never print secrets; only presence
          std::cout << "(not in db; check env OPENAI_API_KEY / QBRAIN_API_KEY)\n";
          return 0;
        } else {
          std::cerr << "not set\n";
          return 1;
        }
        return 0;
      }
      if (key.find("api_key") != std::string::npos) {
        std::cout << "(set, " << v->size() << " chars)\n";
        return 0;
      }
      std::cout << *v << "\n";
      return 0;
    }
    if (args[0] == "set") {
      if (args.size() < 3) return 1;
      b.save_config_value(args[1], args[2]);
      std::cout << "set " << args[1] << "\n";
      return 0;
    }
    return 1;
  });
}

int cmd_put(const std::vector<std::string>& args) {
  return with_brain(args, [&](Brain& b) {
    ops::OpContext ctx;
    ctx.brain = &b;
    ctx.args["slug"] = opt(args, "--slug");
    ctx.args["title"] = opt(args, "--title", ctx.args["slug"]);
    ctx.args["type"] = opt(args, "--type", "note");
    auto file = opt(args, "--file");
    if (!file.empty()) {
      std::ifstream in(util::utf8_to_path(file), std::ios::binary);
      std::ostringstream ss;
      ss << in.rdbuf();
      ctx.args["body"] = ss.str();
    } else {
      ctx.args["body"] = opt(args, "--body");
    }
    auto r = ops::global_registry().call("put_page", ctx);
    std::cout << (flag(args, "--json") ? r.json : r.text) << "\n";
    return r.ok ? 0 : r.exit_code;
  });
}

int cmd_get(const std::vector<std::string>& args) {
  return with_brain(args, [&](Brain& b) {
    ops::OpContext ctx;
    ctx.brain = &b;
    ctx.args["slug"] = join_positional(args, {"--brain", "--json", "--source"});
    auto r = ops::global_registry().call("get_page", ctx);
    std::cout << (flag(args, "--json") ? r.json : r.text);
    return r.ok ? 0 : r.exit_code;
  });
}

int cmd_list(const std::vector<std::string>& args) {
  return with_brain(args, [&](Brain& b) {
    ops::OpContext ctx;
    ctx.brain = &b;
    ctx.args["limit"] = opt(args, "--limit", "50");
    ctx.args["type"] = opt(args, "--type");
    auto r = ops::global_registry().call("list_pages", ctx);
    std::cout << (flag(args, "--json") ? r.json : r.text);
    return 0;
  });
}

int cmd_capture(const std::vector<std::string>& args) {
  return with_brain(args, [&](Brain& b) {
    std::string text;
    if (flag(args, "--stdin")) {
      std::ostringstream ss;
      ss << std::cin.rdbuf();
      text = ss.str();
    } else if (!opt(args, "--file").empty()) {
      std::ifstream in(util::utf8_to_path(opt(args, "--file")), std::ios::binary);
      std::ostringstream ss;
      ss << in.rdbuf();
      text = ss.str();
    } else {
      text = join_positional(args, {"--brain", "--file", "--stdin", "--type", "--json"});
    }
    ops::OpContext ctx;
    ctx.brain = &b;
    ctx.args["text"] = text;
    ctx.args["type"] = opt(args, "--type", "note");
    auto r = ops::global_registry().call("capture", ctx);
    std::cout << (flag(args, "--json") ? r.json : r.text) << "\n";
    return r.ok ? 0 : r.exit_code;
  });
}

int cmd_import(const std::vector<std::string>& args) {
  return with_brain(args, [&](Brain& b) {
    auto path = join_positional(args, {"--brain", "--json"});
    if (path.empty()) {
      std::cerr << "usage: qbrain import <path>\n";
      return 1;
    }
    auto r = ingest::import_path(b, path);
    std::cout << "import files=" << r.files << " pages=" << r.pages << " link_refs=" << r.links
              << " errors=" << r.errors << "\n";
    return r.errors ? 1 : 0;
  });
}

int cmd_search(const std::vector<std::string>& args) {
  return with_brain(args, [&](Brain& b) {
    ops::OpContext ctx;
    ctx.brain = &b;
    ctx.args["query"] =
        join_positional(args, {"--brain", "--limit", "--json", "--no-vector", "--mode", "--rerank",
                               "--rerank-llm"});
    ctx.args["limit"] = opt(args, "--limit", std::to_string(b.config().search_default_limit));
    if (flag(args, "--no-vector")) ctx.args["no_vector"] = "1";
    auto mode = opt(args, "--mode");
    if (!mode.empty()) ctx.args["mode"] = mode;
    if (flag(args, "--rerank")) ctx.args["rerank"] = "1";
    if (flag(args, "--rerank-llm")) ctx.args["rerank_llm"] = "1";
    auto r = ops::global_registry().call("search", ctx);
    std::cout << (flag(args, "--json") ? r.json : r.text);
    return r.ok ? 0 : r.exit_code;
  });
}

int cmd_think(const std::vector<std::string>& args) {
  return with_brain(args, [&](Brain& b) {
    ops::OpContext ctx;
    ctx.brain = &b;
    ctx.args["question"] = join_positional(args, {"--brain", "--json", "--save", "--limit"});
    ctx.args["limit"] = opt(args, "--limit", "8");
    if (flag(args, "--save")) ctx.args["save"] = "1";
    auto r = ops::global_registry().call("think", ctx);
    std::cout << (flag(args, "--json") ? r.json : r.text);
    return r.ok ? 0 : r.exit_code;
  });
}

int cmd_graph(const std::vector<std::string>& args) {
  return with_brain(args, [&](Brain& b) {
    ops::OpContext ctx;
    ctx.brain = &b;
    ctx.args["slug"] = join_positional(args, {"--brain", "--depth", "--json"});
    ctx.args["depth"] = opt(args, "--depth", "1");
    auto r = ops::global_registry().call("get_links", ctx);
    std::cout << (flag(args, "--json") ? r.json : r.text);
    return 0;
  });
}

int cmd_serve(const std::vector<std::string>& args) {
  mcp::ServeOptions opts;
  opts.brain_id = brain_id_from_args(args);
  opts.allow_write = flag(args, "--allow-write");
  if (const char* e = std::getenv("QBRAIN_MCP_ALLOW_WRITE")) {
    if (std::string(e) == "1" || std::string(e) == "true") opts.allow_write = true;
  }
  Brain b(opts.brain_id);
  try {
    b.open();
  } catch (const std::exception& e) {
    std::cerr << "open brain failed: " << e.what() << "\nRun: qbrain init\n";
    return 2;
  }
  if (flag(args, "--http")) {
    const char* tok = std::getenv("QBRAIN_MCP_TOKEN");
    if (!tok || !*tok) {
      std::cerr << "QBRAIN_MCP_TOKEN env required for --http (do not pass token on argv)\n";
      return 2;
    }
    int port = 7420;
    auto ps = opt(args, "--port");
    if (!ps.empty()) port = std::stoi(ps);
    return mcp::run_http_server(b, opts, tok, port);
  }
  return mcp::run_stdio_server(b, opts);
}

int cmd_inbox(const std::vector<std::string>& args) {
  return with_brain(args, [&](Brain& b) {
    if (flag(args, "--watch")) {
      std::cout << "watching " << qbrain::util::path_to_utf8(service::inbox_dir()) << " (Ctrl+C to stop)\n";
      for (;;) {
        int n = service::watch_inbox_once(b);
        if (n) {
          b.drain_embed_jobs(50);
          std::cout << "imported pages~=" << n << "\n";
        }
        Sleep(2000);
      }
    }
    int n = service::watch_inbox_once(b);
    b.drain_embed_jobs(50);
    std::cout << "inbox processed, pages=" << n << "\n";
    return 0;
  });
}

int cmd_sync(const std::vector<std::string>& args) {
  return with_brain(args, [&](Brain& b) {
    auto path = join_positional(args, {"--brain", "--once", "--watch", "--interval", "--source"});
    if (path.empty()) {
      std::cerr << "usage: qbrain sync <notes-dir> [--watch] [--once] [--interval ms]\n";
      return 1;
    }
    auto source = opt(args, "--source", "default");
    int interval = 2000;
    try {
      if (!opt(args, "--interval").empty()) interval = std::stoi(opt(args, "--interval"));
    } catch (...) {
    }
    if (flag(args, "--watch")) {
      int cycles = flag(args, "--once") ? 1 : 0;
      int n = service::live_sync_watch(b, path, interval, cycles, source);
      b.drain_embed_jobs(100);
      std::cout << "live_sync_watch imported_pages_total=" << n << "\n";
      return 0;
    }
    auto r = service::live_sync_once(b, path, source);
    b.drain_embed_jobs(100);
    std::cout << "live_sync scanned=" << r.scanned << " imported=" << r.imported_pages
              << " skipped=" << r.skipped << " errors=" << r.errors << "\n";
    return r.errors && !r.imported_pages ? 1 : 0;
  });
}

int cmd_worker(const std::vector<std::string>& args) {
  return with_brain(args, [&](Brain& b) {
    int cycles = flag(args, "--once") ? 1 : 1000000;
    for (int i = 0; i < cycles; ++i) {
      int n = jobs::drain_jobs(b, 20, "cli-worker");
      int legacy = b.drain_embed_jobs(10);
      int inbox_n = service::watch_inbox_once(b);
      if (n || legacy || inbox_n)
        std::cout << "worker jobs=" << n << " legacy_embed=" << legacy << " inbox=" << inbox_n
                  << "\n";
      if (flag(args, "--once")) break;
      Sleep(3000);
    }
    return 0;
  });
}

int cmd_dream(const std::vector<std::string>& args) {
  return with_brain(args, [&](Brain& b) {
    cycle::DreamOpts opts;
    opts.dry_run = !flag(args, "--apply");
    opts.phase = opt(args, "--phase");
    if (flag(args, "--json")) {
      auto report = cycle::run_dream(b, opts);
      std::cout << cycle::report_to_json(report) << "\n";
      return report.status == "failed" ? 1 : 0;
    }
    auto report = cycle::run_dream(b, opts);
    std::cout << cycle::report_to_text(report);
    if (opts.dry_run) std::cout << "re-run with --apply to write\n";
    return report.status == "failed" ? 1 : 0;
  });
}

int cmd_delete(const std::vector<std::string>& args) {
  return with_brain(args, [&](Brain& b) {
    auto slug = join_positional(args, {"--brain", "--source", "--json"});
    if (slug.empty()) {
      std::cerr << "usage: qbrain delete <slug>\n";
      return 1;
    }
    auto source = opt(args, "--source", "default");
    if (!b.soft_delete(slug, source)) {
      std::cerr << "not found or already deleted: " << slug << "\n";
      return 1;
    }
    std::cout << "deleted " << slug << "\n";
    return 0;
  });
}

int cmd_embed(const std::vector<std::string>& args) {
  return with_brain(args, [&](Brain& b) {
    if (flag(args, "--drain")) {
      int n = b.drain_embed_jobs(1000);
      std::cout << "drained embed jobs; chunks_updated=" << n << "\n";
      return 0;
    }
    std::vector<Chunk> targets;
    if (flag(args, "--all")) {
      targets = b.list_chunks_missing_embedding(100000);
    } else {
      auto slug = opt(args, "--slug");
      if (slug.empty()) {
        std::cerr << "usage: qbrain embed --all | --slug s\n";
        return 1;
      }
      auto page = b.get_page(slug);
      if (!page) {
        std::cerr << "not found\n";
        return 1;
      }
      targets = b.get_chunks(page->id);
    }
    if (targets.empty()) {
      std::cout << "nothing to embed\n";
      return 0;
    }
    int done = 0;
    for (size_t i = 0; i < targets.size(); i += 16) {
      size_t n = (std::min)(static_cast<size_t>(16), targets.size() - i);
      std::vector<std::string> texts;
      for (size_t j = 0; j < n; ++j) texts.push_back(targets[i + j].text);
      auto er = ai::embed_texts(b.config(), texts);
      if (!er.ok) {
        std::cerr << "embed failed: " << er.error << "\n";
        return 2;
      }
      for (size_t j = 0; j < n && j < er.vectors.size(); ++j) {
        b.update_chunk_embedding(targets[i + j].id, er.vectors[j], er.model);
        ++done;
      }
    }
    std::cout << "embedded " << done << " chunks\n";
    return 0;
  });
}

}  // namespace

int run(int argc, char** argv) {
  ops::register_builtin_ops();
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
  if (args.empty() || args[0] == "help" || args[0] == "-h" || args[0] == "--help") {
    print_help();
    return 0;
  }
  if (args[0] == "version" || args[0] == "--version") {
    std::cout << "qbrain 0.1.0-dev (windows-native c++)\n";
    return 0;
  }

  try {
    const auto& cmd = args[0];
    std::vector<std::string> rest(args.begin() + 1, args.end());
    if (cmd == "init") return cmd_init(rest);
    if (cmd == "doctor") return cmd_doctor(rest);
    if (cmd == "config") return cmd_config(rest);
    if (cmd == "put") return cmd_put(rest);
    if (cmd == "get") return cmd_get(rest);
    if (cmd == "list") return cmd_list(rest);
    if (cmd == "capture") return cmd_capture(rest);
    if (cmd == "import") return cmd_import(rest);
    if (cmd == "search") return cmd_search(rest);
    if (cmd == "think") return cmd_think(rest);
    if (cmd == "graph") return cmd_graph(rest);
    if (cmd == "delete") return cmd_delete(rest);
    if (cmd == "embed") return cmd_embed(rest);
    if (cmd == "serve") return cmd_serve(rest);
    if (cmd == "inbox") return cmd_inbox(rest);
    if (cmd == "sync") return cmd_sync(rest);
    if (cmd == "worker") return cmd_worker(rest);
    if (cmd == "dream") return cmd_dream(rest);
    std::cerr << "unknown command: " << cmd << "\n";
    print_help();
    return 1;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 2;
  }
}

}  // namespace qbrain::cli
