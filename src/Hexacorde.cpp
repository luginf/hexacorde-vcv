// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026, luginf
// Module VCV Rack "Hexacorde" : le séquenceur oraculaire (cœur C++ de ../../core) dans le rack.
// Entrées : CLOCK (un pas par impulsion, sinon horloge interne en BPM), RESET, PRIME (poly : le canal i amorce
// l'hexagone i), ROTATE (poly : le canal i fait tourner l'hexagone i d'un cran).
// Sorties polyphoniques (un canal par hexagone, 16 au plus) : PITCH (V/oct, do4 = 0 V), GATE, VEL (0 à 10 V) ;
// STEP = une impulsion par pas. Le plateau se manipule comme dans l'application web.
#include <mutex>
#include <array>
#include <map>
#include <fstream>
#include <sstream>
#include <osdialog.h>
#include "plugin.hpp"
#include "hexacorde.hpp"

using HEngine = hexa::Engine;
using HSlot = hexa::Slot;

static const char* NOTE_EN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char* TRIGRAM_EN[8] = {"Qian (Heaven)", "Zhen (Thunder)", "Kan (Water)", "Gen (Mountain)", "Kun (Earth)", "Xun (Wind)", "Li (Fire)", "Dui (Lake)"};

struct Hexacorde : Module {
  enum ParamId { TEMPO_PARAM, RUN_PARAM, PARAMS_LEN };
  enum InputId { CLOCK_INPUT, RESET_INPUT, PRIME_INPUT, ROTATE_INPUT, INPUTS_LEN };
  enum OutputId { PITCH_OUTPUT, GATE_OUTPUT, VEL_OUTPUT, STEP_OUTPUT, OUTPUTS_LEN };
  enum LightId { RUN_LIGHT, LIGHTS_LEN };

  HEngine engine;
  std::mutex mtx;                                   // protège `engine` entre le fil audio et l'interface
  std::map<int, std::array<int, 8>> flash;          // id d'hexagone -> pas de la dernière note de chaque côté
  double stepProgress = 0;                          // avancement dans le pas courant (0 à 1), pour l'animation
  double lastPeriod = 0.24;                         // durée du dernier pas, en secondes
  int nOut = 6;                                     // nombre de canaux polyphoniques en sortie

  dsp::SchmittTrigger clockTrig, resetTrig, primeTrig[16], rotTrig[16];
  dsp::PulseGenerator stepPulse;
  float phase = 0, sinceStep = 0;
  float pitch[16] = {}, vel[16] = {}, gateLeft[16] = {};

  Hexacorde() {
    config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
    configParam(TEMPO_PARAM, 30.f, 375.f, 125.f, "Internal tempo (one step = an eighth note)", " BPM");
    paramQuantities[TEMPO_PARAM]->snapEnabled = true;
    configButton(RUN_PARAM, "Run the internal clock");
    configInput(CLOCK_INPUT, "Clock (one step per pulse)");
    configInput(RESET_INPUT, "Reset (clears the pulses)");
    configInput(PRIME_INPUT, "Prime (poly: channel i primes hexagon i)");
    configInput(ROTATE_INPUT, "Rotate (poly: channel i turns hexagon i)");
    configOutput(PITCH_OUTPUT, "Pitch, 1 V/oct (poly: one channel per hexagon)");
    configOutput(GATE_OUTPUT, "Gate (poly)");
    configOutput(VEL_OUTPUT, "Velocity, 0 to 10 V (poly)");
    configOutput(STEP_OUTPUT, "Step trigger");
    params[RUN_PARAM].setValue(1.f);
    nOut = std::min<int>(16, (int)engine.slots.size());
  }

  // modification de l'état du moteur depuis l'interface (verrou + mise à jour du nombre de canaux)
  template <class F> void edit(F f) {
    std::lock_guard<std::mutex> lk(mtx);
    f();
    nOut = std::min<int>(16, (int)engine.slots.size());
  }

  void doStep() {
    float period = sinceStep;
    if (period < 0.005f || period > 4.f) period = (float)lastPeriod; else lastPeriod = period;
    sinceStep = 0;
    std::lock_guard<std::mutex> lk(mtx);
    std::vector<hexa::NoteEvent> ev = engine.step();
    nOut = std::min<int>(16, (int)engine.slots.size());
    for (const hexa::NoteEvent& e : ev) {
      int idx = -1;
      for (size_t i = 0; i < engine.slots.size(); i++) if (engine.slots[i].id == e.slotId) { idx = (int)i; break; }
      auto& fl = flash[e.slotId];
      if (fl[0] == 0 && fl[1] == 0 && fl[2] == 0 && fl[3] == 0 && fl[4] == 0 && fl[5] == 0 && fl[6] == 0) fl.fill(-99);
      fl[e.side] = e.tick;
      if (idx < 0 || idx >= 16) continue;
      pitch[idx] = (e.midi - 60) / 12.f;
      vel[idx] = e.velocity / 127.f * 10.f;
      gateLeft[idx] = 0.9f * period;               // la note dure 90 % d'un pas, comme le note off MIDI de l'application web
    }
    stepPulse.trigger(1e-3f);
    static const bool debug = std::getenv("HEXACORDE_DEBUG") != nullptr;      // trace des pas (tests), jamais en usage normal
    if (debug) for (const hexa::NoteEvent& e : ev) INFO("HEXACORDE step=%d slot=%c midi=%d vel=%d gate=%.3f", e.tick, e.label, e.midi, e.velocity, 0.9f * period);
  }

