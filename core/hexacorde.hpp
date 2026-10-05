// Hexacorde : cœur du séquenceur, en C++17 sans dépendance (portage fidèle de src/js/01 à 06 et 15).
// Aucune interface, aucun son, aucun MIDI : le moteur calcule les notes de chaque pas (Engine::step)
// et lit / écrit les setups JSON. Les coques (module VCV Rack, plugin JUCE...) s'en servent.
#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "json.hpp"
#include "tables.hpp"

namespace hexa {

constexpr int N_MIN = 3, N_MAX = 12, MAX_SLOTS = 26, MAX_PULSES = 300;
inline const std::string LABELS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr double INF = std::numeric_limits<double>::infinity();

inline int modn(int a, int n) { return ((a % n) + n) % n; }
inline double modnd(double a, double n) { return std::fmod(std::fmod(a, n) + n, n); }
inline double jsRound(double x) { return std::floor(x + 0.5); }          // Math.round de JavaScript
inline double sideAng(int k, int n) { return 360.0 * k / n; }             // degrés depuis le haut, sens horaire

// ---------- hexagrammes ----------
inline int hexNumber(const std::vector<int>& lines) {                      // les 6 premiers traits
  std::string a, b;
  for (int i = 0; i < 3; i++) a += char('0' + lines[i]);
  for (int i = 3; i < 6; i++) b += char('0' + lines[i]);
  return kwTable()[trigramIndex(a)][trigramIndex(b)];
}
inline const std::map<int, std::vector<int>>& hexLinesTable() {
  static const std::map<int, std::vector<int>> t = [] {
    std::map<int, std::vector<int>> m;
    for (int a = 0; a < 64; a++) {
      std::vector<int> l;
      for (int i = 5; i >= 0; i--) l.push_back((a >> i) & 1);
      m[hexNumber(l)] = l;
    }
    return m;
  }();
  return t;
}

// ---------- sons (données seulement ; la synthèse est l'affaire de la coque) ----------
struct Sound {
  std::string wave = "square";
  int a = 5, d = 250, r = 150;
  double s = 0.1;
  std::string ftype = "lowpass";
  int fcut = 3200;
  double fq = 0.7, fenv = 0, fmr = 2, fmi = 3;
  bool operator==(const Sound& o) const {
    return wave == o.wave && a == o.a && d == o.d && r == o.r && s == o.s && ftype == o.ftype && fcut == o.fcut &&
           fq == o.fq && fenv == o.fenv && fmr == o.fmr && fmi == o.fmi;
  }
};

// ---------- hexagone ----------
struct Custom {
  int p = 0, b = 0;                  // décalages en demi-tons (0 à 23), -1 = silence
  bool operator==(const Custom& o) const { return p == o.p && b == o.b; }
};
struct Slot {
  int id = 0;                        // numéro unique, jamais réutilisé
  char label = 'A';
  int gx = 0, gy = 0;
  std::vector<int> lines, initial;   // 1 = trait plein, 0 = brisé ; 6 ou 7 traits
  int n = 6;
  std::string mode = "chromatique";
  int channel = 0;                   // 0..15
  bool active = true;
  int tonic = -1;                    // -1 = tonique globale
  int octave = 0;
  bool loopOn = false;
  int loopN = 24;
  bool customOn = false;
  std::vector<Custom> custom;
  int rot = 0;
  bool rotOn = false;
  int rotDir = 1;
  double rotSpeed = 1, rotAcc = 0;
  int rotDelta = 0;
  bool phOn = false;                 // tête de lecture (le « playhead »)
  int phSide = 0, phLeft = 0;
};

struct Options {
  double softRange = 2.5;
  bool mutate = false;
  int tonic = 0, octave = 0;
  std::string scale = "chromatique";   // gamme de base des nouveaux hexagones
  bool uniform = false;
  int chanAll = -1;                    // canal commun, -1 = individuels
};

struct Pulse {
  double x0, y0, x1, y1;
  int t0, t1;
  int target;                          // id de l'hexagone visé, -1 = étincelle perdue
  double ang;
  bool strong;
  double range;
};

struct NoteEvent {                     // une note jouée pendant un pas
  int tick, slotId;
  char label;
  int channel, midi, velocity;
  bool solid;                          // trait plein (vélocité 96) ou brisé (56)
  int side;
};

struct SlotInit {
  int gx = 0, gy = 0;
  std::vector<int> lines;
  int label = -1;                      // lettre souhaitée ('A'..), -1 = première libre
  std::vector<int> initial;
  std::string mode = "chromatique";
  int channel = -1;
  bool active = true;
  int tonic = -1, octave = 0;
  bool loopOn = false;
  int loopN = 24;
  bool customOn = false;
  std::vector<Custom> custom;
  int rot = 0;
  bool rotOn = false;
  int rotDir = 1;
  double rotSpeed = 1;
};

class Engine {
 public:
  std::vector<Slot> slots;
  Options opt;
  int cols = 6, rows = 6;
  int tickMs = 240;
  int tick = 0;
  std::vector<Pulse> pulses;
  Sound sound;                          // son de base
  bool hasChanSound[16] = {};           // son propre à un canal
  Sound chanSound[16];
  int program[16];                      // instrument MIDI par canal, -1 = non envoyé

