/*
mcp.cpp — `interviewprep mcp`, the door a model comes in through.

Speaks the Model Context Protocol over stdin and stdout, one JSON message per
line. This file only moves bytes: it reads files, asks the core, and writes
back what the core approves. Every rule is in core/coach.cpp, and every limit
is in section 10 of core/config.hpp.

It writes only under profile/, never to state.json. That file belongs to the
app, and the Mac app rewrites it from memory, so a change made here would be
lost anyway. Within profile/ it never writes the files from before 2026-11-09:
the older judgements and the probability tables stay exactly as they were.

It answers both eras of the protocol, which the spec calls a dual-era server.
A client on 2026-07-28 or later puts its version in the _meta of each request
and holds no session. An older client opens with `initialize` once.
*/

#include "mcp.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

#include "files.hpp"

namespace interviewprep {

namespace {

constexpr const char* SERVER_NAME = "interviewprep";
constexpr const char* SERVER_VERSION = "1.0";

constexpr const char* MODERN_VERSION = "2026-07-28";
constexpr const char* LEGACY_VERSIONS[] = {"2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"};

constexpr const char* META_VERSION = "io.modelcontextprotocol/protocolVersion";
constexpr const char* META_CAPABILITIES = "io.modelcontextprotocol/clientCapabilities";
constexpr const char* META_SERVER = "io.modelcontextprotocol/serverInfo";

constexpr int INVALID_REQUEST = -32600;
constexpr int METHOD_NOT_FOUND = -32601;
constexpr int INVALID_PARAMS = -32602;
constexpr int PARSE_ERROR = -32700;
constexpr int UNSUPPORTED_VERSION = -32022;

// How long a client may reuse the tool list. It only changes with this binary.
constexpr int LIST_TTL_MS = 60 * 60 * 1000;

std::string instructions() {
  return "InterviewPrep is this person's coding interview practice app, and you are their coach. You judge the code they "
         "wrote before running it, record what you saw, and keep a profile for the next session. The app decides what "
         "they practise next, from your labels. Their own grade drives their reviews; your grade can only keep a "
         "problem's checks going, when you find a bug on the check that would retire it.\n\n"
         "Call profile_read first in every session.\n\n"
         "To review a batch:\n"
         "1. batch_pending. A batch is " + std::to_string(BATCH_SIZE) + " graded problems. If fewer are waiting, say "
         "how many and ask first.\n"
         "2. topics_list, once. Name things only from its lists.\n"
         "3. For each problem, judgements_search its topics, then read the code. Grade the first paste, written before "
         "running; the later ones show what running taught them. Record the thinking (did they recognise the pattern, "
         "and find the key step themselves, after a hint, or not at all), the topics it really uses, and each coding "
         "slip from the flaw list. Keep the two apart: a slip never means they don't know the pattern.\n"
         "4. judgement_write once per problem. An attempt from before 2026-11-09 shows its earlier_judgement in an older "
         "shape: judge it again, or judgement_drop it when it has nothing to add, such as one with no code.\n"
         "5. batch_commit the profile.\n"
         "6. In a few lines, tell them what went well, what went wrong and why. Leave out what comes next unless asked, "
         "so they still have to recognise patterns cold.\n\n"
         "Rules:\n"
         "- Use a whole pattern only when no finer topic fits. Anything more specific than a flaw goes in the note.\n"
         "- Name a flaw only when the code shows it. With no code, go by the grade and time, and invent nothing.\n"
         "- They ran the code and you only read it. If you'd grade kinder, assume you missed something: work a small "
         "input through the code by hand and put it in traced.\n"
         "- Your labels are your judgement, not measurements. Say so if asked.";
}

// "off-by-one","overflow",... for a JSON schema enum.
std::string flawEnum() {
  std::string list;
  for (const FlawInfo& f : FLAWS) list += std::string(list.empty() ? "" : ",") + "\"" + f.slug + "\"";
  return list;
}

Json tool(const char* name, const std::string& description, const std::string& schema, bool readOnly) {
  Json definition = Json::object().set("name", name).set("description", description);
  definition.set("inputSchema", Json::parse(schema));
  definition.set("annotations", Json::object().set("readOnlyHint", readOnly));
  return definition;
}

Json toolList() {
  constexpr const char* NO_ARGUMENTS = R"({"type":"object","additionalProperties":false})";
  Json tools = Json::array();
  tools.push(tool(
      "profile_read",
      "Read what earlier sessions learned about this person: their one-page profile, how often each flaw and each "
      "thinking label came up in judged code, and how many graded problems are waiting. Call this first in every "
      "session.",
      NO_ARGUMENTS, true));
  tools.push(tool(
      "topics_list",
      "The closed lists you name things from: every pattern with its finer topics and the signal that points to each, "
      "the flaws with the deck that drills each, and the thinking labels. Call it once per review, before the first "
      "judgement.",
      NO_ARGUMENTS, true));
  tools.push(tool(
      "batch_pending",
      "List the next graded problems with no judgement yet, oldest first, at most " + std::to_string(BATCH_SIZE) +
          ". Each has their grade, their time and their code: a file per day, every paste in order under a \"Paste N at "
          "HH:MM\" line. The first paste was written before running anything, the later ones are fixes after running, "
          "and the last is what they submitted. Over the read limit the middle is cut, never the first or the last. "
          "resources_patterns comes from rules and resources_topics from a one-time labeling, so either can be wrong. "
          "An empty attempts list means they saved no code. An attempt from before 2026-11-09 carries earlier_judgement, "
          "what was said then in an older shape, and your_gaps or your_bug if the app asked them then. total says how "
          "many are waiting in all.",
      NO_ARGUMENTS, true));
  tools.push(tool(
      "judgements_search",
      "Search past judgements. Give a topic, pattern or flaw to see how this person has done on it: first_tries and "
      "first_try_passes count every match, not only the lines returned, and a pass needs both their grade and yours "
      "to be blue. Filters combine. Lines with earlier_shape are from before 2026-11-09 and use older labels. Returns "
      "at most " + std::to_string(SEARCH_MAX_LINES) + " lines, newest first.",
      R"({"type":"object","properties":{
        "label":{"type":"string","description":"A topic, pattern or flaw. A pattern also finds its finer topics."},
        "problem":{"type":"string","description":"A problem id, such as lc207."},
        "text":{"type":"string","description":"Words to find in titles, notes and labels."},
        "limit":{"type":"integer","description":"Lines to return."}
      },"additionalProperties":false})",
      true));
  tools.push(tool(
      "judgement_write",
      "Save your judgement of one graded problem, using the problem and date from batch_pending. Judgements are kept "
      "for good and can't be replaced. With code, model_grade, recognised, key_step and note are required. With no "
      "code, send only what the grade and time support, and no flaws.",
      std::string(R"({"type":"object","properties":{
        "problem":{"type":"string"},
        "date":{"type":"string"},
        "model_grade":{"type":"string","enum":["b","y","r"],"description":"The grade you would give their first paste, written before running: b clean, y a slip or a small hint, r wrong or no idea."},
        "recognised":{"type":"string","enum":["yes","partial","no"],"description":"Did they see which pattern this was?"},
        "key_step":{"type":"string","enum":["own","hint","missed"],"description":"Did they find the key step themselves, after a hint, or not at all?"},
        "topics":{"type":"array","items":{"type":"string"},"description":"What this problem really uses, at most )") +
          std::to_string(JUDGEMENT_MAX_TOPICS) + R"(, from topics_list."},
        "flaws":{"type":"array","items":{"type":"string","enum":[)" + flawEnum() + R"(]},"description":"The coding slips in this code. Empty if there were none, or if there is no code. A slip never counts against the pattern."},
        "note":{"type":"string","description":"What you saw, in at most )" + std::to_string(NOTE_MAX_CHARS) + R"( characters."},
        "traced":{"type":"string","description":"Required when your grade is kinder than theirs: a small input, what this code returns when you work it by hand, and the right answer."},
        "by":{"type":"string","description":"Your model name."}
      },"required":["problem","date"],"additionalProperties":false})",
      false));
  tools.push(tool(
      "judgement_drop",
      "Leave one graded problem out of the judgement log, so their grade alone counts for it. Only for an attempt with "
      "nothing to add: no code saved, or an earlier_judgement that already says it. One with code and no earlier "
      "judgement must be judged.",
      R"({"type":"object","properties":{
        "problem":{"type":"string"},
        "date":{"type":"string"},
        "reason":{"type":"string","description":"Why it has nothing to add, in a sentence."},
        "by":{"type":"string","description":"Your model name."}
      },"required":["problem","date","reason"],"additionalProperties":false})",
      false));
  tools.push(tool(
      "batch_commit",
      "Save the profile, replacing it. It may be at most " + std::to_string(PROFILE_MAX_BYTES) + " bytes. Over the "
          "limit the commit is refused: shorten it, since every judgement stays in the log.",
      R"({"type":"object","properties":{
        "profile":{"type":"string","description":"What the next session should know, in markdown: patterns they recognise or miss, the flaws that keep coming back, and recent performance."}
      },"required":["profile"],"additionalProperties":false})",
      false));
  return tools;
}