  void process(const ProcessArgs& args) override {
    float dt = args.sampleTime;
    sinceStep += dt;
    bool stepNow = false;
    if (inputs[CLOCK_INPUT].isConnected()) {
      if (clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 2.f)) stepNow = true;
    } else if (params[RUN_PARAM].getValue() > 0.5f) {
      phase += dt * params[TEMPO_PARAM].getValue() / 30.f;           // un pas = une croche : BPM / 30 pas par seconde
      if (phase >= 1.f) { phase -= 1.f; stepNow = true; }
    }
    if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 2.f)) {
      std::lock_guard<std::mutex> lk(mtx);
      engine.silence(); engine.tick = 0; flash.clear(); phase = 0;
      for (int i = 0; i < 16; i++) gateLeft[i] = 0;
    }
    int np = std::min(16, inputs[PRIME_INPUT].getChannels());
    for (int c = 0; c < np; c++)
      if (primeTrig[c].process(inputs[PRIME_INPUT].getVoltage(c), 0.1f, 2.f)) {
        std::lock_guard<std::mutex> lk(mtx);
        if (c < (int)engine.slots.size() && engine.slots[c].active) engine.prime(engine.slots[c]);
      }
    int nr = std::min(16, inputs[ROTATE_INPUT].getChannels());
    for (int c = 0; c < nr; c++)
      if (rotTrig[c].process(inputs[ROTATE_INPUT].getVoltage(c), 0.1f, 2.f)) {
        std::lock_guard<std::mutex> lk(mtx);
        if (c < (int)engine.slots.size()) engine.rotateBy(engine.slots[c], 1);
      }
    if (stepNow) doStep();
    stepProgress = std::min(1.0, (double)sinceStep / lastPeriod);

    int n = std::max(1, nOut);
    outputs[PITCH_OUTPUT].setChannels(n); outputs[GATE_OUTPUT].setChannels(n); outputs[VEL_OUTPUT].setChannels(n);
    for (int i = 0; i < n; i++) {
      outputs[PITCH_OUTPUT].setVoltage(pitch[i], i);
      outputs[VEL_OUTPUT].setVoltage(vel[i], i);
      outputs[GATE_OUTPUT].setVoltage(gateLeft[i] > 0.f ? 10.f : 0.f, i);
      gateLeft[i] -= dt;
    }
    outputs[STEP_OUTPUT].setVoltage(stepPulse.process(dt) ? 10.f : 0.f);
    lights[RUN_LIGHT].setBrightness(params[RUN_PARAM].getValue() > 0.5f ? 1.f : 0.f);
  }

  json_t* dataToJson() override {
    json_t* root = json_object();
    std::lock_guard<std::mutex> lk(mtx);
    json_object_set_new(root, "setup", json_string(engine.saveSetup().c_str()));   // même format que l'application web
    return root;
  }
  void dataFromJson(json_t* root) override {
    json_t* s = json_object_get(root, "setup");
    if (!s) return;
    std::lock_guard<std::mutex> lk(mtx);
    engine.loadSetup(json_string_value(s), nullptr);
    flash.clear();
    nOut = std::min<int>(16, (int)engine.slots.size());
  }
  void onReset() override {
    std::lock_guard<std::mutex> lk(mtx);
    engine.resetDefault(); flash.clear();
    nOut = std::min<int>(16, (int)engine.slots.size());
  }

  // ---- fichiers de setup (compatibles avec l'application web) ----
  bool importFile(const std::string& path, std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "cannot open the file"; return false; }
    std::stringstream ss; ss << f.rdbuf();
    std::lock_guard<std::mutex> lk(mtx);
    if (!engine.loadSetup(ss.str(), &err)) return false;
    params[TEMPO_PARAM].setValue(std::round(30000.f / engine.tickMs));
    flash.clear();
    nOut = std::min<int>(16, (int)engine.slots.size());
    return true;
  }
  void exportFile(const std::string& path) {
    std::string text;
    { std::lock_guard<std::mutex> lk(mtx); engine.tickMs = (int)std::round(30000.f / params[TEMPO_PARAM].getValue()); text = engine.saveSetup(); }
    std::ofstream f(path);
    f << text << "\n";
  }
};

// ---------------------------------------------------------------------------------------------------------------
// plateau : dessin et interaction