  Engine() { for (int& p : program) p = -1; resetDefault(); }

  // les six hexagrammes de départ de l'application web
  void resetDefault() {
    slots.clear(); pulses.clear(); tick = 0; slotSeq = 0; cols = rows = 6;
    struct D { int gx, gy; const char* l; };
    for (const D& d : std::initializer_list<D>{{0, 2, "111111"}, {2, 0, "101010"}, {4, 1, "111000"}, {5, 3, "010101"}, {3, 4, "000111"}, {1, 4, "100110"}}) {
      SlotInit c; c.gx = d.gx; c.gy = d.gy;
      for (const char* p = d.l; *p; p++) c.lines.push_back(*p - '0');
      addSlot(c);
    }
  }

  // ---------- accès ----------
  Slot* find(int id) { for (auto& s : slots) if (s.id == id) return &s; return nullptr; }
  const Slot* find(int id) const { for (auto& s : slots) if (s.id == id) return &s; return nullptr; }
  Slot* byLabel(char l) { for (auto& s : slots) if (s.label == l) return &s; return nullptr; }

  // ---------- notes ----------
  static const ScaleDef* scaleDef(const std::string& key) {
    for (const auto& s : scaleTable()) if (key == s.key) return &s;
    return nullptr;
  }
  static bool scaleKnown(const std::string& key) { return scaleDef(key) != nullptr; }