Json textBlock(const std::string& text) { return Json::object().set("type", "text").set("text", text); }

Json toolResult(const Json& value) {
  Json content = Json::array();
  content.push(textBlock(value.dump()));
  return Json::object().set("content", content);
}

/*
A refusal goes to the model as a result, not a protocol error, so it can read
the reason and try again.
*/
Json toolError(const std::string& message) {
  Json content = Json::array();
  content.push(textBlock(message));
  return Json::object().set("content", content).set("isError", true);
}

// A core reply without the fields that only matter to a front end.
Json withoutEnvelope(const Json& reply) {
  Json clean = Json::object();
  for (const auto& field : reply.members()) {
    if (field.first != "ok" && field.first != "dirty") clean.set(field.first, field.second);
  }
  return clean;
}

Json copyFields(Json request, const Json& args, std::initializer_list<const char*> fields) {
  for (const char* field : fields) {
    if (args.has(field)) request.set(field, args[field]);
  }
  return request;
}

// Stops on a character boundary, because half a character isn't valid text.
std::string firstBytes(const std::string& text, size_t count) {
  while (count > 0 && (static_cast<unsigned char>(text[count]) & 0xC0) == 0x80) --count;
  return text.substr(0, count);
}

/*
The first and the last of what they pasted matter most: what they wrote before
running anything, and what they ended with. Over the limit the middle goes, cut
on character boundaries, and a line in its place says how much.
*/
void keepEnds(std::vector<std::string>& texts, size_t room, bool* cut) {
  size_t total = 0;
  for (const std::string& text : texts) total += text.size();
  if (total <= room) return;
  *cut = true;
  const size_t head = room / 2;
  const size_t tailFrom = total - (room - head);   // flattened offsets, across every file
  size_t offset = 0;
  for (std::string& text : texts) {
    const size_t size = text.size();
    const size_t keepHead = head > offset ? std::min(size, head - offset) : 0;
    size_t keepFrom = tailFrom > offset ? std::min(size, tailFrom - offset) : 0;
    offset += size;
    if (keepHead >= keepFrom) continue;  // this file is kept whole
    const std::string front = firstBytes(text, keepHead);
    while (keepFrom < size && (static_cast<unsigned char>(text[keepFrom]) & 0xC0) == 0x80) ++keepFrom;
    const size_t gone = keepFrom - front.size();
    text = front + (front.empty() || front.back() == '\n' ? "" : "\n") + "// … " + std::to_string(gone) +
           " bytes cut here …\n" + text.substr(keepFrom);
  }
}

