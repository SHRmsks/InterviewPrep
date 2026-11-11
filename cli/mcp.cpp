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

It speaks protocol version 2026-07-28 only, the one Claude Code uses: every
request carries its version in _meta, and nothing is remembered between
requests. A client from before that version opens with `initialize` instead,
and gets an error that names the version to use.
*/

#include "mcp.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include "files.hpp"
using namespace std;
namespace interviewprep
{

  namespace
  {

    constexpr const char *SERVER_NAME = "interviewprep";
    constexpr const char *SERVER_VERSION = "1.0";

    constexpr const char *PROTOCOL_VERSION = "2026-07-28";

    constexpr const char *META_VERSION = "io.modelcontextprotocol/protocolVersion";
    constexpr const char *META_CAPABILITIES = "io.modelcontextprotocol/clientCapabilities";
    constexpr const char *META_SERVER = "io.modelcontextprotocol/serverInfo";

    constexpr int INVALID_REQUEST = -32600;
    constexpr int METHOD_NOT_FOUND = -32601;
    constexpr int INVALID_PARAMS = -32602;
    constexpr int PARSE_ERROR = -32700;
    constexpr int UNSUPPORTED_VERSION = -32022;

    // How long a client may reuse the tool list. It only changes with this binary.
    constexpr int LIST_TTL_MS = 60 * 60 * 1000;

    string instructions()
    {
      return 
      "InterviewPrep is this person's coding interview practice app, and you are their coach. You judge the code they wrote dry run, before first run, before first submission in order, record the pattern you saw, and keep a profile for the next session. The app decides what they practise next, from your adjudication labels. Their own grade drives their reviews. Yours can only add: it keeps a problem's checks going when you find a bug on the check that would retire it, and when stricter than theirs it brings that pattern up more often.\nCall profile_read first in every session.\nTo review a batch:\n1. batch_pending. A batch is " + to_string(BATCH_SIZE) + " graded problems. If fewer are waiting, say how many and ask first.\n2. topics_list, once. Name things only from its lists.\n3. For each problem, judgements_search its topics, then read the code. Grade the first paste, written before running; the later ones show what running taught them. Record the thinking (did they recognise the pattern, and find the key step themselves, after a hint, or not at all), the topics it really uses, and each slip from the flaw list. Keep the two apart: a slip never means they don't know the pattern.\n4. judgement_write once per problem, or judgement_drop one with nothing to add.\n5. batch_commit the profile.\n6. In a few lines, tell them what went well, which topic/patterns went wrong and why. Leave out what comes next unless asked, so they still have to recognise patterns cold.\n\nRules:\n- Use a whole pattern only when no finer topic fits. Anything more specific than a flaw goes in the note.\n- recognised yes includes any correct approach fast enough for the limits. wrong is a pattern whose precondition fails here; name it in reached_for.\n- Name a flaw only when the code shows it, by its cause, not its symptom.\n- No code before " + string(BLANK_WITHOUT_CODE_FROM) + " means nothing was saved: drop it. From then on no code is a blank: judge it as one, and invent nothing.\n- They ran the code and you only read it.\n- Your labels are your judgement, not measurements. Say so if asked.";
    }

    // "off-by-one","overflow",... for a JSON schema enum. The clock's flaw isn't the coach's to name.
    string flawEnum()
    {
      string list;
      for (const FlawInfo &f : FLAWS)
        if (!f.byClock)
          list += string(list.empty() ? "" : ",") + "\"" + f.slug + "\"";
      return list;
    }

    Json tool(const char *name, const string &description, const string &schema, bool readOnly)
    {
      Json definition = Json::object().set("name", name).set("description", description);
      definition.set("inputSchema", Json::parse(schema));
      definition.set("annotations", Json::object().set("readOnlyHint", readOnly));
      return definition;
    }