  // heptagone : une gamme de moins de 7 notes est complétée par les notes manquantes d'une gamme de même couleur
  static const char* hepta(const std::string& mode) {
    static const std::map<std::string, const char*> m = {{"pentamajeur", "majeur"}, {"pentamineur", "mineur"}, {"blues", "mineur"},
        {"tons", "lydien"}, {"insen", "phrygien"}, {"hirajoshi", "mineur"}, {"augmentee", "lydien"}};
    auto it = m.find(mode);
    return it == m.end() ? nullptr : it->second;
  }
  static std::vector<int> scaleSteps(const Slot& s) {
    const ScaleDef* d = scaleDef(s.mode);
    std::vector<int> steps = d ? d->steps : std::vector<int>();
    if (steps.empty() || s.n < 7 || steps.size() >= 7 || !hepta(s.mode)) return steps;
    for (int x : scaleDef(hepta(s.mode))->steps)
      if (std::find(steps.begin(), steps.end(), x) == steps.end()) steps.push_back(x);
    return steps;
  }
  // décalage en demi-tons depuis la tonique pour le côté k, ou -1 (silence)
  static int defaultOffset(const Slot& s, int k, int line) {
    std::vector<int> steps = scaleSteps(s);
    if (steps.empty()) return 2 * k + (line ? 0 : 1);
    if (!line) return -1;
    int len = static_cast<int>(steps.size());
    return steps[k % len] + 12 * (k / len);
  }
  // notes personnalisées de départ = celles de la gamme ; les notes en trop vont sur les premiers traits brisés
  static void fillCustom(Slot& s) {
    s.custom.assign(s.n, Custom());
    for (int k = 0; k < s.n; k++) s.custom[k] = {defaultOffset(s, k, 1), defaultOffset(s, k, 0)};
    const ScaleDef* d = scaleDef(s.mode);
    if (d && !d->steps.empty())
      for (int j = s.n; j < static_cast<int>(d->steps.size()); j++) s.custom[j - s.n].b = d->steps[j];
  }
  static int offsetOf(const Slot& s, int k) {
    int line = s.lines[k];
    if (s.customOn) return line ? s.custom[k].p : s.custom[k].b;
    return defaultOffset(s, k, line);
  }
  int slotTonic(const Slot& s) const { return s.tonic < 0 ? opt.tonic : s.tonic; }
  int rowOctave(const Slot& s) const { return opt.uniform ? 1 : static_cast<int>(std::floor((rows - 1 - s.gy) * 3.0 / rows)); }
  int midiOf(const Slot& s, int off) const {
    int m = 48 + 12 * (rowOctave(s) + opt.octave + s.octave) + slotTonic(s) + off;
    return std::max(0, std::min(127, m));
  }
  int chanOf(const Slot& s) const { return opt.chanAll < 0 ? s.channel : opt.chanAll; }