bool isSupported(const std::string& version) {
  if (version == MODERN_VERSION) return true;
  for (const char* legacy : LEGACY_VERSIONS) {
    if (version == legacy) return true;
  }
  return false;
}

Json supportedVersions() {
  Json versions = Json::array();
  versions.push(MODERN_VERSION);
  for (const char* legacy : LEGACY_VERSIONS) versions.push(legacy);
  return versions;
}

Json serverInfo() { return Json::object().set("name", SERVER_NAME).set("version", SERVER_VERSION); }

Json capabilities() { return Json::object().set("tools", Json::object()); }

class Server {
 public:
  Server(Engine& engine, const std::string& statePath, const std::string& fixedNow)
      : engine_(engine), statePath_(statePath), folder_(directoryOf(statePath)), fixedNow_(fixedNow) {}

  int run() {
    std::string line;
    while (std::getline(std::cin, line)) {
      if (line.empty()) continue;
      std::string error;
      const Json message = Json::parse(line, &error);
      if (message.isObject()) handle(message);
      else fail(Json(), PARSE_ERROR, "Parse error: " + error);
    }
    return 0;
  }

 private:
  Engine& engine_;
  std::string statePath_;
  std::string folder_;
  std::string fixedNow_;
  bool openedWithInitialize_ = false;

  // ── The protocol ────────────────────────────────────────────────────────

  static void send(const Json& message) { std::cout << message.dump() << '\n' << std::flush; }

  static void reply(const Json& id, Json result, bool modern) {
    if (modern) {
      result.set("resultType", "complete");
      result.set("_meta", Json::object().set(META_SERVER, serverInfo()));
    }
    send(Json::object().set("jsonrpc", "2.0").set("id", id).set("result", std::move(result)));
  }