namespace {
constexpr float S = 90.f, M = 60.f, R = 36.f;          // mêmes unités que l'application web (grille, marge, rayon)
float px(float g) { return M + g * S; }
NVGcolor col(unsigned rgb, float a = 1.f) { return nvgRGBAf(((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, a); }
const unsigned C_BODY = 0x1c1c22, C_EDGE = 0x2a2a33, C_INK = 0xe8e0c8, C_DIM = 0x8a8574, C_YANG = 0xffb000, C_YIN = 0x5fc8d8, C_GRID = 0x3a3a44, C_GLYPH = 0x4a4a56;

// sommet du polygone (degrés SVG : 0 = à droite, sens horaire à l'écran)
Vec vertexAt(float deg, float d = 0.f) {
  float a = (deg + d) * M_PI / 180.f;
  return Vec(R * std::cos(a), R * std::sin(a));
}
Vec rotatePt(Vec p, float deg) {
  float a = deg * M_PI / 180.f, c = std::cos(a), s = std::sin(a);
  return Vec(p.x * c - p.y * s, p.x * s + p.y * c);
}
float segDist(Vec p, Vec a, Vec b) {
  Vec ab = b.minus(a); float l2 = ab.dot(ab), t = l2 > 0 ? clamp(p.minus(a).dot(ab) / l2, 0.f, 1.f) : 0.f;
  return p.minus(a.plus(ab.mult(t))).norm();
}
}  // namespace

struct BoardWidget : widget::OpaqueWidget {
  Hexacorde* module = nullptr;
  HEngine demo;                                              // aperçu dans le navigateur de modules
  struct Anim { int lastRot = 0; double t0 = -1e9; int delta = 0; };
  std::map<int, Anim> anims;
  // glisser
  int dragId = -1, dragLine = -1;
  Vec dragStart, dragPos;
  bool dragMoved = false;
  float spinPrev = 0, spinTotal = 0; int spinApplied = 0;

  HEngine& eng() { return module ? module->engine : demo; }

  float scale() { HEngine& e = eng(); return std::min(box.size.x / (2 * M + (e.cols - 1) * S), box.size.y / (2 * M + (e.rows - 1) * S)); }
  Vec offset() {
    HEngine& e = eng(); float s = scale();
    return Vec((box.size.x - s * (2 * M + (e.cols - 1) * S)) / 2, (box.size.y - s * (2 * M + (e.rows - 1) * S)) / 2);
  }
  Vec toLogical(Vec p) { float s = scale(); Vec o = offset(); return Vec((p.x - o.x) / s, (p.y - o.y) / s); }

  // hexagone et trait sous le point (coordonnées logiques) ; verrou déjà pris
  int hitSlot(Vec p, int& line) {
    HEngine& e = eng(); line = -1;
    for (int i = (int)e.slots.size() - 1; i >= 0; i--) {
      const HSlot& s = e.slots[i];
      Vec c(px((float)s.gx), px((float)s.gy)), d = p.minus(c);
      if (d.norm() > R + 5) continue;
      float rotDeg = 360.f / s.n * s.rot;
      for (int k = 0; k < s.n; k++) {
        Vec a = rotatePt(vertexAt(270 + hexa::sideAng(k, s.n), -180.f / s.n), rotDeg), b = rotatePt(vertexAt(270 + hexa::sideAng(k, s.n), 180.f / s.n), rotDeg);
        if (segDist(d, a, b) < 7.f) { line = k; break; }
      }
      return s.id;
    }
    return -1;
  }

  void drawHexagon(const DrawArgs& args, const HSlot& s, double tf, const std::array<int, 8>* fl, double rotAngle) {
    NVGcontext* vg = args.vg;
    int n = s.n;
    float alpha = s.active ? 1.f : 0.3f;
    nvgSave(vg);
    nvgTranslate(vg, px((float)s.gx), px((float)s.gy));
    nvgGlobalAlpha(vg, alpha);
    nvgBeginPath(vg);
    for (int k = 0; k < n; k++) {
      Vec v = vertexAt(270 + hexa::sideAng(k, n), -180.f / n);
      if (k == 0) nvgMoveTo(vg, v.x, v.y); else nvgLineTo(vg, v.x, v.y);
    }
    nvgClosePath(vg);
    nvgFillColor(vg, col(C_BODY)); nvgFill(vg);
    nvgStrokeColor(vg, col(C_EDGE)); nvgStrokeWidth(vg, 1.f); nvgStroke(vg);
    // traits : ils tournent avec l'hexagone
    nvgSave(vg);
    nvgRotate(vg, (float)(rotAngle * M_PI / 180.0));
    nvgLineCap(vg, NVG_ROUND);
    for (int k = 0; k < n; k++) {
      Vec a = vertexAt(270 + hexa::sideAng(k, n), -180.f / n), b = vertexAt(270 + hexa::sideAng(k, n), 180.f / n);
      auto lerp = [&](float t) { return Vec(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); };
      float f = fl ? std::max(0.0, 1.0 - (tf - (*fl)[k]) / 2.5) : 0.0;
      NVGcolor c = f > 0 ? col(s.lines[k] ? C_YANG : C_YIN, 0.55f + 0.45f * (float)f) : col(C_INK, 0.55f);
      nvgStrokeColor(vg, c); nvgStrokeWidth(vg, 5.f + 3.f * (float)f);
      nvgBeginPath(vg);
      if (s.lines[k]) { Vec p = lerp(0.12f), q = lerp(0.88f); nvgMoveTo(vg, p.x, p.y); nvgLineTo(vg, q.x, q.y); }
      else {
        Vec p = lerp(0.12f), q = lerp(0.42f), r2 = lerp(0.58f), t = lerp(0.88f);
        nvgMoveTo(vg, p.x, p.y); nvgLineTo(vg, q.x, q.y); nvgMoveTo(vg, r2.x, r2.y); nvgLineTo(vg, t.x, t.y);
      }
      nvgStroke(vg);
    }
    nvgRestore(vg);
    // noms de notes (restent droits) et lettre
    nvgFontSize(vg, 9.f); nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    for (int k = 0; k < n; k++) {
      int off = HEngine::offsetOf(s, k);
      std::string txt = off < 0 ? "." : std::string(NOTE_EN[(eng().slotTonic(s) + off) % 12]) + (off >= 12 ? "'" : "");
      float a = (270 + hexa::sideAng(k, n) + (float)rotAngle) * M_PI / 180.f;
      float f = fl ? std::max(0.0, 1.0 - (tf - (*fl)[k]) / 2.5) : 0.0;
      nvgFillColor(vg, f > 0 ? col(C_INK) : col(C_DIM));
      nvgText(vg, std::cos(a) * R * 0.58f, std::sin(a) * R * 0.58f, txt.c_str(), NULL);
    }
    nvgFontSize(vg, 15.f); nvgFillColor(vg, col(C_GLYPH));
    char l[2] = {s.label, 0}; nvgText(vg, 0, 0, l, NULL);
    int hn = hexa::hexNumber(s.lines);
    std::string name = std::to_string(hn) + " " + hexa::hexName(hn).pinyin + (n == 7 ? " +1" : "");
    nvgFontSize(vg, 8.5f); nvgFillColor(vg, col(C_DIM));
    nvgText(vg, 0, R + 11, name.c_str(), NULL);
    nvgRestore(vg);
  }

  void draw(const DrawArgs& args) override {
    NVGcontext* vg = args.vg;
    std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
    nvgSave(vg);
    nvgScissor(vg, 0, 0, box.size.x, box.size.y);
    if (font) nvgFontFaceId(vg, font->handle);
    float sc = scale(); Vec o = offset();
    nvgTranslate(vg, o.x, o.y); nvgScale(vg, sc, sc);
    std::unique_lock<std::mutex> lk;
    if (module) lk = std::unique_lock<std::mutex>(module->mtx);
    HEngine& e = eng();
    double now = system::getTime();
    double tf = e.tick + (module ? module->stepProgress : 0.0);
    nvgFillColor(vg, col(C_GRID));
    for (int y = 0; y < e.rows; y++) for (int x = 0; x < e.cols; x++) { nvgBeginPath(vg); nvgCircle(vg, px((float)x), px((float)y), 2.5f); nvgFill(vg); }
    for (const HSlot& s : e.slots) {
      Anim& a = anims[s.id];
      if (a.lastRot != s.rot) { a.delta = s.rot - a.lastRot; a.lastRot = s.rot; a.t0 = now; }
      double dur = module ? std::min(0.3, module->lastPeriod) : 0.2, u = clamp((float)((now - a.t0) / dur), 0.f, 1.f);
      double ease = u * u * (3 - 2 * u);
      double rotAngle = 360.0 / s.n * (s.rot - a.delta * (1 - ease));
      const std::array<int, 8>* fl = nullptr;
      if (module) { auto it = module->flash.find(s.id); if (it != module->flash.end()) fl = &it->second; }
      drawHexagon(args, s, tf, fl, rotAngle);
    }
    for (const hexa::Pulse& p : e.pulses) {                  // impulsions : forte = ambre, douce = cyan
      double u = clamp((float)((tf - p.t0) / std::max(1, p.t1 - p.t0)), 0.f, 1.f);
      nvgBeginPath(vg);
      nvgCircle(vg, px((float)(p.x0 + (p.x1 - p.x0) * u)), px((float)(p.y0 + (p.y1 - p.y0) * u)), p.strong ? 6.f : 4.f);
      nvgFillColor(vg, col(p.strong ? C_YANG : C_YIN, p.target >= 0 ? 1.f : (float)(1 - u)));
      nvgFill(vg);
    }
    if (dragId >= 0 && dragMoved && dragLine < 0) {          // fantôme de l'hexagone qu'on déplace
      nvgBeginPath(vg); nvgCircle(vg, dragPos.x, dragPos.y, R); nvgStrokeColor(vg, col(C_YANG, 0.6f)); nvgStrokeWidth(vg, 1.5f); nvgStroke(vg);
    }
    nvgRestore(vg);
  }

  // ---- souris ----
  void onButton(const ButtonEvent& e) override {
    if (!module) return;
    Vec p = toLogical(e.pos);
    int line = -1, id;
    { std::lock_guard<std::mutex> lk(module->mtx); id = hitSlot(p, line); }
    if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && id >= 0) {
      dragId = id; dragLine = line; dragStart = p; dragPos = p; dragMoved = false;
      spinTotal = 0; spinApplied = 0;
      const HSlot* s = module->engine.find(id);
      if (s) spinPrev = std::atan2(p.y - px((float)s->gy), p.x - px((float)s->gx)) * 180.f / M_PI;
      e.consume(this);
    } else if (e.button == GLFW_MOUSE_BUTTON_RIGHT && e.action == GLFW_PRESS && id >= 0) {
      createSlotMenu(id);
      e.consume(this);
    }
    // clic droit hors hexagone : on laisse passer, le menu du module (appendContextMenu) propose les réglages du plateau
  }
  void onDragMove(const DragMoveEvent& e) override {
    if (dragId < 0 || !module) return;
    float sc = scale();
    dragPos = dragPos.plus(e.mouseDelta.div(sc));
    if (!dragMoved && dragPos.minus(dragStart).norm() * sc < 5.f) return;
    dragMoved = true;
    if (dragLine >= 0) {                                      // glisser un trait : tourner l'hexagone par crans
      std::lock_guard<std::mutex> lk(module->mtx);
      HSlot* s = module->engine.find(dragId);
      if (!s) return;
      float ang = std::atan2(dragPos.y - px((float)s->gy), dragPos.x - px((float)s->gx)) * 180.f / M_PI, d = ang - spinPrev;
      if (d > 180) d -= 360; else if (d < -180) d += 360;
      spinPrev = ang; spinTotal += d;
      int n = (int)std::round(spinTotal / (360.f / s->n));
      if (n != spinApplied) { module->engine.rotateBy(*s, n - spinApplied); spinApplied = n; }
    }
  }
  void onDragEnd(const DragEndEvent& e) override {
    if (dragId < 0 || !module) return;
    int id = dragId, line = dragLine; bool moved = dragMoved;
    dragId = -1;
    if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
    module->edit([&] {
      HSlot* s = module->engine.find(id);
      if (!s) return;
      if (!moved) {
        if (line >= 0) s->lines[line] ^= 1;                                        // clic sur un trait : plein / brisé
        else if (s->active) module->engine.prime(*s);                              // clic sur l'hexagone : amorce
      } else if (line < 0) {                                                       // déplacement sur un point libre
        int gx = clamp((int)std::round((dragPos.x - M) / S), 0, module->engine.cols - 1);
        int gy = clamp((int)std::round((dragPos.y - M) / S), 0, module->engine.rows - 1);
        bool taken = false;
        for (const HSlot& o : module->engine.slots) if (o.id != id && o.gx == gx && o.gy == gy) taken = true;
        if (!taken) { s->gx = gx; s->gy = gy; }
      }
    });
  }
  void onHoverScroll(const HoverScrollEvent& e) override {          // molette : tourne l'hexagone (bas = horaire)
    if (!module) return;
    int line, id;
    { std::lock_guard<std::mutex> lk(module->mtx); id = hitSlot(toLogical(e.pos), line); }
    if (id < 0) return;
    int d = e.scrollDelta.y < 0 ? 1 : -1;
    module->edit([&] { HSlot* s = module->engine.find(id); if (s) module->engine.rotateBy(*s, d); });
    e.consume(this);
  }

  // ---- menu d'un hexagone ----
  void createSlotMenu(int id);
};

// aides pour les menus
namespace {
HSlot* slotById(Hexacorde* m, int id) { return m->engine.find(id); }
std::string slotTitle(Hexacorde* m, int id) {
  std::lock_guard<std::mutex> lk(m->mtx);
  HSlot* s = slotById(m, id);
  if (!s) return "Hexagon";
  int hn = hexa::hexNumber(s->lines);
  return std::string(1, s->label) + " : " + std::to_string(hn) + ". " + hexa::hexName(hn).pinyin + ", " + hexa::hexName(hn).en;
}
std::string noteLabel(Hexacorde* m, int id, int off) {
  if (off < 0) return "silence";
  HSlot* s = slotById(m, id);
  int t = s ? m->engine.slotTonic(*s) : 0;
  return std::string(NOTE_EN[(t + off) % 12]) + (off >= 12 ? "'" : "");
}
}  // namespace

void BoardWidget::createSlotMenu(int id) {
  Hexacorde* m = module;
  ui::Menu* menu = createMenu();
  menu->addChild(createMenuLabel(slotTitle(m, id)));
  auto get = [m, id](auto f) { std::lock_guard<std::mutex> lk(m->mtx); HSlot* s = slotById(m, id); return s ? f(*s) : decltype(f(*s))(); };

  // hexagramme : trigramme inférieur puis supérieur
  menu->addChild(createSubmenuItem("Hexagram", "", [m, id](ui::Menu* sub) {
    for (int lo = 0; lo < 8; lo++)
      sub->addChild(createSubmenuItem(std::string("Lower: ") + TRIGRAM_EN[lo], "", [m, id, lo](ui::Menu* sub2) {
        for (int up = 0; up < 8; up++) {
          int n = hexa::kwTable()[lo][up];
          sub2->addChild(createMenuItem(std::string(TRIGRAM_EN[up]) + " : " + std::to_string(n) + ". " + hexa::hexName(n).en, "", [m, id, n] {
            m->edit([&] { HSlot* s = slotById(m, id); if (s) m->engine.setHexagram(*s, n); });
          }));
        }
      }));
  }));
  menu->addChild(createSubmenuItem("Scale", get([](HSlot& s) { return std::string(HEngine::scaleDef(s.mode)->en); }), [m, id](ui::Menu* sub) {
    for (const hexa::ScaleDef& d : hexa::scaleTable()) {
      std::string key = d.key;
      sub->addChild(createCheckMenuItem(d.en, "", [m, id, key] { std::lock_guard<std::mutex> lk(m->mtx); HSlot* s = slotById(m, id); return s && s->mode == key; },
                                        [m, id, key] { m->edit([&] { HSlot* s = slotById(m, id); if (s) s->mode = key; }); }));
    }
  }));
  menu->addChild(createSubmenuItem("Tonic", get([m](HSlot& s) { return s.tonic < 0 ? std::string("global") : std::string(NOTE_EN[s.tonic]); }), [m, id](ui::Menu* sub) {
    sub->addChild(createCheckMenuItem("global tonic", "", [m, id] { std::lock_guard<std::mutex> lk(m->mtx); HSlot* s = slotById(m, id); return s && s->tonic < 0; },
                                      [m, id] { m->edit([&] { HSlot* s = slotById(m, id); if (s) s->tonic = -1; }); }));
    for (int t = 0; t < 12; t++)
      sub->addChild(createCheckMenuItem(NOTE_EN[t], "", [m, id, t] { std::lock_guard<std::mutex> lk(m->mtx); HSlot* s = slotById(m, id); return s && s->tonic == t; },
                                        [m, id, t] { m->edit([&] { HSlot* s = slotById(m, id); if (s) s->tonic = t; }); }));
  }));
  menu->addChild(createSubmenuItem("Octave", get([](HSlot& s) { return std::to_string(s.octave); }), [m, id](ui::Menu* sub) {
    for (int o = -3; o <= 3; o++)
      sub->addChild(createCheckMenuItem(std::to_string(o), "", [m, id, o] { std::lock_guard<std::mutex> lk(m->mtx); HSlot* s = slotById(m, id); return s && s->octave == o; },
                                        [m, id, o] { m->edit([&] { HSlot* s = slotById(m, id); if (s) s->octave = o; }); }));
  }));
  // rotation automatique
  menu->addChild(createCheckMenuItem("Rotation", "", [m, id] { std::lock_guard<std::mutex> lk(m->mtx); HSlot* s = slotById(m, id); return s && s->rotOn; },
                                     [m, id] { m->edit([&] { HSlot* s = slotById(m, id); if (s) s->rotOn = !s->rotOn; }); }));
  menu->addChild(createCheckMenuItem("Rotation counterclockwise", "", [m, id] { std::lock_guard<std::mutex> lk(m->mtx); HSlot* s = slotById(m, id); return s && s->rotDir < 0; },
                                     [m, id] { m->edit([&] { HSlot* s = slotById(m, id); if (s) s->rotDir = -s->rotDir; }); }));
  menu->addChild(createSubmenuItem("Rotation speed", "", [m, id](ui::Menu* sub) {
    static const struct { double v; const char* name; } sp[] = {{0.125, "÷8"}, {0.25, "÷4"}, {1.0 / 3, "÷3"}, {0.5, "÷2"}, {1, "×1"}, {2, "×2"}, {3, "×3"}};
    for (const auto& x : sp) {
      double v = x.v;
      sub->addChild(createCheckMenuItem(x.name, "", [m, id, v] { std::lock_guard<std::mutex> lk(m->mtx); HSlot* s = slotById(m, id); return s && std::fabs(s->rotSpeed - v) < 1e-6; },
                                        [m, id, v] { m->edit([&] { HSlot* s = slotById(m, id); if (s) { s->rotSpeed = v; s->rotAcc = 0; } }); }));
    }
  }));
  menu->addChild(createSubmenuItem("Priming every N steps", get([](HSlot& s) { return s.loopOn ? std::to_string(s.loopN) : std::string("off"); }), [m, id](ui::Menu* sub) {
    sub->addChild(createCheckMenuItem("off", "", [m, id] { std::lock_guard<std::mutex> lk(m->mtx); HSlot* s = slotById(m, id); return s && !s->loopOn; },
                                      [m, id] { m->edit([&] { HSlot* s = slotById(m, id); if (s) s->loopOn = false; }); }));
    for (int n : {2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64})
      sub->addChild(createCheckMenuItem(std::to_string(n), "", [m, id, n] { std::lock_guard<std::mutex> lk(m->mtx); HSlot* s = slotById(m, id); return s && s->loopOn && s->loopN == n; },
                                        [m, id, n] { m->edit([&] { HSlot* s = slotById(m, id); if (s) { s->loopOn = true; s->loopN = n; } }); }));
  }));
  menu->addChild(createCheckMenuItem("7th side (heptagon)", "", [m, id] { std::lock_guard<std::mutex> lk(m->mtx); HSlot* s = slotById(m, id); return s && s->n == 7; },
                                     [m, id] { m->edit([&] { HSlot* s = slotById(m, id); if (s) m->engine.setSides(*s, s->n == 7 ? 6 : 7); }); }));
  // notes personnalisées
  menu->addChild(new ui::MenuSeparator);
  menu->addChild(createCheckMenuItem("Custom notes", "", [m, id] { std::lock_guard<std::mutex> lk(m->mtx); HSlot* s = slotById(m, id); return s && s->customOn; },
                                     [m, id] { m->edit([&] { HSlot* s = slotById(m, id); if (s) s->customOn = !s->customOn; }); }));
  menu->addChild(createSubmenuItem("Edit the custom notes", "", [m, id](ui::Menu* sub) {
    int n; { std::lock_guard<std::mutex> lk(m->mtx); HSlot* s = slotById(m, id); n = s ? s->n : 0; }
    sub->addChild(createMenuItem("Reload the scale", "", [m, id] { m->edit([&] { HSlot* s = slotById(m, id); if (s) { HEngine::fillCustom(*s); s->customOn = true; } }); }));
    sub->addChild(createMenuItem("Shuffle the order", "", [m, id] {
      static std::mt19937 rng{std::random_device{}()};
      m->edit([&] { HSlot* s = slotById(m, id); if (s) m->engine.shuffleCustom(*s, rng); });
    }));
    for (int k = 0; k < n; k++)
      sub->addChild(createSubmenuItem("Line " + std::to_string(k + 1), "", [m, id, k](ui::Menu* sub2) {
        for (int w = 0; w < 2; w++)
          sub2->addChild(createSubmenuItem(w ? "Broken line" : "Solid line", "", [m, id, k, w](ui::Menu* sub3) {
            for (int off = -1; off < 24; off++)
              sub3->addChild(createCheckMenuItem(noteLabel(m, id, off), "", [m, id, k, w, off] {
                std::lock_guard<std::mutex> lk(m->mtx); HSlot* s = slotById(m, id);
                return s && k < (int)s->custom.size() && (w ? s->custom[k].b : s->custom[k].p) == off;
              }, [m, id, k, w, off] {
                m->edit([&] { HSlot* s = slotById(m, id); if (s && k < (int)s->custom.size()) { (w ? s->custom[k].b : s->custom[k].p) = off; s->customOn = true; } });
              }));
          }));
      }));
  }));
  menu->addChild(new ui::MenuSeparator);
  menu->addChild(createMenuItem("Reset", "", [m, id] { m->edit([&] { HSlot* s = slotById(m, id); if (s) m->engine.resetSlot(*s); }); }));
  menu->addChild(createCheckMenuItem("Active", "", [m, id] { std::lock_guard<std::mutex> lk(m->mtx); HSlot* s = slotById(m, id); return s && s->active; },
                                     [m, id] { m->edit([&] { HSlot* s = slotById(m, id); if (s) { s->active = !s->active; if (!s->active) { s->phOn = false; } } }); }));
  menu->addChild(createMenuItem("Delete", "", [m, id] { m->edit([&] { m->engine.removeSlot(id); }); }));
}

// ---------------------------------------------------------------------------------------------------------------
// étiquettes du panneau

struct LabelsWidget : widget::TransparentWidget {
  void draw(const DrawArgs& args) override {
    std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
    if (!font) return;
    NVGcontext* vg = args.vg;
    nvgFontFaceId(vg, font->handle);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGB(0xe8, 0xe0, 0xc8));
    nvgFontSize(vg, 11.f);
    nvgText(vg, mm2px(15.24f), mm2px(7.f), "HEXACORDE", NULL);
    nvgFillColor(vg, nvgRGB(0x8a, 0x85, 0x74));
    nvgFontSize(vg, 7.f);
    struct L { float x, y; const char* t; };
    static const L labels[] = {
        {10.2f, 17.f, "TEMPO"}, {22.f, 17.f, "RUN"}, {10.2f, 34.f, "CLOCK"}, {22.f, 34.f, "RESET"},
        {10.2f, 50.f, "PRIME"}, {22.f, 50.f, "ROTATE"}, {10.2f, 79.f, "PITCH"}, {22.f, 79.f, "GATE"},
        {10.2f, 98.f, "VEL"}, {22.f, 98.f, "STEP"},
    };
    for (const L& l : labels) nvgText(vg, mm2px(l.x), mm2px(l.y), l.t, NULL);
  }
};