  // ---------- création et édition ----------
  Slot& addSlot(const SlotInit& c) {
    Slot s;
    s.id = slotSeq++;
    char free = 'A';
    for (char l : LABELS) { if (!byLabel(l)) { free = l; break; } }
    s.label = (c.label >= 0 && !byLabel(static_cast<char>(c.label))) ? static_cast<char>(c.label) : free;
    s.gx = c.gx; s.gy = c.gy; s.lines = c.lines; s.n = static_cast<int>(c.lines.size());
    s.initial = c.initial.empty() ? c.lines : c.initial;
    s.mode = c.mode;
    s.channel = c.channel < 0 ? static_cast<int>(LABELS.find(s.label)) % 16 : c.channel;
    s.active = c.active; s.tonic = c.tonic; s.octave = c.octave;
    s.loopOn = c.loopOn; s.loopN = c.loopN; s.customOn = c.customOn;
    s.rot = c.rot; s.rotOn = c.rotOn; s.rotDir = c.rotDir; s.rotSpeed = c.rotSpeed;
    if (!c.custom.empty()) s.custom = c.custom; else fillCustom(s);
    slots.push_back(s);
    return slots.back();
  }
  void removeSlot(int id) {
    slots.erase(std::remove_if(slots.begin(), slots.end(), [&](const Slot& s) { return s.id == id; }), slots.end());
    dropPulsesTo(id);
  }
  bool canResize(int c, int r) const {                   // taille permise, et aucun hexagone ne sortirait
    if (c < N_MIN || c > N_MAX || r < N_MIN || r > N_MAX) return false;
    for (const auto& s : slots) if (s.gx >= c || s.gy >= r) return false;
    return true;
  }
  bool setGridSize(int c, int r) {
    if (!canResize(c, r)) return false;
    cols = c; rows = r; return true;
  }
  // point libre le plus proche, ou false si la grille est pleine
  bool freeCellNear(int gx, int gy, int& ox, int& oy) const {
    double bd = INF; bool ok = false;
    for (int y = 0; y < rows; y++) for (int x = 0; x < cols; x++) {
      bool taken = false;
      for (const auto& s : slots) if (s.gx == x && s.gy == y) { taken = true; break; }
      if (taken) continue;
      double d = std::hypot(x - gx, y - gy);
      if (d < bd) { bd = d; ox = x; oy = y; ok = true; }
    }
    return ok;
  }
  void setHexagram(Slot& s, int n) {
    const auto& l = hexLinesTable().at(n);
    for (int i = 0; i < 6; i++) s.lines[i] = l[i];
    s.initial = s.lines;
  }
  void resetSlot(Slot& s) {
    s.lines = s.initial; s.rot = 0; s.rotAcc = 0; s.rotDelta = 0; s.phOn = false;
    dropPulsesTo(s.id);
  }
  void rotateBy(Slot& s, int d) { s.rot += d; s.rotDelta = d; }
  void silence() { pulses.clear(); for (auto& s : slots) s.phOn = false; }
  void prime(Slot& s, int side = 0) { s.phOn = true; s.phSide = side; s.phLeft = s.n; }
  // 6 ou 7 côtés : le 7e est un trait plein de plus ; les notes encore « de la gamme » sont redistribuées
  void setSides(Slot& s, int n) {
    if (n == s.n || (n != 6 && n != 7)) return;
    std::vector<Custom> before = s.custom;
    fillCustom(s);
    bool pristine = s.custom == before;
    s.custom = before;
    if (n == 7) {
      s.lines.push_back(1); s.initial.push_back(1); s.n = 7;
      s.custom.push_back({defaultOffset(s, 6, 1), defaultOffset(s, 6, 0)});
    } else {
      s.lines.resize(6); s.initial.resize(6); s.custom.resize(6); s.n = 6;
    }
    if (pristine) fillCustom(s);
    s.rot = 0; s.rotAcc = 0; s.rotDelta = 0; s.phOn = false;
    dropPulsesTo(s.id);
  }
  // mélange des notes personnalisées (voir shuffleCustom de l'application web)
  template <class Rng> void shuffleCustom(Slot& s, Rng& rng) {
    if (!s.customOn) fillCustom(s);
    auto& c = s.custom;
    const ScaleDef* d = scaleDef(s.mode);
    if (!d || d->steps.empty()) std::shuffle(c.begin(), c.end(), rng);
    else {
      std::vector<int> pool;
      for (const auto& x : c) { if (x.p >= 0) pool.push_back(x.p); if (x.b >= 0) pool.push_back(x.b); }
      std::shuffle(pool.begin(), pool.end(), rng);
      std::vector<int> where(s.n);
      for (int k = 0; k < s.n; k++) where[k] = k;
      std::shuffle(where.begin(), where.end(), rng);
      for (int k = 0; k < s.n; k++) { c[k].p = k < static_cast<int>(pool.size()) ? pool[k] : -1; c[k].b = -1; }
      for (int i = s.n; i < static_cast<int>(pool.size()); i++) c[where[i - s.n]].b = pool[i];
    }
    s.customOn = true;
  }
  template <class Rng> void drawRandom(Rng& rng) {
    for (auto& s : slots) {
      for (int& l : s.lines) l = std::uniform_int_distribution<int>(0, 1)(rng);
      s.initial = s.lines;
    }
  }
  template <class Rng> void scatter(Rng& rng) {
    std::vector<std::pair<int, int>> cells;
    for (int y = 0; y < rows; y++) for (int x = 0; x < cols; x++) cells.push_back({x, y});
    std::shuffle(cells.begin(), cells.end(), rng);
    for (size_t i = 0; i < slots.size() && i < cells.size(); i++) { slots[i].gx = cells[i].first; slots[i].gy = cells[i].second; }
    silence();
  }