  static void fail(const Json& id, int code, const std::string& text, const Json& data = Json()) {
    Json error = Json::object().set("code", code).set("message", text);
    if (!data.isNull()) error.set("data", data);
    Json message = Json::object().set("jsonrpc", "2.0");
    if (!id.isNull()) message.set("id", id);
    send(message.set("error", std::move(error)));
  }

  void handle(const Json& message) {
    if (!message.has("id")) return;  // a notification, and none of them need anything from us
    const Json& id = message["id"];
    if (!id.isString() && !id.isNumber()) return fail(Json(), INVALID_REQUEST, "id must be a string or a number.");
    if (!message["method"].isString()) return fail(id, INVALID_REQUEST, "A request needs a method.");
    const std::string& method = message["method"].asStr();
    const Json& params = message["params"];
    const Json& meta = params["_meta"];

    if (meta.has(META_VERSION)) {
      const std::string& version = meta[META_VERSION].asStr();
      if (!isSupported(version)) {
        return fail(id, UNSUPPORTED_VERSION, "Unsupported protocol version",
                    Json::object().set("supported", supportedVersions()).set("requested", version));
      }
      if (!meta.has(META_CAPABILITIES)) return fail(id, INVALID_PARAMS, std::string("_meta needs ") + META_CAPABILITIES + ".");
      return serve(id, method, params, true);
    }

    if (method == "initialize") {
      openedWithInitialize_ = true;
      return reply(id, initializeResult(params), false);
    }
    if (method == "ping") return reply(id, Json::object(), false);
    if (!openedWithInitialize_) {
      return fail(id, INVALID_PARAMS, std::string("This request has no protocol version. Put one in _meta as ") + META_VERSION +
                                          ", or send initialize first.");
    }
    serve(id, method, params, false);
  }

  void serve(const Json& id, const std::string& method, const Json& params, bool modern) {
    if (method == "server/discover" && modern) {
      Json result = Json::object().set("supportedVersions", supportedVersions()).set("capabilities", capabilities());
      result.set("instructions", instructions());
      return reply(id, cacheable(std::move(result)), true);
    }
    if (method == "tools/list") {
      Json result = Json::object().set("tools", toolList());
      return reply(id, modern ? cacheable(std::move(result)) : std::move(result), modern);
    }
    if (method == "tools/call") {
      const std::string& name = params["name"].asStr();
      Json result;
      if (!callTool(name, params["arguments"], &result)) return fail(id, INVALID_PARAMS, "Unknown tool: " + name);
      return reply(id, std::move(result), modern);
    }
    fail(id, METHOD_NOT_FOUND, "Method not found: " + method);
  }

  static Json cacheable(Json result) {
    return result.set("ttlMs", LIST_TTL_MS).set("cacheScope", "public");
  }

  static Json initializeResult(const Json& params) {
    const std::string& asked = params["protocolVersion"].asStr();
    bool known = false;
    for (const char* legacy : LEGACY_VERSIONS) known = known || asked == legacy;
    return Json::object().set("protocolVersion", known ? asked : LEGACY_VERSIONS[0]).set("capabilities", capabilities())
        .set("serverInfo", serverInfo()).set("instructions", instructions());
  }

  // ── Files ───────────────────────────────────────────────────────────────

  std::string path(const std::string& relative) const { return folder_ + "/" + relative; }
  std::string readLog() const { return readFile(path(JUDGEMENTS_FILE)); }
  // The judgements from before 2026-11-09. Read, never written.
  std::string readOldLog() const { return readFile(path(OLD_JUDGEMENTS_FILE)); }

  // "lc207/2026-11-06T21-30.cpp", one for each file in the attempts folder.
  Json attemptPaths() const {
    namespace fs = std::filesystem;
    Json paths = Json::array();
    std::error_code ignored;
    for (const auto& problem : fs::directory_iterator(path(ATTEMPT_FOLDER), ignored)) {
      for (const auto& file : fs::directory_iterator(problem.path(), ignored)) {
        paths.push(problem.path().filename().string() + "/" + file.path().filename().string());
      }
    }
    return paths;
  }

  // ── Tools ───────────────────────────────────────────────────────────────

  Json ask(Json request) {
    request.set("now", fixedNow_.empty() ? nowString() : fixedNow_);
    return Json::parse(engine_.call(request.dump()));
  }

  static Json command(const char* name) { return Json::object().set("cmd", name); }