    Json toolList()
    {
      constexpr const char *NO_ARGUMENTS = R"({"type":"object","additionalProperties":false})";
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
          "the flaws, and the thinking labels. Call it once per review, before the first "
          "judgement.",
          NO_ARGUMENTS, true));
      tools.push(tool(
          "batch_pending",
          "List the next graded problems with no judgement yet, oldest first, at most " + to_string(BATCH_SIZE) +
              " items. Each has their grade, their time and their code: a file per day, every paste in order under a \"Paste N at "
              "HH:MM\" line. The first paste was written before running anything, the later ones are fixes after running, "
              "and the last is what they submitted. Over the read limit the middle is cut, never the first or the last. "
              "resources_patterns comes from rules and resources_topics from a one-time labeling, so either can be wrong. "
              "An empty attempts list means no code, and no_code says what that means: a blank to judge as one, or "
              "nothing saved then, to drop. An attempt from before 2026-11-09 carries earlier_judgement, "
              "what was said then in an older shape, and your_gaps or your_bug if the app asked them then. total says how "
              "many are waiting in all.",
          NO_ARGUMENTS, true));
      tools.push(tool(
          "judgements_search",
          "Search past judgements. Give a topic, pattern or flaw to see how this person has done on it: first_tries and "
          "first_try_passes count every match, not only the lines returned, and a pass needs both their grade and yours "
          "to be blue. Filters combine. Lines with earlier_shape are from before 2026-11-09 and use older labels. Returns "
          "at most " +
              to_string(SEARCH_MAX_LINES) + " lines, newest first.",
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
          "for good and can't be replaced. With code, model_grade, recognised, key_step and note are required. A blank "
          "needs no more than problem, date and topics: it is saved as model_grade r, recognised no, key_step missed. "
          "With no code from before " + string(BLANK_WITHOUT_CODE_FROM) + ", send only what the grade and time support, "
          "and no flaws.",
          string(R"({"type":"object","properties":{
        "problem":{"type":"string"},
        "date":{"type":"string"},
        "model_grade":{"type":"string","enum":["b","y","r"],"description":"The grade you would give their first paste, written before running: b clean, y a slip or a small hint, r wrong or no idea."},
        "recognised":{"type":"string","enum":["yes","partial","wrong","no"],"description":"Did they see which pattern this was? yes: the right pattern, or any other approach that is correct and fast enough for the limits. partial: the right family, the wrong variant, such as a fixed window where it has to grow and shrink. wrong: a pattern whose precondition fails here, such as a window over negative numbers; name it in reached_for. no: no approach, or a brute force nowhere near it."},
        "reached_for":{"type":"string","description":"Only with recognised wrong: the slug of the pattern they went for instead, from topics_list. Never one this problem uses."},
        "key_step":{"type":"string","enum":["own","hint","missed"],"description":"Did they find the key step themselves, after a hint, or not at all? For DP the state and the recurrence are the key step. With recognised wrong or no it is missed."},
        "topics":{"type":"array","items":{"type":"string"},"description":"What this problem really uses, at most )") +
              to_string(JUDGEMENT_MAX_TOPICS) + R"(, from topics_list. The flaws you name are counted on these."},
        "flaws":{"type":"array","items":{"type":"string","enum":[)" +
              flawEnum() + R"(]},"description":"The slips in this code, each named by its cause, not its symptom. A whole step of the algorithm not there at all, such as sorting first or a visited set, is missing-piece. A step that is there but wrong, or one update, bound or branch missing from code that is there, is a code flaw: wrong-state-update, off-by-one or wrong-condition. An input or outcome the code doesn't handle is missed-edge-input. C++ semantics are library-misuse even inside a condition. misread-problem only when the code answers another question than the one asked. A brute force from not seeing the pattern is recognised no, not too-slow-approach. Empty if there were none, or if there is no code. A slip never counts against the pattern."},
        "note":{"type":"string","description":"What you saw, in at most )" +
              to_string(NOTE_MAX_CHARS) + R"( characters."},
        "traced":{"type":"string","description":"Required when your grade is kinder than theirs: a small input, what this code returns when you work it by hand, and the right answer."},
        "by":{"type":"string","description":"Your model name."}
      },"required":["problem","date"],"additionalProperties":false})",
          false));
      tools.push(tool(
          "judgement_drop",
          "Leave one graded problem out of the judgement log, so their grade alone counts for it. Only for an attempt with "
          "nothing to add: no code saved before " + string(BLANK_WITHOUT_CODE_FROM) + ", or an earlier_judgement that "
          "already says it. One with code and no earlier judgement must be judged, and so must a blank.",
          R"({"type":"object","properties":{
        "problem":{"type":"string"},
        "date":{"type":"string"},
        "reason":{"type":"string","description":"Why it has nothing to add, in a sentence."},
        "by":{"type":"string","description":"Your model name."}
      },"required":["problem","date","reason"],"additionalProperties":false})",
          false));
      tools.push(tool(
          "batch_commit",
          "Save the profile, replacing it. It may be at most " + to_string(PROFILE_MAX_BYTES) + " bytes. Over the limit the commit is refused: shorten it, since every judgement stays in the log.",
          R"({"type":"object","properties":{
        "profile":{"type":"string","description":"What the next session should know, in markdown: patterns they recognise or miss, the flaws that keep coming back, and recent performance."}
      },"required":["profile"],"additionalProperties":false})",
          false));
      return tools;
    }

    Json textBlock(const string &text) { return Json::object().set("type", "text").set("text", text); }

    Json toolResult(const Json &value)
    {
      Json content = Json::array();
      content.push(textBlock(value.dump()));
      return Json::object().set("content", content);
    }

    /*
    A refusal goes to the model as a result, not a protocol error, so it can read
    the reason and try again.
    */
    Json toolError(const string &message)
    {
      Json content = Json::array();
      content.push(textBlock(message));
      return Json::object().set("content", content).set("isError", true);
    }

    // A core reply without the fields that only matter to a front end.
    Json withoutEnvelope(const Json &reply)
    {
      Json clean = Json::object();
      for (const auto &field : reply.members())
      {
        if (field.first != "ok" && field.first != "dirty")
          clean.set(field.first, field.second);
      }
      return clean;
    }

    Json copyFields(Json request, const Json &args, initializer_list<const char *> fields)
    {
      for (const char *field : fields)
      {
        if (args.has(field))
          request.set(field, args[field]);
      }
      return request;
    }

    // Stops on a character boundary, because half a character isn't valid text.
    string firstBytes(const string &text, size_t count)
    {
      while (count > 0 && (static_cast<unsigned char>(text[count]) & 0xC0) == 0x80)
        --count;
      return text.substr(0, count);
    }

    /*
    The first and the last of what they pasted matter most: what they wrote before
    running anything, and what they ended with. Over the limit the middle goes, cut
    on character boundaries, and a line in its place says how much.
    */
    void keepEnds(vector<string> &texts, size_t room, bool *cut)
    {
      size_t total = 0;
      for (const string &text : texts)
        total += text.size();
      if (total <= room)
        return;
      *cut = true;
      const size_t head = room / 2;
      const size_t tailFrom = total - (room - head); // flattened offsets, across every file
      size_t offset = 0;
      for (string &text : texts)
      {
        const size_t size = text.size();
        const size_t keepHead = head > offset ? min(size, head - offset) : 0;
        size_t keepFrom = tailFrom > offset ? min(size, tailFrom - offset) : 0;
        offset += size;
        if (keepHead >= keepFrom)
          continue; // this file is kept whole
        const string front = firstBytes(text, keepHead);
        while (keepFrom < size && (static_cast<unsigned char>(text[keepFrom]) & 0xC0) == 0x80)
          ++keepFrom;
        const size_t gone = keepFrom - front.size();
        text = front + (front.empty() || front.back() == '\n' ? "" : "\n") + "// … " + to_string(gone) +
               " bytes cut here …\n" + text.substr(keepFrom);
      }
    }

    // What server/discover lists, and what a request with the wrong version is told.
    Json supportedVersions()
    {
      Json versions = Json::array();
      versions.push(PROTOCOL_VERSION);
      return versions;
    }

    Json serverInfo() { return Json::object().set("name", SERVER_NAME).set("version", SERVER_VERSION); }

    Json capabilities() { return Json::object().set("tools", Json::object()); }

    class Server
    {
    public:
      Server(Engine &engine, const string &statePath, const string &fixedNow)
          : engine_(engine), statePath_(statePath), folder_(directoryOf(statePath)), fixedNow_(fixedNow) {}

      int run()
      {
        string line;
        while (getline(cin, line))
        {
          if (line.empty())
            continue;
          string error;
          const Json message = Json::parse(line, &error);
          if (message.isObject())
            handle(message);
          else
            fail(Json(), PARSE_ERROR, "Parse error: " + error);
        }
        return 0;
      }

    private:
      Engine &engine_;
      string statePath_;
      string folder_;
      string fixedNow_;

      // ── The protocol ────────────────────────────────────────────────────────

      static void send(const Json &message) { cout << message.dump() << '\n'
                                                        << flush; }

      static void reply(const Json &id, Json result)
      {
        result.set("resultType", "complete");
        result.set("_meta", Json::object().set(META_SERVER, serverInfo()));
        send(Json::object().set("jsonrpc", "2.0").set("id", id).set("result", move(result)));
      }

      static void fail(const Json &id, int code, const string &text, const Json &data = Json())
      {
        Json error = Json::object().set("code", code).set("message", text);
        if (!data.isNull())
          error.set("data", data);
        Json message = Json::object().set("jsonrpc", "2.0");
        if (!id.isNull())
          message.set("id", id);
        send(message.set("error", move(error)));
      }

      void handle(const Json &message)
      {
        if (!message.has("id"))
          return; // a notification, and none of them need anything from us
        const Json &id = message["id"];
        if (!id.isString() && !id.isNumber())
          return fail(Json(), INVALID_REQUEST, "id must be a string or a number.");
        if (!message["method"].isString())
          return fail(id, INVALID_REQUEST, "A request needs a method.");
        const Json &params = message["params"];
        const Json &meta = params["_meta"];

        // Nothing is remembered between requests, so each one says which version it speaks.
        if (!meta.has(META_VERSION))
        {
          return fail(id, INVALID_PARAMS, string("This server speaks protocol version ") + PROTOCOL_VERSION + " only. Put it in _meta as " + META_VERSION + ".");
        }
        const string &version = meta[META_VERSION].asStr();
        if (version != PROTOCOL_VERSION)
        {
          return fail(id, UNSUPPORTED_VERSION, "Unsupported protocol version",
                      Json::object().set("supported", supportedVersions()).set("requested", version));
        }
        if (!meta.has(META_CAPABILITIES))
          return fail(id, INVALID_PARAMS, string("_meta needs ") + META_CAPABILITIES + ".");
        serve(id, message["method"].asStr(), params);
      }

      void serve(const Json &id, const string &method, const Json &params)
      {
        if (method == "server/discover")
        {
          Json result = Json::object().set("supportedVersions", supportedVersions()).set("capabilities", capabilities());
          result.set("instructions", instructions());
          return reply(id, cacheable(move(result)));
        }
        if (method == "tools/list")
          return reply(id, cacheable(Json::object().set("tools", toolList())));
        if (method == "tools/call")
        {
          const string &name = params["name"].asStr();
          Json result;
          if (!callTool(name, params["arguments"], &result))
            return fail(id, INVALID_PARAMS, "Unknown tool: " + name);
          return reply(id, move(result));
        }
        fail(id, METHOD_NOT_FOUND, "Method not found: " + method);
      }

      static Json cacheable(Json result)
      {
        return result.set("ttlMs", LIST_TTL_MS).set("cacheScope", "public");
      }

      // ── Files ───────────────────────────────────────────────────────────────

      string path(const string &relative) const { return folder_ + "/" + relative; }
      string readLog() const { return readFile(path(JUDGEMENTS_FILE)); }
      // The judgements from before 2026-11-09. Read, never written.
      string readOldLog() const { return readFile(path(OLD_JUDGEMENTS_FILE)); }

      // "lc207/2026-11-06T21-30.cpp", one for each file in the attempts folder.
      Json attemptPaths() const
      {
        namespace fs = filesystem;
        Json paths = Json::array();
        error_code ignored;
        for (const auto &problem : fs::directory_iterator(path(ATTEMPT_FOLDER), ignored))
        {
          for (const auto &file : fs::directory_iterator(problem.path(), ignored))
          {
            paths.push(problem.path().filename().string() + "/" + file.path().filename().string());
          }
        }
        return paths;
      }

      // ── Tools ───────────────────────────────────────────────────────────────

      Json ask(Json request)
      {
        request.set("now", fixedNow_.empty() ? nowString() : fixedNow_);
        return Json::parse(engine_.call(request.dump()));
      }

      static Json command(const char *name) { return Json::object().set("cmd", name); }

      bool callTool(const string &name, const Json &args, Json *result)
      {
        using Handler = Json (Server::*)(const Json &);
        static const pair<const char *, Handler> handlers[] = {
            {"profile_read", &Server::profileRead},
            {"topics_list", &Server::topicsList},
            {"batch_pending", &Server::batchPending},
            {"judgements_search", &Server::search},
            {"judgement_write", &Server::judgement},
            {"judgement_drop", &Server::drop},
            {"batch_commit", &Server::commit},
        };
        for (const auto &handler : handlers)
        {
          if (name != handler.first)
            continue;
          // Read fresh every time: the app can grade a problem while this server runs.
          string error;
          const bool loaded = engine_.loadState(exists(statePath_) ? readFile(statePath_) : "{}", &error);
          *result = loaded ? (this->*handler.second)(args) : toolError("Can't read progress: " + error);
          return true;
        }
        return false;
      }

      Json profileRead(const Json &)
      {
        const Json status = ask(command("overview").set("log", readLog()).set("old_log", readOldLog()));
        if (!status["ok"].asBool())
          return toolError(status["error"].asStr());
        const string profile = readFile(path(PROFILE_FILE));
        Json content = Json::array();
        content.push(textBlock(profile.empty() ? "No profile yet: nothing has been reviewed." : profile));
        content.push(textBlock(withoutEnvelope(status).dump()));
        return Json::object().set("content", content);
      }

      Json batchPending(const Json &)
      {
        Json reply = ask(command("pending").set("log", readLog()).set("old_log", readOldLog()).set("attempts", attemptPaths()));
        if (!reply["ok"].asBool())
          return toolError(reply["error"].asStr());
        Json items = Json::array();
        for (const Json &listed : reply["items"].items())
        {
          const vector<Json> &files = listed["attempts"].items();
          vector<string> texts;
          for (const Json &file : files)
            texts.push_back(readFile(path(string(ATTEMPT_FOLDER) + "/" + file.asStr())));
          bool cutShort = false;
          keepEnds(texts, ATTEMPT_READ_MAX_BYTES, &cutShort);
          Json code = Json::array();
          for (size_t k = 0; k < files.size(); ++k)
            code.push(Json::object().set("file", files[k]).set("code", move(texts[k])));
          Json item = listed;
          item.set("attempts", move(code));
          if (cutShort)
            item.set("code_cut_short", true);
          items.push(move(item));
        }
        reply.set("items", move(items));
        return toolResult(withoutEnvelope(reply));
      }

      Json search(const Json &args)
      {
        Json request = command("search").set("log", readLog()).set("old_log", readOldLog());
        const Json reply = ask(copyFields(move(request), args, {"label", "problem", "text", "limit"}));
        return reply["ok"].asBool() ? toolResult(withoutEnvelope(reply)) : toolError(reply["error"].asStr());
      }

      Json topicsList(const Json &)
      {
        const Json reply = ask(command("topics"));
        return reply["ok"].asBool() ? toolResult(withoutEnvelope(reply)) : toolError(reply["error"].asStr());
      }

      Json judgement(const Json &args)
      {
        const Json reply = ask(command("judgement").set("judgement", args).set("log", readLog()).set("attempts", attemptPaths()));
        if (!reply["ok"].asBool())
          return toolError(reply["error"].asStr());
        if (!appendLine(path(JUDGEMENTS_FILE), reply["line"].dump()))
          return toolError("Couldn't write the judgement log.");
        return toolResult(Json::object().set("saved", true).set("remaining", reply["remaining"]));
      }

      Json drop(const Json &args)
      {
        const Json reply = ask(command("drop").set("drop", args).set("log", readLog()).set("old_log", readOldLog()).set("attempts", attemptPaths()));
        if (!reply["ok"].asBool())
          return toolError(reply["error"].asStr());
        if (!appendLine(path(JUDGEMENTS_FILE), reply["line"].dump()))
          return toolError("Couldn't write the judgement log.");
        return toolResult(Json::object().set("dropped", true).set("remaining", reply["remaining"]));
      }

      Json commit(const Json &args)
      {
        const Json reply = ask(copyFields(command("commit"), args, {"profile"}));
        if (!reply["ok"].asBool())
          return toolError(reply["error"].asStr());
        if (!replaceFile(path(PROFILE_FILE), args["profile"].asStr()))
          return toolError("Couldn't write the profile.");
        return toolResult(Json::object().set("saved", true));
      }
    };

  } // namespace

  int runMcpServer(Engine &engine, const string &statePath, const string &fixedNow)
  {
    return Server(engine, statePath, fixedNow).run();
  }

} // namespace interviewprep
