/*
InterviewPrep website. Every rule lives in the C++ core (web/core.wasm). This file
only asks the core what to show and tells it what you clicked.
*/
(function () {
  "use strict";

  /*
  ── Core bridge ─────────────────────────────────────────────────────────
  core/capi.h, as JavaScript. The core is a plain WebAssembly module with no
  glue, so strings cross by hand: UTF-8 bytes, NUL terminated.
  */

  async function loadCore(bytes) {
    let memory = null;
    const wasi = {
      fd_close: () => 0,
      fd_seek: () => 0,
      fd_write: (fd, iovs, count, written) => {
        const view = new DataView(memory.buffer);
        let total = 0;
        for (let i = 0; i < count; i++) total += view.getUint32(iovs + i * 8 + 4, true);
        view.setUint32(written, total, true);
        return 0;
      },
      random_get: (pointer, length) => {
        crypto.getRandomValues(new Uint8Array(memory.buffer, pointer, length));
        return 0;
      },
    };
    const { instance } = await WebAssembly.instantiate(bytes, { wasi_snapshot_preview1: wasi });
    const x = instance.exports;
    memory = x.memory;
    x._initialize();

    const encoder = new TextEncoder();
    const decoder = new TextDecoder();
    const readString = (pointer) => {
      const heap = new Uint8Array(memory.buffer);
      let end = pointer;
      while (heap[end]) end++;
      return decoder.decode(heap.subarray(pointer, end));
    };
    const withString = (text, fn) => {
      const encoded = encoder.encode(text);
      const pointer = x.interviewprep_alloc(encoded.length + 1);
      const heap = new Uint8Array(memory.buffer);
      heap.set(encoded, pointer);
      heap[pointer + encoded.length] = 0;
      const result = fn(pointer);
      x.interviewprep_free(pointer);
      return result;
    };

    return {
      loadResources: (text) => withString(text, x.interviewprep_load_resources),
      loadState: (text) => withString(text, x.interviewprep_load_state),
      saveState: () => readString(x.interviewprep_save_state()),
      lastError: () => readString(x.interviewprep_last_error()),
      call: (request) => JSON.parse(withString(JSON.stringify(request), (p) => readString(x.interviewprep_call(p)))),
    };
  }

  /*
  ── Storage ─────────────────────────────────────────────────────────────
  In a Claude artifact your progress lives in the artifact's database, so it
  follows your account. Anywhere else it lives in this browser's storage.
  */

  const LOCAL_KEY = "interviewprep-state";
  const OLD_KEY = "coldstart-state";  // the app's name until November 2026: read once, gone after the first save
  const local = {
    read() { try { return localStorage.getItem(LOCAL_KEY) ?? localStorage.getItem(OLD_KEY); } catch (e) { return null; } },
    write(text) {
      try { localStorage.setItem(LOCAL_KEY, text); localStorage.removeItem(OLD_KEY); return true; } catch (e) { return false; }
    },
  };

  let remote = null;          // the artifact document, when there is one
  let writing = false;
  let writeAgain = false;
  let lastWritten = "";
  let writingNow = "";        // so our own save, echoing back, is not seen as news

  async function flush() {
    if (writing) { writeAgain = true; return; }
    writing = true;
    const text = core.saveState();
    local.write(text);
    try {
      if (remote && text !== lastWritten) {
        writingNow = text;
        await remote.set({ json: text });
        lastWritten = text;
      }
    } catch (e) {
      toast("Couldn't save to your account. Your progress is safe in this browser for now.");
    }
    writing = false;
    if (writeAgain) { writeAgain = false; flush(); }
  }

  async function connectRemote() {
    const api = window.claude && typeof window.claude.use === "function" ? window.claude : null;
    if (!api) return;
    let db = null, user = null;
    try { [db, user] = await Promise.all([api.use("db"), api.use("user")]); } catch (e) { return; }
    if (!db || !user) return;
    try {
      const id = await user.id();
      remote = db.collection("data/users/" + id).doc("state");
      const snapshot = await remote.get();
      if (snapshot.exists) {
        const data = snapshot.data();
        if (data && typeof data.json === "string") {
          lastWritten = data.json;
          if (core.loadState(data.json) === 0) { local.write(data.json); refresh(); }
        }
      } else {
        flush();  // first visit: keep whatever this browser already has
      }
      if (typeof remote.onSnapshot === "function") watchRemote();
    } catch (e) {
      remote = null;
    }
  }

  /*
  Picks up changes from another tab or device. Waits while you are mid-session,
  because swapping the state under an open card would lose your place.
  */
  function watchRemote() {
    remote.onSnapshot((snapshot) => {
      if (!snapshot.exists) return;
      const data = snapshot.data();
      if (!data || typeof data.json !== "string") return;
      if (data.json === lastWritten || data.json === writingNow) return;
      if (ui.session) return;
      lastWritten = data.json;
      if (core.loadState(data.json) === 0) { local.write(data.json); refresh(); }
    }, () => {});
  }

  // ── Shared state ────────────────────────────────────────────────────────

  let core = null;
  let facets = null;

  const ui = {
    view: "today",
    library: { kind: "problem", status: "", pattern: "", curated: false, q: "", limit: 100 },
    session: null,
  };

  const $ = (selector) => document.querySelector(selector);
  const esc = (s) => String(s == null ? "" : s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
  const inline = (s) => esc(s).replace(/`([^`]+)`/g, "<code>$1</code>");

  function nowString() {
    const d = new Date();
    const pad = (n) => String(n).padStart(2, "0");
    return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}T${pad(d.getHours())}:${pad(d.getMinutes())}`;
  }

  function call(request) {
    const response = core.call({ now: nowString(), ...request });
    if (response.dirty) flush();
    if (!response.ok) toast(response.error || "That didn't work.");
    return response;
  }

  let toastTimer = null;
  function toast(message) {
    const el = $("#toast");
    el.textContent = message;
    el.hidden = false;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => { el.hidden = true; }, 3400);
  }

  // ── Small pieces ────────────────────────────────────────────────────────

  const dot = (card) => `<span class="dot dot-${esc(card.track)}" title="${esc(card.position)}"></span>`;

  function pips(card) {
    if (!card.steps) return "";
    let html = "";
    for (let i = 0; i < card.steps; i++) html += `<i class="${i < card.step ? "on" : i === card.step ? "cur" : ""}"></i>`;
    return `<span class="pips t-${esc(card.track)}" title="${esc(card.position)}">${html}</span>`;
  }

  const SLOT_LABEL = {
    sibling: "Sibling", weak: "Weak spot", explore: "New ground", wildcard: "Wildcard", coach: "Coach", new: "New",
    "new-pattern": "New pattern", "new-topic": "New topic", practice: "Practice",
  };
  const slotBadge = (slot) => (SLOT_LABEL[slot] ? `<span class="badge badge-${esc(slot)}">${SLOT_LABEL[slot]}</span>` : "");

  function levelText(card) {
    if (card.kind !== "problem") return "";
    return `<span class="${card.level === "Hard" ? "hard" : ""}">${esc(card.level)}</span>`;
  }

  function dueText(card) {
    if (card.track === "new") return { text: "New", now: false };
    if (card.track === "retired") return { text: "Gone", now: false };
    if (card.dueIn <= 0) return { text: card.dueIn < 0 ? `Due, waited ${-card.dueIn}d` : "Due", now: true };
    return { text: `In ${card.dueIn}d`, now: false };
  }

  const titleOf = (card) => (card.kind === "problem" ? `<span class="num">${card.number}</span>${esc(card.title)}` : inline(card.title));

  // Where this card is on its way out, as the core lays out its checks.
  function rail(card) {
    if (!card.rail || !card.rail.length) return "";
    const stops = card.rail;
    const here = Math.min(card.railAt || 0, stops.length - 1);
    const html = stops.map((s, n) => {
      const state = n < here ? "on" : n === here ? "here" : "";
      return `<span class="stop t-${esc(s.track)} ${state}" title="Check after ${esc(s.gap)}"><b></b>${esc(s.gap)}</span>`;
    }).join("");
    return `<div class="rail" role="img" aria-label="${esc(card.position)}">${html}<span class="stop end"><b></b>gone</span></div>`;
  }

  // ── Today ───────────────────────────────────────────────────────────────

  function laneRow(item) {
    const done = Boolean(item.doneToday);
    const sub = [levelText(item), pips(item), item.slot !== "review" ? slotBadge(item.slot) : ""].filter(Boolean).join("");
    return `<li class="row ${done ? "done" : ""}">
      ${dot(item)}
      <button type="button" class="ttl" data-act="open" data-id="${esc(item.id)}">${titleOf(item)}</button>
      <span class="mono muted" style="font-size:12px">${done ? esc(item.doneToday.toUpperCase()) : ""}</span>
      ${sub ? `<span class="sub">${sub}</span>` : ""}
    </li>`;
  }

  function lane(title, key, data) {
    const left = data.items.filter((i) => !i.doneToday).length;
    const foot = data.overflow ? `<span>${data.overflow} rolled to tomorrow</span>` : "<span></span>";
    return `<section class="sheet lane" aria-label="${esc(title)}">
      <div class="lane-head">
        <h2>${esc(title)}</h2>
        <span class="count">${data.done}/${data.total}</span>
        <button type="button" class="primary" data-act="start" data-lane="${key}" ${left ? "" : "disabled"}>${left ? `Start ${left}` : "Done"}</button>
      </div>
      ${data.items.length
        ? `<ul class="rows">${data.items.map(laneRow).join("")}</ul>`
        : `<p class="empty">Nothing queued.</p>`}
      <div class="lane-foot">${foot}<button type="button" class="ghost" data-act="more" data-lane="${key}">Pull more</button></div>
    </section>`;
  }

  function strip(counts, label) {
    const segs = [["retired", "Gone"], ["blue", "Blue"], ["yellow", "Yellow"], ["red", "Red"]];
    const started = counts.retired + counts.blue + counts.yellow + counts.red;
    const bar = segs.map(([k]) => (counts[k] ? `<span class="seg seg-${k}" style="flex-grow:${counts[k]}"></span>` : "")).join("");
    const legend = segs.map(([k, name]) => `<li><span class="dot dot-${k}"></span>${name} <b>${counts[k]}</b></li>`).join("");
    return `<div>
      <p class="label">${esc(label)}, ${started} started of ${started + counts.new}</p>
      <div class="bar" role="img" aria-label="${segs.map(([k, name]) => `${name} ${counts[k]}`).join(", ")}">${bar}</div>
      <ul class="legend">${legend}</ul>
    </div>`;
  }

  function renderToday() {
    const today = call({ cmd: "today" });
    if (!today.ok) return "";
    ui.todayData = today;
    return `<div class="stack">
      <section class="sheet strip" aria-label="Progress">
        ${strip(today.code.counts, "Problems")}
        ${strip(today.cards.counts, "Cards")}
      </section>
      <div class="lanes">
        ${lane("Problems", "code", today.code)}
        ${lane("Cards", "cards", today.cards)}
      </div>
      <section class="sheet mockbar" aria-label="Mock interview">
        <h2>Mock interview</h2>
        <p>Two problems you have never seen, 45 minutes, no labels.</p>
        <button type="button" class="ghost" data-act="mock">Start a mock</button>
      </section>
    </div>`;
  }

  // ── Study ───────────────────────────────────────────────────────────────

  function startSession(items, options) {
    const pending = items.filter((i) => !i.doneToday);
    const list = pending.length ? pending : items;
    if (!list.length) { toast("Nothing to study here yet."); return; }
    ui.session = {
      items: list, index: 0, phase: "front", card: null, typed: "", diff: null,
      started: Date.now(), hinted: false, grade: null,
      moved: null, undoable: null, mock: false, mockEnds: 0, reviewing: false,
      ...options,
    };
    openCurrent();
    show("study");
  }

  function openCurrent() {
    const s = ui.session;
    const item = s.items[s.index];
    const response = call({ cmd: "card", id: item.id });
    if (!response.ok) return;
    s.card = { ...response.card, slot: item.slot, why: item.why };
    s.phase = "front";
    s.typed = "";
    s.diff = null;
    s.hinted = false;
    s.grade = null;
    s.started = Date.now();
  }

  function formatClock(ms) {
    const total = Math.max(0, Math.floor(ms / 1000));
    return `${String(Math.floor(total / 60)).padStart(2, "0")}:${String(total % 60).padStart(2, "0")}`;
  }

  function clockHtml(s) {
    if (s.mock) {
      const left = s.mockEnds - Date.now();
      return `<span class="clock ${left <= 0 ? "over" : ""}" id="clock">${formatClock(left)}</span>`;
    }
    if (s.card.kind !== "problem") return "";
    const spent = Date.now() - s.started;
    return `<span class="clock ${spent > s.card.timebox * 60000 ? "over" : ""}" id="clock">${formatClock(spent)}</span>`;
  }

  function frontHtml(s) {
    const c = s.card;
    if (c.kind === "problem") {
      const meta = s.mock ? "" : `<div class="flash-meta">${slotBadge(c.slot)}<span>${esc(c.level)}</span><span>${c.timebox} min box</span>${c.premium ? "<span>Premium</span>" : ""}</div>`;
      return `${meta}
        <h1 class="question"><span class="num">${c.number}</span>${esc(c.title)}</h1>
        <a class="openlink" href="${esc(c.url)}" target="_blank" rel="noopener">Open on LeetCode ↗</a>
        ${s.phase !== "front" || s.mock ? "" : s.hinted
          ? `<div class="chips">${(c.patterns || []).map((p) => `<span class="chip">${esc(p.name)}</span>`).join("")}</div>`
          : `<button type="button" class="linkbtn" style="justify-self:start" data-act="hint">Show the pattern (counts as a hint)</button>`}`;
    }
    const meta = `<div class="flash-meta"><span>${esc(c.deckTitle || c.deck)}</span>${slotBadge(c.slot)}</div>`;
    const code = c.qcode ? `<pre><code>${esc(c.qcode)}</code></pre>` : "";
    const box = c.kind === "drill"
      ? `<textarea class="drill" id="typed" spellcheck="false" autocomplete="off" autocapitalize="off" aria-label="Type your code here" placeholder="Type it from memory">${esc(s.typed)}</textarea>`
      : "";
    return `${meta}<h1 class="question">${inline(c.title)}</h1>${code}${box}`;
  }

  function diffHtml(diff) {
    const classes = ["", "miss", "extra"];
    const parts = diff.ops.map((op) => (op.s ? `<span class="${classes[op.s]}">${esc(op.t)}</span>` : esc(op.t)));
    return `<div><h3>Your code against the template, ${Math.round(diff.score * 100)}% match</h3>
      <pre class="diff"><code>${parts.join(" ")}</code></pre>
      <p class="muted" style="font-size:12.5px;margin-top:4px"><span class="diff"><span class="miss">blue</span></span> is what you left out. <span class="diff"><span class="extra">red</span></span> is what you added.</p></div>`;
  }

  function backHtml(s) {
    const c = s.card;
    const patterns = (c.patterns || []).filter(Boolean);
    const chips = patterns.length ? `<div class="chips">${patterns.map((p) => `<span class="chip">${esc(p.name)}</span>`).join("")}</div>` : "";
    let body = "";
    if (c.kind === "problem") {
      if (c.idea) body += `<div><h3>Key idea</h3><p>${inline(c.idea)}</p></div>`;
      if (c.pitfall) body += `<div><h3>Watch out for</h3><p>${inline(c.pitfall)}</p></div>`;
      if (c.time) body += `<p class="mono" style="font-size:13px">Time ${esc(c.time)} · Space ${esc(c.space)}</p>`;
    } else {
      body += `<p>${inline(c.answer)}</p>`;
      if (c.code) {
        body += s.diff ? diffHtml(s.diff) : "";
        body += `<div><h3>${c.kind === "drill" ? "Template" : "Code"}</h3><pre><code>${esc(c.code)}</code></pre></div>`;
      }
    }
    const why = c.why && c.slot !== "review" ? `<p class="why">${inline(c.why)}</p>` : "";
    return `<div class="answer flip-in" id="answer">${chips}${body}${why}</div>`;
  }

  function gradeButtons(suggest) {
    const b = (g, name, note, key) =>
      `<button type="button" class="gbtn gbtn-${g} ${suggest === g ? "hint" : ""}" data-act="grade" data-g="${g}"><span>${name} <kbd>${key}</kbd></span><small>${note}</small></button>`;
    return `<div class="grades">
      ${b("b", "Blue", "Clean, no help", "1")}
      ${b("y", "Yellow", "Small hint or a slip", "2")}
      ${b("r", "Red", "Wrong or no idea", "3")}
    </div>`;
  }

  function renderStudy() {
    const s = ui.session;
    if (!s) return "";
    if (s.phase === "finished") {
      return `<div class="study"><div class="sheet flash">
        <h1 class="question">${s.mock ? "Mock finished." : s.items.length > 1 ? "Queue cleared." : "Done."}</h1>
        ${s.items.length > 1 ? `<p class="muted" style="margin:0">${s.items.length} graded. Everything has its next check.</p>` : ""}
        ${s.moved ? `<p class="moved"><span>${esc(s.moved)}</span>${s.undoable ? `<button type="button" class="linkbtn" data-act="undo-last">Undo</button>` : ""}</p>` : ""}
        <div class="dlg-actions"><button type="button" class="primary" data-act="exit">Back to Today</button></div>
      </div></div>`;
    }
    const c = s.card;
    const percent = Math.round((s.index / s.items.length) * 100);
    const top = `<div class="study-top">
      <span>${s.index + 1}/${s.items.length}</span>
      <span class="progress"><i style="width:${percent}%"></i></span>
      ${clockHtml(s)}
      <button type="button" class="ghost" data-act="exit">${s.mock ? "End mock" : "Exit"}</button>
    </div>`;
    const moved = s.moved
      ? `<p class="moved"><span>${esc(s.moved)}</span>${s.undoable ? `<button type="button" class="linkbtn" data-act="undo-last">Undo</button>` : ""}</p>`
      : "";

    let actions;
    if (s.phase === "front") {
      const label = c.kind === "drill" ? "Check my code" : c.kind === "problem" ? "I'm done, show notes" : "Show answer";
      const keys = c.kind === "drill" ? `<kbd>⌘</kbd> <kbd>Enter</kbd> check` : `<kbd>Space</kbd> show`;
      actions = `<button type="button" class="primary reveal" data-act="reveal">${label}</button>
        <div class="keys"><span>${keys}</span>${c.kind === "problem" ? `<span><button type="button" class="linkbtn" data-act="hide">Never show this one</button></span>` : ""}</div>`;
    } else {
      actions = `${gradeButtons(s.diff ? s.diff.suggest : null)}
        <div class="keys"><span><kbd>1</kbd> <kbd>2</kbd> <kbd>3</kbd> grade</span><span><kbd>Esc</kbd> exit</span></div>`;
    }

    return `<div class="study">
      ${top}
      ${s.mock ? "" : rail(c)}
      ${moved}
      <section class="sheet flash" aria-label="Card">
        ${frontHtml(s)}
        ${s.phase === "back" ? backHtml(s) : ""}
      </section>
      ${actions}
    </div>`;
  }

  function reveal() {
    const s = ui.session;
    if (!s || s.phase !== "front") return;
    if (s.card.kind === "drill") {
      const el = $("#typed");
      if (el) s.typed = el.value;
      s.diff = call({ cmd: "diff", typed: s.typed, reference: s.card.code });
    }
    s.phase = "back";
    render();
  }

  function pickGrade(g) {
    const s = ui.session;
    if (!s || s.phase !== "back") return;
    s.grade = g;
    commit();
  }

  function commit() {
    const s = ui.session;
    const id = s.card.id;
    const response = call({
      cmd: "grade", id, grade: s.grade,
      secs: s.card.kind === "problem" ? Math.round((Date.now() - s.started) / 1000) : 0,
      hinted: s.hinted, mock: s.mock,
    });
    if (!response.ok) return;
    s.moved = `${response.after}. ${response.next}.`;
    s.undoable = id;
    if (s.index + 1 >= s.items.length) {
      s.phase = "finished";
      render();
      return;
    }
    s.index += 1;
    openCurrent();
    render();
  }

  function undoLast() {
    const s = ui.session;
    if (!s || !s.undoable) return;
    const response = call({ cmd: "undo", id: s.undoable });
    if (!response.ok) return;
    s.undoable = null;
    s.moved = null;
    if (s.phase !== "finished") s.index = Math.max(0, s.index - 1);
    openCurrent();
    render();
  }

  // ── Decks ───────────────────────────────────────────────────────────────

  function renderDecks() {
    facets = call({ cmd: "facets" });
    const off = new Set(facets.decks.filter((d) => d.off).map((d) => d.slug));
    const cards = facets.decks.map((d) => {
      const percent = d.cards ? Math.round((d.seen / d.cards) * 100) : 0;
      return `<article class="sheet deck ${off.has(d.slug) ? "off" : ""}">
        <h3>${esc(d.title)}</h3>
        <div class="meta"><span>${d.cards} cards</span><span>${d.seen} seen</span>${d.due ? `<span>${d.due} due</span>` : ""}</div>
        <span class="meter"><i style="width:${percent}%"></i></span>
        <div class="foot">
          <button type="button" class="ghost" data-act="toggle-deck" data-deck="${esc(d.slug)}" aria-pressed="${!off.has(d.slug)}">${off.has(d.slug) ? "Not in Today" : "In Today"}</button>
          <button type="button" class="primary" data-act="study-deck" data-deck="${esc(d.slug)}">Study</button>
        </div>
      </article>`;
    }).join("");
    return `<div class="deckgrid">${cards || `<p class="empty">No decks in this build.</p>`}</div>`;
  }

  // ── Library ─────────────────────────────────────────────────────────────

  function libraryRow(card) {
    const due = dueText(card);
    const today = card.doneToday;
    const button = (g) => `<button type="button" class="g g-${g} ${today === g ? "on" : ""}" data-act="quick" data-id="${esc(card.id)}" data-g="${g}" aria-label="Grade ${g.toUpperCase()}: ${esc(card.title)}" aria-pressed="${today === g}">${g.toUpperCase()}</button>`;
    const undo = today
      ? `<button type="button" class="undo" data-act="quick-undo" data-id="${esc(card.id)}" aria-label="Undo today's grade" title="Undo today's grade">↺</button>`
      : `<span class="undo-slot"></span>`;
    return `<li class="librow">
      ${dot(card)}
      <button type="button" class="ttl" data-act="open" data-id="${esc(card.id)}">${titleOf(card)}${card.premium ? ` <span class="badge">Premium</span>` : ""}</button>
      <span class="level">${card.kind === "problem" ? esc(card.level) : ""}</span>
      <span class="nextin ${due.now ? "now" : ""}">${esc(due.text)}</span>
      <span class="mini">${button("b")}${button("y")}${button("r")}${undo}</span>
    </li>`;
  }

  function libraryRequest() {
    const f = ui.library;
    const request = { cmd: "library", kind: f.kind, q: f.q, status: f.status, limit: f.limit, curated: f.curated };
    if (f.pattern) request.pattern = f.pattern;
    return request;
  }

  function renderLibrary() {
    const f = ui.library;
    if (!facets) facets = call({ cmd: "facets" });
    const data = call(libraryRequest());
    const option = (value, label, current) => `<option value="${esc(value)}" ${value === current ? "selected" : ""}>${esc(label)}</option>`;
    const statuses = [["", "Any status"], ["due", "Due now"], ["new", "New"], ["red", "Red"], ["yellow", "Yellow"], ["blue", "Blue"], ["retired", "Gone"]];
    const seg = (key, label) => `<button type="button" data-act="lib-kind" data-v="${key}" aria-pressed="${f.kind === key}">${label}</button>`;
    return `<div class="stack">
      <div class="filters">
        <div class="segbtns" role="group" aria-label="Kind">${seg("problem", "Problems")}${seg("card", "Cards")}</div>
        <div class="segbtns" role="group" aria-label="List">
          <button type="button" data-act="lib-curated" data-v="" aria-pressed="${!f.curated}">All</button>
          <button type="button" data-act="lib-curated" data-v="1" aria-pressed="${f.curated}">My list</button>
        </div>
        <select id="lib-status" aria-label="Status">${statuses.map(([v, l]) => option(v, l, f.status)).join("")}</select>
        <select id="lib-pattern" aria-label="Pattern"><option value="">Any pattern</option>${facets.patterns.map((p) => option(p.slug, p.name, f.pattern)).join("")}</select>
        <input id="lib-search" type="search" placeholder="Search title or number" aria-label="Search" autocomplete="off" value="${esc(f.q)}">
      </div>
      <ul class="sheet rows lib" id="lib-rows">${data.rows.map(libraryRow).join("") || `<li class="pad empty">Nothing matches these filters.</li>`}</ul>
      <div class="libfoot"><span>${data.rows.length} of ${data.total}</span>${data.rows.length < data.total ? `<button type="button" class="ghost" data-act="lib-more">Show more</button>` : ""}</div>
    </div>`;
  }

  // ── Insights ────────────────────────────────────────────────────────────

  function rateCell(evidence) {
    if (evidence.rate === undefined) return `<span class="muted">–</span>`;
    const percent = Math.round(evidence.rate * 100);
    return `<span class="cell" title="${percent}% from ${evidence.n} weighted attempts"><span class="meter"><i style="width:${percent}%"></i></span>${percent}%</span>`;
  }

  function renderInsights() {
    const data = call({ cmd: "insights" });
    if (!data.ok) return "";
    if (!data.graded) return `<div class="sheet pad"><p class="empty">Grade a few problems. Your patterns show up here.</p></div>`;
    const notes = data.notes.length
      ? `<section class="sheet pad"><ul class="notes">${data.notes.map((n) => `<li class="${esc(n.kind)}">${esc(n.text)}</li>`).join("")}</ul></section>`
      : "";
    const needs = data.needs.length
      ? `<ul class="hbars">${data.needs.map((a) => `<li><span>${esc(a.label)}</span><b>${Math.round(a.share * 100)}%</b><span class="meter"><i style="width:${Math.round(a.share * 100)}%"></i></span></li>`).join("")}</ul>`
      : `<p class="empty">Your coach fills this in after it reviews code you saved before running it.</p>`;
    const rows = data.patterns.map((p) => `<tr>
      <td><button type="button" class="linkbtn" data-act="drill-pattern" data-pattern="${esc(p.slug)}" title="Study this pattern's cards">${esc(p.name)}</button>${p.memorizing ? `<span class="flagged">Memorizing</span>` : ""}</td>
      <td>${rateCell(p.cold)}</td><td>${rateCell(p.review)}</td><td>${rateCell(p.drill)}</td><td>${rateCell(p.flash)}</td>
      <td class="mono" style="font-size:12.5px">${p.seen}/${p.total}</td>
    </tr>`).join("");
    return `<div class="stack">
      ${notes}
      <section class="sheet pad"><p class="label">What your judged code shows most</p>${needs}</section>
      <section class="sheet pad">
        <p class="label">Patterns, pass rate by kind of attempt</p>
        <div class="tablewrap"><table class="grid">
          <thead><tr><th>Pattern</th><th>New face</th><th>Review</th><th>Typed</th><th>Cards</th><th>Seen</th></tr></thead>
          <tbody>${rows}</tbody>
        </table></div>
      </section>
    </div>`;
  }

  // ── Settings ────────────────────────────────────────────────────────────

  function openSettings() {
    const s = call({ cmd: "settings" }).settings;
    const number = (key, label) => `<label class="field">${label}<input type="number" id="set-${key}" min="0" max="200" value="${s[key]}"></label>`;
    const companyOptions = ['<option value="">Any company</option>']
      .concat(facets.companies.map((c) => `<option value="${esc(c.slug)}" ${c.slug === s.company ? "selected" : ""}>${esc(c.name)}</option>`)).join("");
    const dialog = $("#settings");
    dialog.innerHTML = `<form class="dlg" id="settings-form" method="dialog">
      <div class="dlg-head"><h2>Settings</h2><button type="button" class="ghost" data-act="close-settings">Close</button></div>
      <div class="fields">
        ${number("codeCap", "Problems a day")}
        ${number("codeNew", "New problems a day")}
        ${number("codeReviews", "Reviews a day")}
        ${number("cardCap", "Cards a day")}
        ${number("cardNew", "New cards a day")}
        <label class="field">Days that count
          <select id="set-clock">
            <option value="study" ${s.clock === "study" ? "selected" : ""}>Only days I study</option>
            <option value="calendar" ${s.clock === "calendar" ? "selected" : ""}>Every calendar day</option>
          </select>
        </label>
        <label class="field">Target company<select id="set-company">${companyOptions}</select></label>
        <label class="field">Interview date<input type="date" id="set-interviewDate" value="${esc(s.interviewDate || "")}"></label>
      </div>
      <p class="muted" style="margin:0;font-size:13.5px">${s.exploreToday != null
        ? `New ground today: ${Math.round(s.exploreToday * 100)}% of new problems, easing to ${Math.round(s.exploreEnd * 100)}% by your interview.`
        : "With no interview date, every pattern gets a first try, then one new problem in four goes to a topic you haven't done."}</p>
      <label class="check"><input type="checkbox" id="set-premium" ${s.premium ? "checked" : ""}> Recommend LeetCode Premium problems</label>
      <div class="dlg-actions"><button type="submit" class="primary">Save settings</button></div>
      <hr style="border:0;border-top:1px solid var(--line);width:100%;margin:0">
      <p class="muted" style="margin:0;font-size:13.5px">${remote ? "Progress saves to your Claude account." : "Progress saves in this browser only. Copy it below to keep a backup."}</p>
      <label class="field">Your progress, as text
        <textarea id="set-backup" spellcheck="false" aria-label="Progress as JSON"></textarea>
      </label>
      <div class="dlg-actions">
        <button type="button" class="ghost" data-act="export">Show and copy</button>
        <button type="button" class="ghost" data-act="import">Replace with pasted text</button>
      </div>
    </form>`;
    dialog.showModal();
  }

  function saveSettings() {
    const value = (id) => $("#set-" + id).value;
    call({ cmd: "settings", set: {
      codeCap: Number(value("codeCap")), codeNew: Number(value("codeNew")), codeReviews: Number(value("codeReviews")),
      cardCap: Number(value("cardCap")), cardNew: Number(value("cardNew")),
      clock: value("clock"), company: value("company"), premium: $("#set-premium").checked,
      interviewDate: value("interviewDate"),
    } });
    $("#settings").close();
    refresh();
  }

  async function exportState() {
    const text = core.saveState();
    $("#set-backup").value = text;
    try {
      await navigator.clipboard.writeText(text);
      toast("Copied your progress.");
    } catch (e) {
      $("#set-backup").select();
      toast("Selected. Press ⌘C to copy it.");
    }
  }

  function importState() {
    const text = $("#set-backup").value.trim();
    if (!text) { toast("Paste your saved progress first."); return; }
    if (core.loadState(text) !== 0) { toast("That text isn't saved progress: " + core.lastError()); return; }
    flush();
    $("#settings").close();
    toast("Progress loaded.");
    refresh();
  }

  // ── Rendering and events ────────────────────────────────────────────────

  function show(view) {
    ui.view = view;
    render();
    window.scrollTo(0, 0);
  }

  function render() {
    const views = { today: renderToday, decks: renderDecks, library: renderLibrary, insights: renderInsights, study: renderStudy };
    $("#view").innerHTML = views[ui.view]();
    document.querySelectorAll(".tabs button").forEach((b) => {
      if (b.dataset.view === ui.view) b.setAttribute("aria-current", "page");
      else b.removeAttribute("aria-current");
    });
    if (ui.view === "study" && ui.session && ui.session.phase === "front" && ui.session.card && ui.session.card.kind === "drill") {
      const area = $("#typed");
      if (area) area.focus();
    }
    tick();
  }

  function refresh() {
    if (core) render();
  }

  // Only the clock changes every second, so only the clock is touched.
  function tick() {
    const s = ui.session;
    const el = $("#clock");
    if (ui.view !== "study" || !s || !el || !s.card) return;
    if (s.mock) {
      const left = s.mockEnds - Date.now();
      el.textContent = formatClock(left);
      el.classList.toggle("over", left <= 0);
    } else {
      const spent = Date.now() - s.started;
      el.textContent = formatClock(spent);
      el.classList.toggle("over", spent > s.card.timebox * 60000);
    }
  }
  setInterval(tick, 1000);

  // Keep the page honest if it stays open past the day rollover.
  let lastDay = null;
  setInterval(() => {
    if (!core) return;
    const day = nowString().slice(0, 10);
    if (lastDay && day !== lastDay && ui.view === "today") refresh();
    lastDay = day;
  }, 60000);

  function openOne(id) {
    const response = call({ cmd: "card", id });
    if (!response.ok) return;
    startSession([{ ...response.card, slot: "", why: "" }], {});
  }

  document.addEventListener("click", (event) => {
    const button = event.target.closest("[data-act]");
    if (!button || button.disabled) return;
    const act = button.dataset.act;
    const s = ui.session;

    if (act === "nav") { ui.session = null; show(button.dataset.view); }
    else if (act === "settings") openSettings();
    else if (act === "close-settings") $("#settings").close();
    else if (act === "export") exportState();
    else if (act === "import") importState();
    else if (act === "start") {
      const key = button.dataset.lane;
      const items = ui.todayData[key].items;
      startSession(items, {});
    }
    else if (act === "more") {
      call({ cmd: "more", lane: button.dataset.lane });
      render();
    }
    else if (act === "mock") {
      const roll = Math.floor(Date.now() / 1000) % 100000;
      const mock = call({ cmd: "mock", roll });
      if (mock.ok) startSession(mock.items, { mock: true, mockEnds: Date.now() + mock.minutes * 60000 });
    }
    else if (act === "open") openOne(button.dataset.id);
    else if (act === "reveal") reveal();
    else if (act === "hint") { s.hinted = true; render(); }
    else if (act === "grade") pickGrade(button.dataset.g);
    else if (act === "undo-last") undoLast();
    else if (act === "hide") {
      call({ cmd: "hide", id: s.card.id });
      toast("It won't be recommended again.");
      s.items.splice(s.index, 1);
      if (s.index >= s.items.length) { ui.session = null; show("today"); }
      else { openCurrent(); render(); }
    }
    else if (act === "exit") { ui.session = null; show("today"); }
    else if (act === "drill-pattern") {
      const session = call({ cmd: "session", pattern: button.dataset.pattern, n: 20 });
      if (session.ok) startSession(session.items, {});
    }
    else if (act === "study-deck") {
      const session = call({ cmd: "session", deck: button.dataset.deck, n: 20 });
      if (session.ok) startSession(session.items, {});
    }
    else if (act === "toggle-deck") {
      const off = new Set(facets.decks.filter((d) => d.off).map((d) => d.slug));
      const slug = button.dataset.deck;
      if (off.has(slug)) off.delete(slug); else off.add(slug);
      call({ cmd: "settings", set: { decksOff: [...off] } });
      render();
    }
    else if (act === "lib-kind") { ui.library.kind = button.dataset.v; ui.library.limit = 100; render(); }
    else if (act === "lib-curated") { ui.library.curated = Boolean(button.dataset.v); ui.library.limit = 100; render(); }
    else if (act === "lib-more") { ui.library.limit = Math.min(500, ui.library.limit + 100); render(); }
    else if (act === "quick") { call({ cmd: "grade", id: button.dataset.id, grade: button.dataset.g }); updateLibraryRows(); }
    else if (act === "quick-undo") { call({ cmd: "undo", id: button.dataset.id }); updateLibraryRows(); }
  });

  // Redraws only the rows, so typing in the search box keeps its focus.
  function updateLibraryRows() {
    const data = call(libraryRequest());
    const list = $("#lib-rows");
    if (list) list.innerHTML = data.rows.map(libraryRow).join("") || `<li class="pad empty">Nothing matches these filters.</li>`;
    const foot = document.querySelector(".libfoot");
    if (foot) foot.innerHTML = `<span>${data.rows.length} of ${data.total}</span>${data.rows.length < data.total ? `<button type="button" class="ghost" data-act="lib-more">Show more</button>` : ""}`;
  }

  document.addEventListener("input", (event) => {
    const target = event.target;
    if (target.id === "lib-search") { ui.library.q = target.value; ui.library.limit = 100; updateLibraryRows(); }
    else if (target.id === "typed" && ui.session) ui.session.typed = target.value;
  });

  document.addEventListener("change", (event) => {
    const target = event.target;
    if (target.id === "lib-status") { ui.library.status = target.value; ui.library.limit = 100; updateLibraryRows(); }
    else if (target.id === "lib-pattern") { ui.library.pattern = target.value; ui.library.limit = 100; updateLibraryRows(); }
  });

  document.addEventListener("submit", (event) => {
    if (event.target.id === "settings-form") {
      event.preventDefault();
      saveSettings();
    }
  });

  document.addEventListener("keydown", (event) => {
    if (ui.view !== "study" || !ui.session) return;
    if ($("#settings").open) return;
    const s = ui.session;
    const inField = event.target.matches("textarea, input, select");

    // Esc while typing only leaves the editor, so it can't throw away a drill.
    if (event.key === "Escape") {
      event.preventDefault();
      if (inField) { event.target.blur(); return; }
      ui.session = null;
      show("today");
      return;
    }
    if (s.phase === "front" && event.key === "Enter" && (event.metaKey || event.ctrlKey)) {
      event.preventDefault();
      reveal();
      return;
    }
    if (inField) {
      if (event.key === "Tab" && event.target.id === "typed") {
        event.preventDefault();
        const t = event.target;
        const at = t.selectionStart;
        t.setRangeText("    ", at, t.selectionEnd, "end");
        s.typed = t.value;
      }
      return;
    }
    if (s.phase === "front" && (event.key === " " || event.key === "Enter")) {
      if (event.target.closest("a, button")) return;
      event.preventDefault();
      reveal();
    } else if (s.phase === "back") {
      const grade = { 1: "b", 2: "y", 3: "r", b: "b", y: "y", r: "r" }[event.key];
      if (grade) { event.preventDefault(); pickGrade(grade); }
    }
    if (event.key === "u" && s.undoable) undoLast();
  });

  // ── Boot ────────────────────────────────────────────────────────────────

  async function boot() {
    try {
      const encoded = $("#wasm").textContent.trim();
      const bytes = Uint8Array.from(atob(encoded), (c) => c.charCodeAt(0));
      core = await loadCore(bytes);
      if (core.loadResources($("#resources").textContent) < 0) throw new Error(core.lastError());
      const saved = local.read();
      if (saved) core.loadState(saved);
      facets = core.call({ now: nowString(), cmd: "facets" });
      render();
      connectRemote();
    } catch (e) {
      $("#view").innerHTML = `<p class="empty">The app couldn't start: ${esc(e.message)}. Try reloading, or use a current browser.</p>`;
    }
  }

  boot();
})();
