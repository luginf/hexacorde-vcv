// Lecture et écriture des setups JSON d'Hexacorde (même format et mêmes vérifications que src/js/15-setup.js).
// Inclus à la fin de hexacorde.hpp.
#pragma once

namespace hexa {

namespace detail {
struct Speed { double v; const char* code; };
inline const Speed* speeds() {
  static const Speed t[] = {{0.125, "/8"}, {0.25, "/4"}, {1.0 / 3, "/3"}, {0.5, "/2"}, {1, "x1"}, {2, "x2"}, {3, "x3"}};
  return t;
}
inline const char* speedCode(double v) {
  const Speed* t = speeds();
  for (int i = 0; i < 7; i++) if (std::fabs(t[i].v - v) < 1e-6) return t[i].code;
  return t[4].code;
}
inline double clampd(double v, double lo, double hi) { return std::min(hi, std::max(lo, v)); }
inline double numOr(const Json* j, double lo, double hi, double def) {
  return (j && j->isNum() && std::isfinite(j->num)) ? clampd(j->num, lo, hi) : def;
}
inline bool bitString(const Json* j, std::vector<int>& out) {       // 6 ou 7 chiffres 0 / 1
  if (!j || !j->isStr() || (j->str.size() != 6 && j->str.size() != 7)) return false;
  out.clear();
  for (char c : j->str) { if (c != '0' && c != '1') return false; out.push_back(c - '0'); }
  return true;
}
inline Sound parseSound(const Json* so, const Sound& def) {
  Sound r = def;
  if (!so || !so->isObj()) return r;
  const Json* w = so->get("onde");
  static const char* waves[] = {"square", "sawtooth", "triangle", "sine", "fm"};
  if (w && w->isStr()) for (const char* x : waves) if (w->str == x) r.wave = x;
  r.a = int(jsRound(numOr(so->get("a"), 1, 500, def.a)));
  r.d = int(jsRound(numOr(so->get("d"), 10, 1000, def.d)));
  r.s = numOr(so->get("s"), 0, 1, def.s);
  r.r = int(jsRound(numOr(so->get("r"), 10, 1500, def.r)));
  const Json* f = so->get("filtre");
  static const char* filters[] = {"lowpass", "highpass", "bandpass", "notch"};
  if (f && f->isStr()) for (const char* x : filters) if (f->str == x) r.ftype = x;
  r.fcut = int(jsRound(numOr(so->get("coupure"), 30, 16000, def.fcut)));
  r.fq = numOr(so->get("q"), 0.1, 20, def.fq);
  r.fenv = numOr(so->get("env"), 0, 1, def.fenv);
  r.fmr = numOr(so->get("fm"), 0.5, 12, def.fmr);
  r.fmi = numOr(so->get("indice"), 0, 12, def.fmi);
  return r;
}
inline Json soundToJson(const Sound& x) {
  Json o = Json::object();
  o.set("onde", Json::string(x.wave)); o.set("a", Json::number(x.a)); o.set("d", Json::number(x.d));
  o.set("s", Json::number(x.s)); o.set("r", Json::number(x.r)); o.set("filtre", Json::string(x.ftype));
  o.set("coupure", Json::number(x.fcut)); o.set("q", Json::number(x.fq)); o.set("env", Json::number(x.fenv));
  if (x.wave == "fm") { o.set("fm", Json::number(x.fmr)); o.set("indice", Json::number(x.fmi)); }
  return o;
}
}  // namespace detail

// vérifie tout avant de toucher à l'état ; en cas d'erreur, l'état n'est pas modifié
inline bool Engine::loadSetup(const std::string& text, std::string* err) {
  using namespace detail;
  auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
  Json d;
  if (!Json::parse(text, d)) {                                      // tolère du texte autour de l'objet
    size_t a = text.find('{'), b = text.rfind('}');
    if (a == std::string::npos || b == std::string::npos || b < a || !Json::parse(text.substr(a, b - a + 1), d))
      return fail("unreadable text (invalid JSON)");
  }
  const Json* ver = d.get("hexacorde");
  const Json* hx = d.get("hexagrammes");
  if (!d.isObj() || !ver || !ver->isNum() || ver->num != 1 || !hx || !hx->isArr()) return fail("this is not a Hexacorde setup");
  const Json* gr = d.get("grille");
  double gc = 6, gr2 = 6;
  if (gr && gr->isArr()) {
    if (gr->arr.size() > 0 && gr->arr[0].isNum()) gc = gr->arr[0].num;
    if (gr->arr.size() > 1 && gr->arr[1].isNum()) gr2 = gr->arr[1].num;
  } else if (gr && gr->isNum()) gc = gr2 = gr->num;
  int ncols = int(jsRound(clampd(gc, N_MIN, N_MAX))), nrows = int(jsRound(clampd(gr2, N_MIN, N_MAX)));
  if (static_cast<int>(hx->arr.size()) > static_cast<int>(LABELS.size())) return fail("too many hexagrams");

  std::vector<SlotInit> list;
  std::vector<char> usedLabels;
  std::vector<std::pair<int, int>> usedPos;
  for (size_t i = 0; i < hx->arr.size(); i++) {
    const Json& x = hx->arr[i];
    std::string L = "#" + std::to_string(i + 1);
    char label = 0;
    const Json* lj = x.get("lettre");
    if (lj && lj->isStr() && lj->str.size() == 1 && LABELS.find(lj->str[0]) != std::string::npos &&
        std::find(usedLabels.begin(), usedLabels.end(), lj->str[0]) == usedLabels.end()) {
      label = lj->str[0]; usedLabels.push_back(label); L = lj->str;
    }
    const Json* pos = x.get("pos");
    if (!pos || !pos->isArr() || pos->arr.size() != 2 || !pos->arr[0].isInt() || !pos->arr[1].isInt() ||
        pos->arr[0].num < 0 || pos->arr[0].num >= ncols || pos->arr[1].num < 0 || pos->arr[1].num >= nrows)
      return fail(L + ": invalid position");
    std::pair<int, int> pp(int(pos->arr[0].num), int(pos->arr[1].num));
    if (std::find(usedPos.begin(), usedPos.end(), pp) != usedPos.end()) return fail(L + ": position already taken");
    usedPos.push_back(pp);
    SlotInit c; c.gx = pp.first; c.gy = pp.second; c.label = label ? label : -1;
    const Json* nj = x.get("numero");
    if (!bitString(x.get("traits"), c.lines)) {
      if (nj && nj->isInt() && hexLinesTable().count(int(nj->num))) c.lines = hexLinesTable().at(int(nj->num));
      else return fail(L + ": \"traits\" (6 or 7 digits) or \"numero\" (1 to 64) expected");
    }
    std::vector<int> ini;
    if (bitString(x.get("depart"), ini) && ini.size() == c.lines.size()) c.initial = ini; else c.initial = c.lines;
    const Json* g = x.get("gamme");
    c.mode = "chromatique";
    if (g) { if (!g->isStr() || !scaleKnown(g->str)) return fail(L + ": unknown scale"); c.mode = g->str; }
    const Json* cj = x.get("canal");
    int canal = label ? int(LABELS.find(label)) % 16 + 1 : int(i) % 16 + 1;
    if (cj) { if (!cj->isInt() || cj->num < 1 || cj->num > 16) return fail(L + ": channel from 1 to 16"); canal = int(cj->num); }
    c.channel = canal - 1;
    const Json* r = x.get("rotation");
    std::string vit = "x1";
    if (r && r->get("vitesse")) { const Json* v = r->get("vitesse"); vit = v->isStr() ? v->str : "?"; }
    const Speed* sp = nullptr;
    for (int k = 0; k < 7; k++) if (vit == speeds()[k].code) sp = &speeds()[k];
    if (!sp) return fail(L + ": unknown speed");
    double angle = 0;
    if (r && r->get("angle")) { const Json* a = r->get("angle"); if (!a->isInt() || std::fmod(a->num, 60) != 0) return fail(L + ": angle must be a multiple of 60"); angle = a->num; }
    const Json* nt = x.get("notes");
    if (nt) {
      const Json* pl = nt->get("plein"); const Json* br = nt->get("brise");
      auto ok = [&](const Json* a) {
        if (!a || !a->isArr() || a->arr.size() != c.lines.size()) return false;
        for (const auto& v : a->arr) if (!v.isNull() && !(v.isInt() && v.num >= 0 && v.num < 24)) return false;
        return true;
      };
      if (!ok(pl) || !ok(br)) return fail(L + ": notes: \"plein\" and \"brise\" need one value per line (0 to 23, null = silence)");
      for (size_t k = 0; k < c.lines.size(); k++)
        c.custom.push_back({pl->arr[k].isNull() ? -1 : int(pl->arr[k].num), br->arr[k].isNull() ? -1 : int(br->arr[k].num)});
      const Json* act = nt->get("active");
      c.customOn = act && ((act->type == Json::Bool && act->b) || (act->isNum() && act->num != 0) || (act->isStr() && !act->str.empty()));
    }
    const Json* tj = x.get("tonique");
    c.tonic = (tj && tj->isInt() && tj->num >= 0 && tj->num < 12) ? int(tj->num) : -1;
    const Json* oj = x.get("octave");
    c.octave = (oj && oj->isInt()) ? int(clampd(oj->num, -3, 3)) : 0;
    const Json* aj = x.get("amorce");
    const Json* legacy = d.get("amorce");
    const Json* am = aj ? aj : (i == 0 ? legacy : nullptr);
    c.loopOn = am && am->isNum() && std::isfinite(am->num) && am->num >= 2;
    c.loopN = c.loopOn ? std::min(128, int(jsRound(am->num))) : 24;
    const Json* ac = r ? r->get("active") : nullptr;
    c.rotOn = ac && ((ac->type == Json::Bool && ac->b) || (ac->isNum() && ac->num != 0) || (ac->isStr() && !ac->str.empty()));
    const Json* sj = r ? r->get("sens") : nullptr;
    c.rotDir = (sj && sj->isStr() && sj->str == "antihoraire") ? -1 : 1;
    c.rotSpeed = sp->v;
    c.rot = modn(int(angle / 60), static_cast<int>(c.lines.size()));
    const Json* aj2 = x.get("actif");
    c.active = !(aj2 && aj2->type == Json::Bool && !aj2->b);
    list.push_back(c);
  }
  Sound base = parseSound(d.get("son"), Sound());
  Sound chanS[16]; bool hasS[16] = {};
  if (const Json* sons = d.get("sons")) if (sons->isObj())
    for (const auto& kv : sons->obj) {
      char* e = nullptr; long k = std::strtol(kv.first.c_str(), &e, 10);
      if (*e == 0 && k >= 1 && k <= 16) { chanS[k - 1] = parseSound(&kv.second, base); hasS[k - 1] = true; }
    }
  int prog[16]; for (int& p : prog) p = -1;
  if (const Json* pg = d.get("programmes")) if (pg->isObj())
    for (const auto& kv : pg->obj) {
      char* e = nullptr; long k = std::strtol(kv.first.c_str(), &e, 10);
      if (*e == 0 && k >= 1 && k <= 16 && kv.second.isInt() && kv.second.num >= 0 && kv.second.num <= 127) prog[k - 1] = int(kv.second.num);
    }
  const Json* gm = d.get("gamme");
  std::string scale = (gm && gm->isStr() && scaleKnown(gm->str)) ? gm->str : "chromatique";
  const Json* cu = d.get("canalUnique");
  int chanAll = (cu && cu->isInt() && cu->num >= 1 && cu->num <= 16) ? int(cu->num) - 1 : -1;
  const Json* un = d.get("uniforme");
  bool uniform = un && ((un->type == Json::Bool && un->b) || (un->isNum() && un->num != 0));
  const Json* mu = d.get("mutation");
  bool mutate = mu && ((mu->type == Json::Bool && mu->b) || (mu->isNum() && mu->num != 0));

  // tout est valide : on applique
  silence();
  tickMs = int(jsRound(numOr(d.get("pas"), 80, 700, 240)));
  opt.tonic = int(jsRound(numOr(d.get("tonique"), 0, 11, 0)));
  opt.octave = int(jsRound(numOr(d.get("octave"), -3, 3, 0)));
  opt.softRange = numOr(d.get("portee"), 1, 9, 2.5);
  opt.mutate = mutate; opt.scale = scale; opt.uniform = uniform; opt.chanAll = chanAll;
  sound = base;
  for (int k = 0; k < 16; k++) { hasChanSound[k] = hasS[k]; chanSound[k] = chanS[k]; program[k] = prog[k]; }
  slots.clear(); pulses.clear(); tick = 0; slotSeq = 0;
  cols = ncols; rows = nrows;
  for (const auto& c : list) addSlot(c);
  return true;
}

inline Json Engine::toJson(const std::string& date) const {
  using namespace detail;
  Json o = Json::object();
  o.set("hexacorde", Json::number(1));
  if (!date.empty()) o.set("date", Json::string(date));
  Json gr = Json::array(); gr.push(Json::number(cols)); gr.push(Json::number(rows));
  o.set("grille", gr);
  o.set("gamme", Json::string(opt.scale));
  if (opt.chanAll >= 0) o.set("canalUnique", Json::number(opt.chanAll + 1));
  if (opt.uniform) o.set("uniforme", Json::boolean(true));
  o.set("pas", Json::number(tickMs)); o.set("tonique", Json::number(opt.tonic)); o.set("octave", Json::number(opt.octave));
  o.set("portee", Json::number(opt.softRange)); o.set("mutation", Json::boolean(opt.mutate));
  o.set("son", soundToJson(sound));
  bool anyS = false, anyP = false;
  for (int k = 0; k < 16; k++) { anyS |= hasChanSound[k]; anyP |= program[k] >= 0; }
  if (anyS) { Json s = Json::object(); for (int k = 0; k < 16; k++) if (hasChanSound[k]) s.set(std::to_string(k + 1), soundToJson(chanSound[k])); o.set("sons", s); }
  if (anyP) { Json p = Json::object(); for (int k = 0; k < 16; k++) if (program[k] >= 0) p.set(std::to_string(k + 1), Json::number(program[k])); o.set("programmes", p); }
  Json hs = Json::array();
  for (const auto& s : slots) {
    Json h = Json::object();
    h.set("lettre", Json::string(std::string(1, s.label)));
    Json pos = Json::array(); pos.push(Json::number(s.gx)); pos.push(Json::number(s.gy)); h.set("pos", pos);
    std::string tr, de;
    for (int l : s.lines) tr += char('0' + l);
    for (int l : s.initial) de += char('0' + l);
    h.set("traits", Json::string(tr)); h.set("depart", Json::string(de));
    h.set("gamme", Json::string(s.mode)); h.set("canal", Json::number(s.channel + 1));
    Json r = Json::object();
    r.set("active", Json::boolean(s.rotOn)); r.set("sens", Json::string(s.rotDir > 0 ? "horaire" : "antihoraire"));
    r.set("vitesse", Json::string(speedCode(s.rotSpeed))); r.set("angle", Json::number(modn(s.rot, s.n) * 60));
    h.set("rotation", r);
    if (!s.active) h.set("actif", Json::boolean(false));
    if (s.tonic >= 0) h.set("tonique", Json::number(s.tonic));
    if (s.octave) h.set("octave", Json::number(s.octave));
    if (s.loopOn) h.set("amorce", Json::number(s.loopN));
    if (s.customOn) {
      Json nt = Json::object(), pl = Json::array(), br = Json::array();
      nt.set("active", Json::boolean(true));
      for (const auto& c : s.custom) { pl.push(c.p < 0 ? Json() : Json::number(c.p)); br.push(c.b < 0 ? Json() : Json::number(c.b)); }
      nt.set("plein", pl); nt.set("brise", br); h.set("notes", nt);
    }
    hs.push(h);
  }
  o.set("hexagrammes", hs);
  return o;
}

// JSON lisible : un hexagramme par ligne, comme l'application web
inline std::string Engine::saveSetup(const std::string& date) const {
  Json o = toJson(date);
  std::string head, out;
  for (const auto& kv : o.obj) {
    if (kv.first == "hexagrammes") continue;
    if (!head.empty()) head += ',';
    Json::quote(kv.first, head); head += ':'; kv.second.write(head);
  }
  out = "{" + head + ",\n\"hexagrammes\": [\n";
  const Json* hs = o.get("hexagrammes");
  for (size_t i = 0; i < hs->arr.size(); i++) { out += "  " + hs->arr[i].dump(); out += i + 1 < hs->arr.size() ? ",\n" : "\n"; }
  return out + "]}";
}

}  // namespace hexa