  bool callTool(const std::string& name, const Json& args, Json* result) {
    using Handler = Json (Server::*)(const Json&);
    static const std::pair<const char*, Handler> handlers[] = {
        {"profile_read", &Server::profileRead},   {"topics_list", &Server::topicsList},
        {"batch_pending", &Server::batchPending}, {"judgements_search", &Server::search},
        {"judgement_write", &Server::judgement},  {"judgement_drop", &Server::drop},
        {"batch_commit", &Server::commit},
    };
    for (const auto& handler : handlers) {
      if (name != handler.first) continue;
      // Read fresh every time: the app can grade a problem while this server runs.
      std::string error;
      const bool loaded = engine_.loadState(exists(statePath_) ? readFile(statePath_) : "{}", &error);
      *result = loaded ? (this->*handler.second)(args) : toolError("Can't read progress: " + error);
      return true;
    }
    return false;
  }

  Json profileRead(const Json&) {
    const Json status = ask(command("overview").set("log", readLog()).set("old_log", readOldLog()));
    if (!status["ok"].asBool()) return toolError(status["error"].asStr());
    const std::string profile = readFile(path(PROFILE_FILE));
    Json content = Json::array();
    content.push(textBlock(profile.empty() ? "No profile yet: nothing has been reviewed." : profile));
    content.push(textBlock(withoutEnvelope(status).dump()));
    return Json::object().set("content", content);
  }

  Json batchPending(const Json&) {
    Json reply = ask(command("pending").set("log", readLog()).set("old_log", readOldLog()).set("attempts", attemptPaths()));
    if (!reply["ok"].asBool()) return toolError(reply["error"].asStr());
    Json items = Json::array();
    for (const Json& listed : reply["items"].items()) {
      const std::vector<Json>& files = listed["attempts"].items();
      std::vector<std::string> texts;
      for (const Json& file : files) texts.push_back(readFile(path(std::string(ATTEMPT_FOLDER) + "/" + file.asStr())));
      bool cutShort = false;
      keepEnds(texts, ATTEMPT_READ_MAX_BYTES, &cutShort);
      Json code = Json::array();
      for (size_t k = 0; k < files.size(); ++k) code.push(Json::object().set("file", files[k]).set("code", std::move(texts[k])));
      Json item = listed;
      item.set("attempts", std::move(code));
      if (cutShort) item.set("code_cut_short", true);
      items.push(std::move(item));
    }
    reply.set("items", std::move(items));
    return toolResult(withoutEnvelope(reply));
  }

  Json search(const Json& args) {
    Json request = command("search").set("log", readLog()).set("old_log", readOldLog());
    const Json reply = ask(copyFields(std::move(request), args, {"label", "problem", "text", "limit"}));
    return reply["ok"].asBool() ? toolResult(withoutEnvelope(reply)) : toolError(reply["error"].asStr());
  }

  Json topicsList(const Json&) {
    const Json reply = ask(command("topics"));
    return reply["ok"].asBool() ? toolResult(withoutEnvelope(reply)) : toolError(reply["error"].asStr());
  }

  Json judgement(const Json& args) {
    const Json reply = ask(command("judgement").set("judgement", args).set("log", readLog()).set("attempts", attemptPaths()));
    if (!reply["ok"].asBool()) return toolError(reply["error"].asStr());
    if (!appendLine(path(JUDGEMENTS_FILE), reply["line"].dump())) return toolError("Couldn't write the judgement log.");
    return toolResult(Json::object().set("saved", true).set("remaining", reply["remaining"]));
  }

  Json drop(const Json& args) {
    const Json reply = ask(command("drop").set("drop", args).set("log", readLog()).set("old_log", readOldLog())
                               .set("attempts", attemptPaths()));
    if (!reply["ok"].asBool()) return toolError(reply["error"].asStr());
    if (!appendLine(path(JUDGEMENTS_FILE), reply["line"].dump())) return toolError("Couldn't write the judgement log.");
    return toolResult(Json::object().set("dropped", true).set("remaining", reply["remaining"]));
  }

  Json commit(const Json& args) {
    const Json reply = ask(copyFields(command("commit"), args, {"profile"}));
    if (!reply["ok"].asBool()) return toolError(reply["error"].asStr());
    if (!replaceFile(path(PROFILE_FILE), args["profile"].asStr())) return toolError("Couldn't write the profile.");
    return toolResult(Json::object().set("saved", true));
  }
};

}  // namespace

int runMcpServer(Engine& engine, const std::string& statePath, const std::string& fixedNow) {
  return Server(engine, statePath, fixedNow).run();
}

}  // namespace interviewprep