  // ---------- propagation ----------
  // "to" est-il dans le secteur de "from" centré sur la direction ang ? (demi-ouvert, largeur 360 / n)
  static bool inSector(const Slot& from, const Slot& to, double ang) {
    double cw = std::atan2(to.gx - from.gx, -(to.gy - from.gy)) * 180.0 / M_PI, w = 180.0 / from.n;
    return std::fmod(cw - ang + w + 1e-6 + 1080, 360.0) < 2 * w;
  }
  bool findTarget(const Slot& from, double ang, int& id, double& dist) const {
    double bd = INF; int best = -1;
    for (const auto& o : slots) {
      if (o.id == from.id || !o.active) continue;
      if (!inSector(from, o, ang)) continue;
      double d = std::hypot(o.gx - from.gx, o.gy - from.gy);
      if (d < bd) { bd = d; best = o.id; }
    }
    if (best < 0) return false;
    id = best; dist = bd; return true;
  }
  void emit(const Slot& from, double ang, bool strong, double range) {
    if (static_cast<int>(pulses.size()) >= MAX_PULSES) return;
    int id = -1; double dist = 0;
    bool hit = findTarget(from, ang, id, dist);
    double x0 = from.gx, y0 = from.gy, r = ang * M_PI / 180.0;
    if (!hit || dist > range + 1e-9) {                   // impulsion perdue : étincelle
      pulses.push_back({x0, y0, x0 + std::sin(r) * .6, y0 - std::cos(r) * .6, tick, tick + 1, -1, ang, strong, range});
      return;
    }
    const Slot* t = find(id);
    pulses.push_back({x0, y0, double(t->gx), double(t->gy), tick, tick + std::max(1, int(std::ceil(dist - 1e-9))), id, ang, strong, range - dist});
  }
  void receive(const Pulse& p) {
    Slot* slot = find(p.target);
    if (!slot) return;
    int n = slot->n, s = int(jsRound(modnd(p.ang + 180, 360) / (360.0 / n))) % n;
    int li = modn(s - slot->rot, n), line = slot->lines[li];
    if (line == 1) prime(*slot, li);                     // trait plein : rebond
    else emit(*slot, p.ang, p.strong, p.range);          // trait brisé : traversée
    if (opt.mutate && (p.strong ? 1 : 0) != line) slot->lines[li] ^= 1;
  }

  // un pas d'horloge ; renvoie les notes jouées
  std::vector<NoteEvent> step() {
    std::vector<NoteEvent> out;
    tick++;
    for (auto& s : slots) if (s.active && s.loopOn && tick % s.loopN == 1) prime(s);
    std::vector<Pulse> arriving, rest;
    for (const auto& p : pulses) (p.t1 <= tick ? arriving : rest).push_back(p);
    pulses = rest;
    for (const auto& p : arriving) {
      if (p.target < 0) continue;
      const Slot* t = find(p.target);
      if (t && t->active) receive(p);
    }
    for (auto& slot : slots) {
      if (!slot.phOn || !slot.active) continue;
      int k = slot.phSide, line = slot.lines[k], off = offsetOf(slot, k);
      if (off >= 0) {
        out.push_back({tick, slot.id, slot.label, chanOf(slot), midiOf(slot, off), line ? 96 : 56, line == 1, k});
        emit(slot, sideAng(modn(k + slot.rot, slot.n), slot.n), line == 1, line ? INF : opt.softRange);
      }
      slot.phSide = (k + 1) % slot.n;
      if (--slot.phLeft <= 0) slot.phOn = false;
    }
    for (auto& slot : slots) {                           // rotation à la fin du pas
      if (!slot.rotOn) continue;
      slot.rotAcc += slot.rotSpeed;
      int n = int(std::floor(slot.rotAcc + 1e-6));
      if (n < 1) continue;
      slot.rotAcc -= n;
      slot.rotDelta = n * slot.rotDir;
      slot.rot += slot.rotDelta;
    }
    return out;
  }

  // ---------- setup JSON ----------
  bool loadSetup(const std::string& text, std::string* err = nullptr);
  Json toJson(const std::string& date = "") const;
  std::string saveSetup(const std::string& date = "") const;

 private:
  int slotSeq = 0;
  void dropPulsesTo(int id) {
    pulses.erase(std::remove_if(pulses.begin(), pulses.end(), [&](const Pulse& p) { return p.target == id; }), pulses.end());
  }
};

}  // namespace hexa

#include "setup.hpp"