// ---------------------------------------------------------------------------------------------------------------
// menu du module : réglages du plateau, tonalité, fichiers

struct HexacordeWidget : ModuleWidget {
  HexacordeWidget(Hexacorde* module) {
    setModule(module);
    setPanel(createPanel(asset::plugin(pluginInstance, "res/Hexacorde.svg")));
    addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
    addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

    addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.2f, 24.f)), module, Hexacorde::TEMPO_PARAM));
    addParam(createParamCentered<LEDButton>(mm2px(Vec(22.f, 24.f)), module, Hexacorde::RUN_PARAM));
    addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(22.f, 24.f)), module, Hexacorde::RUN_LIGHT));
    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.2f, 41.f)), module, Hexacorde::CLOCK_INPUT));
    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.f, 41.f)), module, Hexacorde::RESET_INPUT));
    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.2f, 57.f)), module, Hexacorde::PRIME_INPUT));
    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22.f, 57.f)), module, Hexacorde::ROTATE_INPUT));
    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(10.2f, 86.f)), module, Hexacorde::PITCH_OUTPUT));
    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(22.f, 86.f)), module, Hexacorde::GATE_OUTPUT));
    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(10.2f, 105.f)), module, Hexacorde::VEL_OUTPUT));
    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(22.f, 105.f)), module, Hexacorde::STEP_OUTPUT));

    LabelsWidget* lw = new LabelsWidget; lw->box.pos = Vec(0, 0); lw->box.size = box.size; addChild(lw);
    BoardWidget* bw = new BoardWidget;
    bw->module = module;
    bw->box.pos = mm2px(Vec(32.f, 3.f));
    bw->box.size = mm2px(Vec(97.f, 122.5f));
    addChild(bw);
  }

  void appendContextMenu(ui::Menu* menu) override {
    Hexacorde* m = dynamic_cast<Hexacorde*>(module);
    if (!m) return;
    menu->addChild(new ui::MenuSeparator);
    menu->addChild(createMenuLabel("Hexacorde"));
    menu->addChild(createSubmenuItem("Tonality", "", [m](ui::Menu* sub) {
      sub->addChild(createSubmenuItem("Tonic", NOTE_EN[m->engine.opt.tonic], [m](ui::Menu* s2) {
        for (int t = 0; t < 12; t++)
          s2->addChild(createCheckMenuItem(NOTE_EN[t], "", [m, t] { return m->engine.opt.tonic == t; }, [m, t] { m->edit([&] { m->engine.opt.tonic = t; }); }));
      }));
      sub->addChild(createSubmenuItem("Octave", std::to_string(m->engine.opt.octave), [m](ui::Menu* s2) {
        for (int o = -3; o <= 3; o++)
          s2->addChild(createCheckMenuItem(std::to_string(o), "", [m, o] { return m->engine.opt.octave == o; }, [m, o] { m->edit([&] { m->engine.opt.octave = o; }); }));
      }));
      sub->addChild(createSubmenuItem("Scale (all hexagrams, and new ones)", HEngine::scaleDef(m->engine.opt.scale)->en, [m](ui::Menu* s2) {
        for (const hexa::ScaleDef& d : hexa::scaleTable()) {
          std::string key = d.key;
          s2->addChild(createCheckMenuItem(d.en, "", [m, key] { return m->engine.opt.scale == key; },
                                           [m, key] { m->edit([&] { m->engine.opt.scale = key; for (HSlot& s : m->engine.slots) s.mode = key; }); }));
        }
      }));
    }));
    menu->addChild(createSubmenuItem("Settings", "", [m](ui::Menu* sub) {
      sub->addChild(createCheckMenuItem("Line mutation", "", [m] { return m->engine.opt.mutate; }, [m] { m->edit([&] { m->engine.opt.mutate = !m->engine.opt.mutate; }); }));
      sub->addChild(createCheckMenuItem("Uniform octave", "", [m] { return m->engine.opt.uniform; }, [m] { m->edit([&] { m->engine.opt.uniform = !m->engine.opt.uniform; }); }));
      sub->addChild(createSubmenuItem("Soft line range", std::to_string(m->engine.opt.softRange).substr(0, 3), [m](ui::Menu* s2) {
        for (double r : {1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 9.0})
          s2->addChild(createCheckMenuItem(std::to_string(r).substr(0, 3), "", [m, r] { return std::fabs(m->engine.opt.softRange - r) < 1e-6; }, [m, r] { m->edit([&] { m->engine.opt.softRange = r; }); }));
      }));
    }));
    menu->addChild(createSubmenuItem("Grid size", std::to_string(m->engine.cols) + "x" + std::to_string(m->engine.rows), [m](ui::Menu* sub) {
      for (int w = 0; w < 2; w++)
        sub->addChild(createSubmenuItem(w ? "Rows" : "Columns", std::to_string(w ? m->engine.rows : m->engine.cols), [m, w](ui::Menu* s2) {
          for (int n = hexa::N_MIN; n <= hexa::N_MAX; n++) {
            bool ok = w ? m->engine.canResize(m->engine.cols, n) : m->engine.canResize(n, m->engine.rows);
            s2->addChild(createCheckMenuItem(std::to_string(n), ok ? "" : "(a hexagon is in the way)",
                                             [m, w, n] { return (w ? m->engine.rows : m->engine.cols) == n; },
                                             [m, w, n] { m->edit([&] { if (w) m->engine.setGridSize(m->engine.cols, n); else m->engine.setGridSize(n, m->engine.rows); }); }, !ok));
          }
        }));
    }));
    menu->addChild(createMenuItem("New hexagram", "", [m] {
      static std::mt19937 rng{std::random_device{}()};
      m->edit([&] {
        int x = 0, y = 0;
        if ((int)m->engine.slots.size() < hexa::MAX_SLOTS && m->engine.freeCellNear(m->engine.cols / 2, m->engine.rows / 2, x, y)) {
          hexa::SlotInit c; c.gx = x; c.gy = y; c.mode = m->engine.opt.scale;
          for (int i = 0; i < 6; i++) c.lines.push_back(std::uniform_int_distribution<int>(0, 1)(rng));
          m->engine.addSlot(c);
        }
      });
    }));
    menu->addChild(createMenuItem("Draw lines at random", "", [m] { static std::mt19937 rng{std::random_device{}()}; m->edit([&] { m->engine.drawRandom(rng); }); }));
    menu->addChild(createMenuItem("Scatter at random", "", [m] { static std::mt19937 rng{std::random_device{}()}; m->edit([&] { m->engine.scatter(rng); }); }));
    menu->addChild(createMenuItem("Silence (clear the pulses)", "", [m] { m->edit([&] { m->engine.silence(); }); }));
    menu->addChild(new ui::MenuSeparator);
    menu->addChild(createMenuItem("Import a setup (.json)...", "", [m] {
      osdialog_filters* filters = osdialog_filters_parse("Setup:json,txt");
      char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, filters);
      osdialog_filters_free(filters);
      if (path) {
        std::string err;
        if (!m->importFile(path, err)) osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, ("Hexacorde: " + err).c_str());
        free(path);
      }
    }));
    menu->addChild(createMenuItem("Export the setup (.json)...", "", [m] {
      osdialog_filters* filters = osdialog_filters_parse("Setup:json");
      char* path = osdialog_file(OSDIALOG_SAVE, NULL, "hexacorde.json", filters);
      osdialog_filters_free(filters);
      if (path) { m->exportFile(path); free(path); }
    }));
    menu->addChild(createMenuItem("Reset the six starting hexagrams", "", [m] { m->onReset(); }));
  }
};

Model* modelHexacorde = createModel<Hexacorde, HexacordeWidget>("Hexacorde");
