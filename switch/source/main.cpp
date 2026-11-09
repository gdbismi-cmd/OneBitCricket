// 1-Bit Cricket for Nintendo Switch (homebrew) and desktop.
//
// A C++/SDL2 port of app/src/main/assets/index.html. The game logic below
// mirrors the JS function for function (same names, same numbers), so a
// gameplay change made there can be carried across line by line. What
// differs is the platform layer: a 1-bit software renderer standing in for
// the canvas, a pixel-font HUD standing in for the DOM, an SDL synth standing
// in for Web Audio, and Joy-Con / touch input standing in for the pointer
// joystick.
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include <sys/stat.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
#include "font.h"

#define DOT "\x7f"  // middle dot, drawn by drawText

/* ---------------- maths ---------------- */
static const double PI = M_PI, TAU = M_PI * 2, G = 9.8, YC = 8.0;
static std::mt19937 rng((unsigned)time(nullptr));
static double rnd() { return std::uniform_real_distribution<double>(0, 1)(rng); }
static double rand2(double a, double b) { return a + rnd() * (b - a); }
static double clampd(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
static double hyp(double a, double b) { return std::hypot(a, b); }
static int jround(double v) { return (int)std::floor(v + 0.5); }  // Math.round
static double sgn(double v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }

/* ---------------- grounds ---------------- */
struct Stadium {
  const char *id, *name, *shortName, *city;
  double rx, ry, n, pace, bounce, swing, turn, slope, outfield, crowd, rate;
  int lights;
  const char *feat, *pitch, *note;
};
static const Stadium STADIUMS[] = {
  { "lords", "Lord's", nullptr, "London, England", 62, 68, 2, 1.0, .55, .7, .45, .45, 3.8, .42, 1.0, 4, "lords", "Swing",
    "The famous slope runs across the square. After pitching, the ball drifts towards the off side." },
  { "mcg", "MCG", nullptr, "Melbourne, Australia", 80, 84, 2, 1.12, .68, .4, .3, 0, 3.4, .5, .92, 6, "mcg", "Pace",
    "The biggest field on the tour. A six here needs a well-timed, full-power loft." },
  { "eden", "Eden Gardens", nullptr, "Kolkata, India", 65, 67, 2, .95, .5, .3, .9, 0, 3.6, .66, 1.02, 4, "eden", "Spin",
    "The loudest crowd in cricket. The pitch grips and turns as the ball gets older." },
  { "wankhede", "Wankhede", nullptr, "Mumbai, India", 60, 63, 2, 1.0, .62, .5, .6, 0, 3.1, .58, 1.15, 4, "sea", "Flat",
    "Short boundaries and a quick outfield beside the Arabian Sea. A batting paradise." },
  { "newlands", "Newlands", nullptr, "Cape Town, South Africa", 67, 66, 2, 1.05, .6, 1.25, .4, 0, 3.6, .4, .98, 4, "mountain", "Swing",
    "Table Mountain looms over the oaks. The new ball swings both ways in the air." },
  { "edenpark", "Eden Park", nullptr, "Auckland, New Zealand", 72, 55, 3.2, 1.0, .58, .8, .4, 0, 3.4, .48, 1.1, 4, "rect", "Seam",
    "A rugby ground. Straight boundaries are very short; square ones are long. Hit down the ground." },
  { "kensington", "Kensington Oval", nullptr, "Bridgetown, Barbados", 64, 67, 2, 1.12, .68, .6, .35, 0, 3.5, .5, 1.0, 4, "palms", "Pace",
    "Pace and carry under the palms. Expect short balls that climb at the body." },
  { "galle", "Galle", nullptr, "Galle, Sri Lanka", 65, 63, 2, .9, .48, .4, 1.35, 0, 3.9, .22, .95, 0, "fort", "Big turn",
    "Beneath the old fort walls by the Indian Ocean. Spinners rip it square here." },
  { "ahmedabad", "Narendra Modi Stadium", "Ahmedabad", "Ahmedabad, India", 74, 76, 2, 1.0, .58, .4, .7, 0, 3.3, .62, 1.0, 0, "ring", "Balanced",
    "The largest cricket stadium in the world, with about 132,000 seats and long, even boundaries." },
  { "sharjah", "Sharjah", nullptr, "Sharjah, UAE", 58, 60, 2, .92, .52, .3, .85, 0, 3.3, .55, 1.12, 4, "desert", "Dry, slow",
    "Small, hot and tight in the desert. Spinners grip the dry surface, but edges fly to the rope." },
};
static const int NSTAD = sizeof(STADIUMS) / sizeof(STADIUMS[0]);
static const char* sname(const Stadium& s) { return s.shortName ? s.shortName : s.name; }
struct Diff { double speed, win, field; bool marker; double rate; };
enum { EASY, NORMAL, HARD };
static const Diff DIFF[] = {
  { .8, .4, 6.0, true, .86 },
  { 1.0, .32, 6.8, true, 1.0 },
  { 1.15, .24, 7.4, false, 1.12 },
};

/* ---------------- 1-bit framebuffer ---------------- */
static const int W = 640, H = 360;
static uint8_t fb[W * H];  // 1 = white
static uint8_t clipMask[W * H];
static bool clipOn = false;

struct Pat { int w = 1, h = 1; std::vector<uint8_t> v{1}; };
struct Paint { int solid; const Pat* p; };  // solid 0 black, 1 white, -1 pattern
static const Paint BLACK{0, nullptr}, WHITE{1, nullptr};
static Paint pp(const Pat& p) { return {-1, &p}; }
static int patOX = 0, patOY = 0;
static inline int paintAt(const Paint& c, int x, int y) {
  if (c.solid >= 0) return c.solid;
  const Pat& p = *c.p;
  int i = ((x - patOX) % p.w + p.w) % p.w, j = ((y - patOY) % p.h + p.h) % p.h;
  return p.v[j * p.w + i];
}
static inline void put(int x, int y, const Paint& c) {
  if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
  if (clipOn && !clipMask[y * W + x]) return;
  fb[y * W + x] = paintAt(c, x, y);
}
static void fillRect(int x, int y, int w, int h, const Paint& c) {
  if (w < 0) { x += w; w = -w; }
  if (h < 0) { y += h; h = -h; }
  int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(W, x + w), y1 = std::min(H, y + h);
  for (int j = y0; j < y1; j++) for (int i = x0; i < x1; i++) put(i, j, c);
}

// canvas-style path: filled with the nonzero rule, sampled at pixel centres
struct V2 { double x, y; };
struct Path {
  std::vector<std::vector<V2>> subs;
  void begin() { subs.clear(); }
  void moveTo(double x, double y) { subs.push_back({{x, y}}); }
  void lineTo(double x, double y) { if (subs.empty()) moveTo(x, y); else subs.back().push_back({x, y}); }
};
static Path path;
static void fillPolys(const std::vector<std::vector<V2>>& polys, const Paint* c, uint8_t* mask) {
  double ymin = 1e9, ymax = -1e9;
  for (auto& s : polys) for (auto& p : s) { ymin = std::min(ymin, p.y); ymax = std::max(ymax, p.y); }
  int j0 = std::max(0, (int)std::floor(ymin)), j1 = std::min(H - 1, (int)std::ceil(ymax));
  std::vector<std::pair<double, int>> xs;
  for (int j = j0; j <= j1; j++) {
    double yc = j + .5; xs.clear();
    for (auto& s : polys) {
      size_t n = s.size(); if (n < 2) continue;
      for (size_t k = 0; k < n; k++) {
        const V2 &a = s[k], &b = s[(k + 1) % n];
        if ((a.y <= yc) == (b.y <= yc)) continue;
        double x = a.x + (yc - a.y) / (b.y - a.y) * (b.x - a.x);
        xs.push_back({x, b.y > a.y ? 1 : -1});
      }
    }
    std::sort(xs.begin(), xs.end());
    int wind = 0;
    for (size_t k = 0; k + 1 < xs.size(); k++) {
      wind += xs[k].second;
      if (!wind) continue;
      int i0 = std::max(0, (int)std::ceil(xs[k].first - .5)), i1 = std::min(W - 1, (int)std::ceil(xs[k + 1].first - .5) - 1);
      for (int i = i0; i <= i1; i++) { if (mask) mask[j * W + i] = 1; else put(i, j, *c); }
    }
  }
}
static void fillPath(const Paint& c) { fillPolys(path.subs, &c, nullptr); }
static void clipPath() { memset(clipMask, 0, sizeof clipMask); fillPolys(path.subs, nullptr, clipMask); clipOn = true; }
static void bres(int x0, int y0, int x1, int y1, const Paint& c) {
  int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
  for (int k = 0; k < 4000; k++) {
    put(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * e;
    if (e2 >= dy) { e += dy; x0 += sx; }
    if (e2 <= dx) { e += dx; y0 += sy; }
  }
}
static void fillCircle(double cx, double cy, double r, const Paint& c) {
  for (int j = (int)std::floor(cy - r); j <= (int)std::ceil(cy + r); j++)
    for (int i = (int)std::floor(cx - r); i <= (int)std::ceil(cx + r); i++)
      if (hyp(i + .5 - cx, j + .5 - cy) <= r) put(i, j, c);
}
// paths here are always closed (polygons, ellipses, the boundary)
static void strokePath(double lw, const Paint& c) {
  for (auto& s : path.subs) {
    size_t n = s.size(); if (n < 2) continue;
    for (size_t k = 0; k < n; k++) {
      const V2 &a = s[k], &b = s[(k + 1) % n];
      if (lw <= 1.5) { bres((int)std::floor(a.x), (int)std::floor(a.y), (int)std::floor(b.x), (int)std::floor(b.y), c); continue; }
      double dx = b.x - a.x, dy = b.y - a.y, L = hyp(dx, dy);
      if (L < 1e-6) continue;
      double nx = -dy / L * lw / 2, ny = dx / L * lw / 2;
      fillPolys({{{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}}}, &c, nullptr);
      fillCircle(a.x, a.y, lw / 2, c);
    }
  }
}
static void ellipsePath(double cx, double cy, double rx, double ry) {
  path.begin();
  int n = std::max(16, std::min(96, (int)((rx + ry) * 2)));
  for (int i = 0; i < n; i++) { double t = i * TAU / n; double x = cx + std::cos(t) * rx, y = cy + std::sin(t) * ry; i ? path.lineTo(x, y) : path.moveTo(x, y); }
}

/* ---------------- canvas + patterns ---------------- */
static Pat pat(int w, int h, const std::function<bool(int, int)>& fn) {
  Pat p; p.w = w; p.h = h; p.v.assign(w * h, 1);
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) p.v[j * w + i] = fn(i, j) ? 0 : 1;
  return p;
}
static Pat Pgrass, Pgrass2, Ppitch, Pcheck, Pdark, Phatch, Pwaves, Psand, crowdA, crowdB;
static void initPatterns() {
  Pgrass = pat(8, 8, [](int i, int j) { return (i == 0 && j == 0) || (i == 4 && j == 4); });
  Pgrass2 = pat(4, 4, [](int i, int j) { return (i == 0 && j == 0) || (i == 2 && j == 2); });
  Ppitch = pat(2, 2, [](int i, int j) { return i == 0 && j == 0; });
  Pcheck = pat(2, 2, [](int i, int j) { return (i + j) % 2 == 0; });
  Pdark = pat(2, 2, [](int i, int j) { return !(i == 0 && j == 0); });
  Phatch = pat(4, 4, [](int i, int j) { return (i + j) % 4 == 0; });
  Pwaves = pat(12, 6, [](int i, int j) { return !((j == 0 && i < 5) || (j == 3 && i >= 6 && i < 11)); });
  Psand = pat(7, 7, [](int i, int j) { return (i == 1 && j == 2) || (i == 5 && j == 5); });
}
static std::function<double()> srandf(uint32_t seed) {
  auto s = std::make_shared<uint32_t>(seed);
  return [s]() { *s = *s * 1664525u + 1013904223u; return *s / 4294967296.0; };
}
static void makeCrowd(double d, uint32_t seed) {
  auto r = srandf(seed); const int N = 48; std::vector<bool> base;
  for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) base.push_back(j % 3 != 2 && r() < d);
  crowdA = pat(N, N, [&](int i, int j) { return !base[j * N + i]; });
  auto r2 = srandf(seed * 7 + 3);
  crowdB = pat(N, N, [&](int i, int j) { bool v = base[j * N + i]; if (j % 3 != 2 && r2() < 0.4) v = !v; return !v; });
}

/* ---------------- state ---------------- */
enum State { S_MENU, S_INTRO, S_RUNUP, S_DELIVERY, S_PLAY, S_AFTER, S_END };
enum Role { R_F, R_KEEPER, R_BOWLER };
struct F { double x, y, hx, hy; Role role; double tx, ty; bool go; double react, speed, reach; };
struct Match {
  int score = 0, wk = 0, balls = 0, over = 0, overs = 5, target = 0, fours = 0, sixes = 0;
  std::string thisOver;  // one char per ball
  double heat[16] = {};
  int lastHot = -1, lastSec = -1;
  std::string fieldMsg;
  bool training = false, tSpin = false, guide = true;
  double tSpeed = .85;
  int tn = 0, tp = 0, streak = 0, best = 0;
};
struct Delivery { double speed, px, py, swing, turn; bool spin; double x0, y0, z0, vx0, vz0, tc, ts; bool bounced, passed; };
struct Ball { double x, y, z, vx, vy, vz; bool vis; };
enum Kind { K_DEF, K_DRIVE, K_LOFT };
struct Shot { Kind kind; double a, power; bool def; double ts; bool done, tried, hit; };
struct Pt { double t, x, y, z, sp; };
struct Sim { std::vector<Pt> pts; int bIdx; bool six; int firstLand; };
struct Drop { F* f; int i; bool dive; };
enum OType { O_CATCH, O_FIELD, O_BOUNDARY };
struct Outcome {
  OType type; F* f = nullptr; int i = 0; bool dive = false, six = false;
  bool hasDrop = false; Drop drop{};
  Pt p{}; double tF = 0, end = 0, total = 0; int runs = 0; bool ro = false;
};
enum Phase { PH_FLIGHT, PH_HOLD, PH_THROW, PH_DEAD };
struct Play { Sim sim; Outcome o; double t; Phase phase; bool done, msgDrop; double runCap, dropT; double thx, thy, tht0, thT; };
struct Opts { int overs = 5, diff = NORMAL; };

static int sIdx = 0; static const Stadium* st = &STADIUMS[0];
static Opts opts;
static State state = S_MENU; static bool paused = false; static double stateT = 0, gT = 0;
static Match Mstore; static Match* M = nullptr;
static Delivery delStore; static Delivery* del = nullptr;
static Ball ball{}; static bool ballOn = false;
static Shot shotStore; static Shot* shot = nullptr;
static Play playStore; static Play* play = nullptr;
static double dT = 0;
static std::vector<F> fielders; static F *keeper = nullptr, *bowler = nullptr;
struct Batter { double x, y, sw, sa; };
static Batter batS{-0.45, 8.9, 0, 0}, batN{0.65, -9.4, 0, 0};
struct Part { double x, y, z, vx, vy, vz, life; };
static std::vector<Part> parts; static double celebrate = 0; static int stumpsTop = 0, stumpsBot = 0; static bool keeperBall = false;
struct Cam { double x = 0, y = 0, s = 2, ox = 320, oy = 180, visW = 328, visH = 360; } cam;

// HUD state that the JS kept in the DOM
static std::string hintText = "Pull back, release on time";
static std::string flashBig, flashSub; static double flashT = -1;
static struct { double zoneL = .3, zoneR = .7, perfL = .45, perfR = .55, pin = .5; bool pinHidden = true; std::string text = "Hit in the white"; } meter;
static struct { std::string ground, title, line, score, fours, sixes; } endInfo;
enum Screen { SC_MENU, SC_GAME, SC_END };
static Screen screen = SC_MENU;

/* ---------------- geometry ---------------- */
static V2 sePt(double a, double d = 0) {
  double c = std::cos(a), s = std::sin(a), e = 2 / st->n;
  return {(st->rx + d) * sgn(c) * std::pow(std::abs(c), e), (st->ry + d) * sgn(s) * std::pow(std::abs(s), e)};
}
static bool inField(double x, double y) { return std::pow(std::abs(x) / st->rx, st->n) + std::pow(std::abs(y) / st->ry, st->n) < 1; }
static int X(double x) { return jround((x - cam.x) * cam.s + cam.ox); }
static int Y(double y) { return jround((y - cam.y) * cam.s + cam.oy); }
static void sePath(double d) {
  path.begin();
  for (int i = 0; i < 128; i++) { V2 p = sePt(i / 128.0 * TAU, d); i ? path.lineTo(X(p.x), Y(p.y)) : path.moveTo(X(p.x), Y(p.y)); }
}

/* ---------------- drawing helpers ---------------- */
static void disc(int cx, int cy, int r, const Paint& col) {
  if (r <= 0) { fillRect(cx, cy, 1, 1, col); return; }
  for (int dy = -r; dy <= r; dy++) { int w = (int)std::floor(std::sqrt((double)r * r - dy * dy) + 0.5); fillRect(cx - w, cy + dy, 2 * w + 1, 1, col); }
}
static void pline(int x0, int y0, int x1, int y1, const Paint& col) { bres(x0, y0, x1, y1, col); }
static void wRect(double x, double y, double w, double h, const Paint& fill) {
  int x0 = X(x - w / 2), y0 = Y(y - h / 2), x1 = X(x + w / 2), y1 = Y(y + h / 2);
  fillRect(x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0), fill);
}
static void wBox(double x, double y, double w, double h, const Paint& fill = WHITE) {
  wRect(x, y, w, h, BLACK);
  int x0 = X(x - w / 2) + 1, y0 = Y(y - h / 2) + 1, x1 = X(x + w / 2) - 1, y1 = Y(y + h / 2) - 1;
  if (x1 > x0 && y1 > y0) fillRect(x0, y0, x1 - x0, y1 - y0, fill);
}
static void wPoly(const std::vector<V2>& pts, const Paint& fill, const Paint* stroke) {
  path.begin();
  for (size_t i = 0; i < pts.size(); i++) i ? path.lineTo(X(pts[i].x), Y(pts[i].y)) : path.moveTo(X(pts[i].x), Y(pts[i].y));
  fillPath(fill); if (stroke) strokePath(1, *stroke);
}
static void wEll(double x, double y, double rx, double ry, const Paint* fill, const Paint* stroke) {
  ellipsePath(X(x), Y(y), std::max(1.0, rx * cam.s), std::max(1.0, ry * cam.s));
  if (fill) fillPath(*fill);
  if (stroke) strokePath(1, *stroke);
}
static void anchor() { patOX = X(0); patOY = Y(0); }

/* ---------------- ground features ---------------- */
static void lightsTowers(int n) {
  for (int k = 0; k < n; k++) {
    V2 p = sePt(TAU / 8 + k * TAU / n, 37);
    wBox(p.x, p.y, 5, 5, pp(Pcheck));
    int sx = X(p.x), sy = Y(p.y), r = std::max(4, jround(4.5 * cam.s));
    for (int a = 0; a < 8; a++) { double t = a / 8.0 * TAU; fillRect(sx + jround(std::cos(t) * r), sy + jround(std::sin(t) * r), 1, 1, WHITE); }
  }
}
static void palm(double x, double y) {
  int sx = X(x), sy = Y(y), r = std::max(3, jround(3.2 * cam.s));
  for (int a = 0; a < 6; a++) { double t = a / 6.0 * TAU + 0.3; pline(sx, sy, sx + jround(std::cos(t) * r), sy + jround(std::sin(t) * r), WHITE); }
  disc(sx, sy, 1, BLACK);
}
static void featBack() {
  std::string f = st->feat; double rx = st->rx, ry = st->ry;
  if (f == "desert") wRect(0, 0, 3000, 3000, pp(Psand));
  if (f == "sea") wRect(rx + 22 + 500, 0, 1000, 3000, pp(Pwaves));
  if (f == "fort") { wRect(rx + 16 + 500, 0, 1000, 3000, pp(Pwaves)); wRect(0, ry + 18 + 500, 3000, 1000, pp(Pwaves)); }
  if (f == "mountain") {
    double b = -ry - 26;
    wPoly({{-190, b + 6}, {-140, b - 40}, {-110, b - 62}, {-20, b - 66}, {30, b - 58}, {70, b - 30}, {110, b + 6}}, pp(Phatch), &WHITE);
    wRect(-65, b - 64, 90, 1.2, WHITE);
  }
  if (f == "palms") for (int k = 0; k < 22; k++) { V2 p = sePt(k / 22.0 * TAU + 0.1, 36 + (k % 2) * 6); palm(p.x, p.y); }
}
static void featFront() {
  std::string f = st->feat; double rx = st->rx, ry = st->ry;
  Paint chk = pp(Pcheck);
  if (f == "lords") {
    double py = -(ry + 17);
    wBox(0, py, 36, 11);
    for (int r = 0; r < 2; r++) for (int i = -3; i <= 3; i++) wRect(i * 4.4, py - 2 + r * 4, 2.4, 1.6, BLACK);
    wBox(-19.5, py, 6, 9, chk); wBox(19.5, py, 6, 9, chk);
    wEll(0, ry + 17, 14, 5.5, &WHITE, &BLACK); wRect(0, ry + 17, 20, 1.4, BLACK);
  }
  if (f == "eden") { double py = -(ry + 15); wBox(0, py, 26, 9); wBox(-14, py, 5, 9, chk); wBox(14, py, 5, 9, chk); wRect(0, py, 18, 1.4, BLACK); }
  if (f == "mcg" || f == "rect") { wBox(0, ry + 19, 24, 7, BLACK); for (int i = -4; i <= 4; i++) wRect(i * 2.4, ry + 19, 1.2, 2.4, WHITE); }
  if (f == "mountain") for (double a = -0.7; a <= 0.71; a += 0.2) for (double d : {12.0, 23.0}) { V2 p = sePt(a + (d > 15 ? 0.1 : 0), d); wEll(p.x, p.y, 3.4, 3.4, &chk, &BLACK); }
  if (f == "fort") {
    double x0 = rx + 7, x1 = rx + 14; std::vector<V2> pts{{x0, -ry * 0.95}};
    for (double y = -ry * 0.95; y <= ry * 0.95; y += 24) { pts.push_back({x1, y}); pts.push_back({x1 + 6, y + 6}); pts.push_back({x1, y + 12}); }
    pts.push_back({x1, ry * 0.95}); pts.push_back({x0, ry * 0.95});
    wPoly(pts, chk, &WHITE);
    wEll(rx + 24, ry * 0.62, 2.4, 2.4, &WHITE, &BLACK);
  }
  if (f == "ring") {
    sePath(31); strokePath(std::max(1.0, cam.s * 1.2), WHITE);
    double step = TAU / 72;
    for (double a = 0; a < TAU; a += step) { V2 p = sePt(a, 31); fillRect(X(p.x), Y(p.y), 1, 1, BLACK); }
  }
}

/* ---------------- people ---------------- */
static int personR() { return std::max(2, jround(0.34 * cam.s)); }
static void drawFielder(const F& f) {
  int sx = X(f.x), sy = Y(f.y), r = personR();
  disc(sx, sy, r, BLACK);
  if (r >= 3) disc(sx, sy, 0, WHITE);
  if (f.role == R_KEEPER && r >= 2) { fillRect(sx - r, sy - 1, 1, 1, WHITE); fillRect(sx + r, sy - 1, 1, 1, WHITE); }
}
static void drawBatter(const Batter& b, bool withBat) {
  int sx = X(b.x), sy = Y(b.y), r = personR();
  disc(sx, sy, r + 1, BLACK); disc(sx, sy, r, WHITE); if (r >= 3) disc(sx, sy, r - 2, BLACK);
  if (withBat) {
    double a = 1.2, L = std::max(.7, 3 / cam.s);
    if (b.sw > 0) { double u = 1 - b.sw / 0.24; a = b.sa + PI * 0.9 * (1 - u) * (1 - u) - 0.2 * u; L = std::max(.95, 4 / cam.s); }
    pline(sx, sy, sx + jround(std::cos(a) * L * cam.s), sy + jround(std::sin(a) * L * cam.s), BLACK);
  }
}
static void drawUmpire(double x, double y) {
  int sx = X(x), sy = Y(y), r = personR();
  disc(sx, sy, r + 1, BLACK); disc(sx, sy, r, WHITE); if (r >= 2) disc(sx, sy, 0, BLACK);
}
static void drawStumps(double yy, int down) {
  int sx = X(0), sy = Y(yy), w = std::max(3, jround(0.26 * cam.s)), h = std::max(1, jround(0.1 * cam.s));
  if (down > 0) { fillRect(sx - (w >> 1), sy, 1, h, BLACK); fillRect(sx + (w >> 1), sy + 1, 1, h, BLACK); fillRect(sx, sy - 1, 1, h, BLACK); }
  else fillRect(sx - (w >> 1), sy, w, h, BLACK);
}
static void drawBall(double x, double y, double z) {
  int sx = X(x), sy = Y(y);
  if (z > 0.15) { fillRect(sx - 1, sy, 1, 1, BLACK); fillRect(sx + 1, sy, 1, 1, BLACK); }
  int by = sy - jround(z * cam.s * 0.45), r = std::max(1, jround(0.1 * cam.s + std::min(2.0, z * 0.2)));
  disc(sx, by, r + 1, WHITE); disc(sx, by, r, BLACK);
}
static void drawPitch() {
  wRect(0, 0, 3.2, 23, WHITE); wRect(0, 0, 3.2, 23, pp(Ppitch));
  Paint chk = pp(Pcheck);
  wEll(0.55, -7.2, 0.6, 1.3, &chk, nullptr); wEll(-0.55, 7.4, 0.6, 1.2, &chk, nullptr);
  double lw = std::max(1 / cam.s, 0.06);
  for (int g : {-1, 1}) {
    wRect(0, g * 8.78, 3.66, lw, BLACK); wRect(0, g * 10.06, 2.64, lw, BLACK);
    wRect(-1.32, g * 9.7, lw, 2.2, BLACK); wRect(1.32, g * 9.7, lw, 2.2, BLACK);
  }
  drawStumps(-10.06, stumpsTop); drawStumps(10.06, stumpsBot);
}

/* ---------------- sound ---------------- */
// A tiny synth standing in for Web Audio: square/saw tones with a linear
// pitch slide and exponential decay, and band-passed noise bursts.
enum { V_SQUARE, V_SAW, V_NOISE };
struct Voice { int kind; double f, slide, vol, dur, at, t = 0, ph = 0; double b0 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
static std::vector<Voice> voices;
static SDL_AudioDeviceID adev = 0;
static int SR = 48000;
static uint32_t nseed = 22222;
static void audioCb(void*, Uint8* stream, int len) {
  float* out = (float*)stream; int n = len / sizeof(float);
  for (int k = 0; k < n; k++) {
    double s = 0;
    for (auto& v : voices) {
      v.t += 1.0 / SR;
      double lt = v.t - v.at;
      if (lt < 0 || lt >= v.dur) continue;
      double g = v.vol * std::pow(0.0001 / v.vol, lt / v.dur), y;
      if (v.kind == V_NOISE) {
        nseed ^= nseed << 13; nseed ^= nseed >> 17; nseed ^= nseed << 5;
        double x = nseed / 2147483648.0 - 1;
        y = v.b0 * x + v.b2 * v.x2 - v.a1 * v.y1 - v.a2 * v.y2;
        v.x2 = v.x1; v.x1 = x; v.y2 = v.y1; v.y1 = y;
      } else {
        v.ph += (v.f + v.slide * lt / v.dur) / SR; v.ph -= std::floor(v.ph);
        y = v.kind == V_SQUARE ? (v.ph < .5 ? 1 : -1) : 2 * v.ph - 1;
      }
      s += y * g;
    }
    out[k] = (float)clampd(s, -1, 1);
  }
  voices.erase(std::remove_if(voices.begin(), voices.end(), [](const Voice& v) { return v.t - v.at >= v.dur; }), voices.end());
}
static void addVoice(const Voice& v) { if (!adev) return; SDL_LockAudioDevice(adev); voices.push_back(v); SDL_UnlockAudioDevice(adev); }
static void tone(double f, double dur, int type = V_SQUARE, double vol = .06, double at = 0, double slide = 0) {
  Voice v; v.kind = type; v.f = f; v.slide = slide; v.vol = vol; v.dur = dur; v.at = at; addVoice(v);
}
static void noise(double dur, double vol = .1, double freq = 1200, double at = 0) {
  Voice v; v.kind = V_NOISE; v.f = freq; v.slide = 0; v.vol = vol; v.dur = dur; v.at = at;
  double w0 = TAU * freq / SR, alpha = std::sin(w0) / 2, a0 = 1 + alpha;  // RBJ band-pass, Q = 1
  v.b0 = alpha / a0; v.b2 = -alpha / a0; v.a1 = -2 * std::cos(w0) / a0; v.a2 = (1 - alpha) / a0;
  addVoice(v);
}
namespace sfx {
static void hit(double q) { noise(.05, .35, 2400); tone(160 + q * 160, .06, V_SQUARE, .07); }
static void four() { double fs[] = {523, 659, 784}; for (int i = 0; i < 3; i++) tone(fs[i], .12, V_SQUARE, .05, i * .09); noise(1.4, .08, 700, .1); }
static void six() { double fs[] = {523, 659, 784, 1046}; for (int i = 0; i < 4; i++) tone(fs[i], .14, V_SQUARE, .05, i * .09); noise(2, .1, 700, .1); }
static void out() { tone(440, .5, V_SAW, .05, 0, -300); noise(.8, .06, 500, .1); }
static void tick() { tone(900, .03, V_SQUARE, .03); }
}

/* ---------------- field ---------------- */
static F mkF(double x, double y, Role role = R_F) {
  double sp = DIFF[opts.diff].field;
  return { x, y, x, y, role, x, y, false, role == R_KEEPER ? .1 : .3, role == R_KEEPER ? sp * .8 : sp, role == R_KEEPER ? 1.3 : 1.0 };
}
static const int SECT = 16; static const double SW = TAU / SECT;
static double normA(double a) { while (a > PI) a -= TAU; while (a <= -PI) a += TAU; return a; }
static double secCen(int k) { return -PI + (k + .5) * SW; }
static int secOf(double rel) { return (int)std::floor((normA(rel) + PI) / SW) % SECT; }
static std::string relName(double rel) {
  double d = std::abs(rel) * 180 / PI; bool off = rel > 0;
  if (d < 35) return off ? "long off" : "long on";
  if (d < 70) return off ? "extra cover" : "midwicket";
  if (d < 110) return off ? "point" : "square leg";
  return off ? "third man" : "fine leg";
}
static double rayEdge(double a) { double t = 10; while (t < 220 && inField(std::cos(a) * t, 8.9 + std::sin(a) * t)) t += 1; return t; }
static void planField(bool first);
static void setHomes();
static void setupField() {
  fielders.clear(); fielders.reserve(16);
  for (int i = 0; i < 9; i++) fielders.push_back(mkF(0, 0));
  fielders.push_back(mkF(0.2, 15, R_KEEPER)); keeper = &fielders.back();
  fielders.push_back(mkF(0.9, -24, R_BOWLER)); bowler = &fielders.back();
  planField(true);
  setHomes();
}
static void planField(bool first) {
  double zero[SECT] = {}; const double* heat = M ? M->heat : zero;
  auto pick = [](const double* score, int n, int sep) {
    std::vector<int> order(SECT); for (int k = 0; k < SECT; k++) order[k] = k;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return score[b] < score[a]; });
    std::vector<int> out;
    for (int k : order) {
      bool ok = true; for (int o : out) { int d = std::abs(o - k); if (std::min(d, SECT - d) < sep) ok = false; }
      if (ok) out.push_back(k);
      if ((int)out.size() == n) break;
    }
    return out;
  };
  double dS[SECT], iS[SECT];
  for (int k = 0; k < SECT; k++) {
    double ar = std::abs(secCen(k));
    dS[k] = (ar < .8 ? 1 : ar < 1.9 ? .75 : .35) + 1.7 * heat[k] + rand2(0, .4);
    iS[k] = (ar < 2 ? .8 : .55) + 1.0 * heat[k] + rand2(0, .5);
  }
  auto deep = pick(dS, 4, 2), inner = pick(iS, 5, 2);
  std::vector<V2> slots;
  for (int k : deep) { double rel = secCen(k) + rand2(-.1, .1), a = rel - PI / 2, t = rayEdge(a) - rand2(4, 8); slots.push_back({std::cos(a) * t, 8.9 + std::sin(a) * t}); }
  for (int k : inner) { double rel = secCen(k) + rand2(-.12, .12), a = rel - PI / 2, r = std::abs(rel) > 1.9 ? rand2(13, 17) : rand2(19, 25); slots.push_back({std::cos(a) * r, 8.9 + std::sin(a) * r}); }
  std::vector<F*> freeF; for (auto& f : fielders) if (f.role == R_F) freeF.push_back(&f);
  for (auto& s : slots) {
    size_t bi = 0; double bd = 1e18;
    for (size_t i = 0; i < freeF.size(); i++) { double d = hyp(freeF[i]->x - s.x, freeF[i]->y - s.y); if (d < bd) { bd = d; bi = i; } }
    F* f = freeF[bi]; freeF.erase(freeF.begin() + bi); f->hx = s.x; f->hy = s.y; if (first) { f->x = s.x; f->y = s.y; }
  }
  // announce when the captain plugs your favourite zone
  if (M) {
    int hk = 0; for (int k = 1; k < SECT; k++) if (heat[k] > heat[hk]) hk = k;
    bool covered = false; for (int k : deep) { int d = std::abs(k - hk); if (std::min(d, SECT - d) <= 1) covered = true; }
    M->fieldMsg = heat[hk] > 1.4 && covered && M->lastHot != hk ? "Field moved: deep " + relName(secCen(hk)) : "";
    if (!M->fieldMsg.empty()) M->lastHot = hk;
  }
}
static void recordHit(double vx, double vy, double w) {
  if (!M) return;
  int k = secOf(std::atan2(vy, vx) + PI / 2);
  M->heat[k] += w; M->heat[(k + 1) % SECT] += w * .45; M->heat[(k + SECT - 1) % SECT] += w * .45;
  M->lastSec = k;
}
static void setHomes() { bool spin = M && (M->training ? M->tSpin : M->over % 2 == 1); keeper->hy = spin ? 11.7 : 15; }
static bool moveToward(F& f, double tx, double ty, double step) {
  double dx = tx - f.x, dy = ty - f.y, d = hyp(dx, dy);
  if (d <= step || d < 1e-6) { f.x = tx; f.y = ty; return true; }
  f.x += dx / d * step; f.y += dy / d * step; return false;
}

/* ---------------- HUD hooks ---------------- */
static FILE* tlog = nullptr;  // autotest event log
static void hint(const std::string& t) { hintText = t; if (tlog) fprintf(tlog, "%.2f hint %s\n", gT, t.c_str()); }
static void flash(const std::string& big, const std::string& sub) { flashBig = big; flashSub = sub; flashT = 0; if (tlog) fprintf(tlog, "%.2f flash %s | %s\n", gT, big.c_str(), sub.c_str()); }
static void setMeter();
static void judge(double err, int kind);
static void scoreBall(int r, char sym);
static void outBall(const std::string& how, const std::string& sub);
static void endBall(double delay);
static void launch(double x, double y, double z, double vx, double vy, double vz);

/* ---------------- delivery ---------------- */
static Delivery makeDelivery() {
  const Diff& D = DIFF[opts.diff]; bool spin = M->training ? M->tSpin : M->over % 2 == 1;
  double speed = (spin ? rand2(13, 15.5) : rand2(18.5, 22.5) * st->pace) * (M->training ? M->tSpeed : D.speed);
  double r = rnd();
  double py = spin ? rand2(2.5, 6.8) : r < .15 ? rand2(-1.5, 1.5) : r < .75 ? rand2(2, 6) : r < .92 ? rand2(6, 7.8) : rand2(8.1, 8.5);
  double px = rand2(-0.35, 0.6);
  double swing = spin ? 0 : rand2(-1, 1) * st->swing * 1.6;
  double turn = spin ? (rnd() < .6 ? -1 : 1) * rand2(.3, 1) * st->turn * 2 : rand2(-.4, .4);
  double x0 = .55, y0 = -9.6, z0 = 2.1, Tp = (py - y0) / speed;
  return { speed, px, py, swing, turn, spin, x0, y0, z0,
    (px - x0 - .5 * swing * Tp * Tp) / Tp, (.5 * G * Tp * Tp - z0) / Tp,
    (YC - y0) / speed, (10.06 - y0) / speed, false, false };
}
static void newBall() {
  state = S_RUNUP; stateT = 0; shot = nullptr; play = nullptr; keeperBall = false;
  stumpsTop = stumpsBot = 0;
  if (M->balls % 6 == 0 && !M->training) M->thisOver.clear();
  delStore = makeDelivery(); del = &delStore;
  setMeter(); ballOn = false;
  bowler->x = .9; bowler->y = -24;
  batS.x = -.45; batS.y = 8.9; batS.sw = 0; batN.x = .65; batN.y = -9.4;
  for (auto& f : fielders) { if (f.role == R_KEEPER) { f.x = f.hx; f.y = f.hy; } f.go = false; }
  hint(!M->fieldMsg.empty() ? M->fieldMsg : (M->balls < 6 ? "Pull back, release on time" : (del->spin ? "Spinner on" : "Quick on")));
  M->fieldMsg = "";
}
static const double RUNUP = 1.25;
static void release() {
  state = S_DELIVERY; dT = 0;
  ball = { del->x0, del->y0, del->z0, del->vx0, del->speed, del->vz0, true }; ballOn = true;
}
static bool isShort();
static void resolveShot(double q, int side);
static void breakStumps(double yy) {
  if (yy > 0) stumpsBot = 1; else stumpsTop = 1;
  for (int i = 0; i < 6; i++) parts.push_back({ rand2(-.15, .15), yy, .7, rand2(-3, 3), rand2(-1, 4) * sgn(yy), rand2(2, 5), 1.4 });
}
static void stepDelivery(double dt) {
  const Diff& D = DIFF[opts.diff]; Ball& b = ball;
  dT += dt;
  if (!del->bounced) b.vx += del->swing * dt;
  b.vz -= G * dt; b.x += b.vx * dt; b.y += b.vy * dt; b.z += b.vz * dt;
  if (b.z <= 0 && b.vz < 0) {
    b.z = 0;
    if (!del->bounced) { del->bounced = true; b.vz = -b.vz * st->bounce; b.vx += del->turn + st->slope * .5; sfx::tick(); if (isShort() && !shot) hint("Short, pull it"); }
    else b.vz = -b.vz * .4;
  }
  // bowler follow-through
  F& bw = *bowler; moveToward(bw, 1.4, -5.5, std::max(0.0, 6 - dT * 5) * dt);
  keeper->x += (clampd(b.x, -1.5, 1.8) - keeper->x) * std::min(1.0, dt * 6);
  if (shot && !shot->done) {
    double err = shot->ts - del->tc;
    double lateW = (10.25 - YC) / del->speed;
    if (err < -D.win) { shot->done = true; hint("Too early"); judge(err, 1); }
    else if (err <= 0) { if (dT >= del->tc) { shot->done = true; judge(err, 0); resolveShot(1 + err / D.win, -1); } }
    else { shot->done = true; if (b.y < 10.25) { judge(err, 0); resolveShot(1 - err / lateW, 1); } else { hint("Too late"); judge(err, 1); } }
    if (state != S_DELIVERY) return;
  }
  if (!del->passed && b.y >= 10.06) {
    del->passed = true;
    if (std::abs(b.x) < .14 && b.z < .72) { if (!shot) judge(0, 2); breakStumps(10.06); outBall(rnd() < .35 ? "LBW" : "BOWLED", ""); return; }
  }
  if (b.y >= keeper->y - .3) {
    b.x = keeper->x; b.y = keeper->y; b.z = .5; keeperBall = true;
    if (!shot) judge(0, 2);
    if (shot && shot->done && !shot->hit && shot->tried) hint("Beaten"); else if (!shot) hint("Left alone");
    scoreBall(0, '\x7f'); endBall(1.0);
  }
}

/* ---------------- shots ---------------- */
static const double LOFT_AT = .78, DEF_BELOW = .16;
static void triggerShot(double a, double power) {
  if (state != S_DELIVERY || shot) return;
  Kind kind = power < DEF_BELOW ? K_DEF : power >= LOFT_AT ? K_LOFT : K_DRIVE;
  shotStore = { kind, a, power, kind == K_DEF, dT, false, true, false }; shot = &shotStore;
}
enum { F_NONE, F_DOWN, F_SKIED, F_TOPEDGE };
// A full pull asks for a loft, but whether the ball goes up depends on the
// delivery and the timing: half-volleys and short balls are easy to get
// under, good length and yorkers less so, and yanking the stick all the way
// to the rim overhits it.
static int loftMiss(double q, const Ball& b, bool pull = false) {
  double py = del->py;
  double c = pull ? .95 : py > 8 ? .4 : py > 6 ? 1 : py < 2 ? .85 : .7;
  if (del->spin) c = std::min(1.0, c + .12);
  c *= .35 + .65 * q;
  if (shot->power > .96) c *= .82;
  if (b.z > 1.3 && !pull) c *= .7;
  if (rnd() < c) return F_NONE;
  return b.z > 1.1 || q < .55 || rnd() < .3 ? F_SKIED : F_DOWN;
}
// Aimed square or behind square on the leg side (-x): a cross-bat pull.
static bool legSquare(double a) { return a < -2.0 || a > 1.9; }
static bool isShort() { return del->py < (del->spin ? 3.2 : 2.6); }
static void resolveShot(double q, int side) {
  q = .3 + .7 * clampd(q, 0, 1);
  const Ball& b = ball; Kind k = shot->kind; double up = -PI / 2;
  if (b.x < -1.6 || b.x > 1.9 || b.z > 2.6) { hint("Beaten"); return; }
  bool cross = k != K_DEF && legSquare(shot->a);
  bool pull = cross && isShort() && b.z > .4;
  // cross-batting a pitched-up ball risks missing it altogether
  if (cross && del->py > 5.5 && rnd() < .15 + .6 * (1 - q)) { hint("Played across the line"); return; }
  double a = shot->a + rand2(-.08, .08);
  if (side < 0) a -= .5 * (1 - q); else a += .4 * (1 - q);
  double hs, vz, z0 = .3; int fail = F_NONE;
  double Pd = clampd((shot->power - DEF_BELOW) / (LOFT_AT - DEF_BELOW), 0, 1), Pl = clampd((shot->power - LOFT_AT) / (1 - LOFT_AT), 0, 1);
  if (k == K_DEF) { hs = 3 + 5 * q; vz = 0; a = up + rand2(-.6, .6); }
  else if (pull && !del->spin && q < .5 && rnd() < .55) {
    // hurried by the bouncer: top edge over the keeper or towards fine leg
    fail = F_TOPEDGE; a = rand2(1.95, 2.8); hs = rand2(10, 16); vz = rand2(9, 13); z0 = 1.2;
  }
  else if (pull && k == K_LOFT && !(fail = loftMiss(q, b, true))) {
    hs = (17 + 12 * Pl) * (.45 + .55 * q) + .15 * del->speed * q; vz = (9 + 5 * Pl) * (.6 + .4 * q); z0 = 1.1;
  }
  else if (pull && !fail) {
    // rolled the wrists: hard and flat, using the bowler's pace
    hs = (10 + 20 * Pd) * (.4 + .6 * q) + .35 * del->speed * q; vz = q < .6 ? rand2(2, 5) : rand2(0, 1.5); z0 = 1;
  }
  else if (!pull && side > 0 && q < .5 && b.x > -.1 && rnd() < .4) { a = PI / 2 - rand2(.1, .5); hs = del->speed * rand2(.45, .65); vz = rand2(.5, 2.2); z0 = .6; }
  else if (k == K_LOFT && (fail || (fail = loftMiss(q, b)))) {
    // went for the big one but didn't get under it
    if (fail == F_SKIED) { hs = rand2(6, 11); vz = rand2(12, 16); z0 = 1; a += rand2(-.5, .5); }
    else { hs = (16 + 10 * Pl) * (.45 + .55 * q); vz = rand2(0, 1.5); }
  }
  else if (k == K_LOFT) { hs = (15 + 12 * Pl) * (.45 + .55 * q); vz = (10 + 5 * Pl) * (.6 + .4 * q); z0 = .8; if (q < .45) { vz *= 1.2; hs *= .75; } }
  else { hs = (7 + 22 * Pd) * (.4 + .6 * q) + .2 * del->speed * q; vz = q < .45 ? rand2(1, 3) : 0; }
  shot->hit = true;
  batS.sw = .24; batS.sa = a;
  sfx::hit(q);
  if (fail == F_TOPEDGE) hint("Top edge off the pull");
  else if (fail == F_SKIED) hint("Top edge, skied it"); else if (fail) hint("Kept it down");
  else if (pull && q > .8) hint(k == K_LOFT ? "Pulled away, high" : "Pulled hard");
  else if (q > .88 && k != K_DEF) hint("Middled it"); else if (q < .45) hint("Mistimed");
  launch(b.x, b.y, z0, std::cos(a) * hs, std::sin(a) * hs, vz);
}
static Sim simulate(double x, double y, double z, double vx, double vy, double vz) {
  Sim s; s.pts.reserve(900); double t = 0; int firstLand = (z <= .35 && vz <= 0) ? 0 : -1;
  if (firstLand == 0) z = 0;
  const double dt = 1 / 60.0;
  for (int i = 0; i < 60 * 14; i++) {
    s.pts.push_back({ t, x, y, z, hyp(vx, vy) });
    if (!inField(x, y)) { s.bIdx = i; s.six = firstLand < 0; s.firstLand = firstLand; return s; }
    if (z > 0 || vz > 0) {
      vz -= G * dt; double k = 1 - .06 * dt; vx *= k; vy *= k;
      x += vx * dt; y += vy * dt; z += vz * dt;
      if (z <= 0) { z = 0; if (firstLand < 0) firstLand = i + 1; if (vz < -3) { vz = -vz * .3; vx *= .7; vy *= .7; } else vz = 0; }
    } else {
      double sp = hyp(vx, vy);
      if (sp < .3) break;
      double k = std::max(0.0, sp - st->outfield * dt) / sp; vx *= k; vy *= k;
      vx += st->slope * .25 * dt;
      x += vx * dt; y += vy * dt;
    }
    t += dt;
  }
  s.bIdx = -1; s.six = false; s.firstLand = firstLand; return s;
}
struct FieldOpt { bool hasT = false; double tOverride = 0, extra = 0; bool hasDrop = false; Drop drop{}; };
static Outcome fieldedAt(const Sim& sim, F* f, int i, const FieldOpt& o) {
  i = std::min(i, (int)sim.pts.size() - 1);
  const Pt& p = sim.pts[i];
  double tF = o.hasT ? o.tOverride : p.t + o.extra;
  double nearE = p.y > 0 ? 10.06 : -10.06;
  double thr = hyp(p.x, p.y - nearE), total = tF + .45 + thr / 26;
  double runsF = (total - .3) / 3.0;
  int runs = (int)clampd(std::floor(runsF), 0, 3);
  bool ro = runs < 3 && runsF - runs > .78 && rnd() < .35;
  double end = nearE;
  if (ro) { end = runs % 2 == 0 ? -10.06 : 10.06; thr = hyp(p.x, p.y - end); total = tF + .45 + thr / 26; }
  Outcome out; out.type = O_FIELD; out.f = f; out.i = i; out.p = p; out.tF = tF; out.end = end; out.total = total; out.runs = runs; out.ro = ro;
  out.hasDrop = o.hasDrop; out.drop = o.drop;
  return out;
}
static Outcome predict(const Sim& sim) {
  const auto& pts = sim.pts; int fl = sim.firstLand < 0 ? (int)pts.size() : sim.firstLand;
  int end = sim.bIdx >= 0 ? sim.bIdx : (int)pts.size();
  bool hasC = false; Drop c{};
  for (auto& f : fielders) {
    for (int i = 0; i < std::min(fl, end); i++) {
      const Pt& p = pts[i]; if (p.z > 2.7) continue;
      double av = std::max(0.0, p.t - f.react) * f.speed, d = hyp(p.x - f.x, p.y - f.y);
      if (d <= av + f.reach + 1.6) { if (!hasC || i < c.i) { c = { &f, i, d > av + f.reach }; hasC = true; } break; }
    }
  }
  if (hasC) {
    double pr = c.dive ? .35 : .9; if (c.f->role == R_KEEPER) pr = c.dive ? .5 : .94;
    if (opts.diff == EASY) pr *= .85;
    if (rnd() < pr) { Outcome o; o.type = O_CATCH; o.f = c.f; o.i = c.i; o.dive = c.dive; return o; }
    int di = std::min(fl + 20, (int)pts.size() - 1);
    if (sim.bIdx >= 0 && sim.bIdx <= di) { Outcome o; o.type = O_BOUNDARY; o.i = sim.bIdx; o.six = sim.six; o.hasDrop = true; o.drop = c; return o; }
    FieldOpt fo; fo.hasDrop = true; fo.drop = c; fo.extra = .6;
    return fieldedAt(sim, c.f, std::max(di, c.i + 1), fo);
  }
  bool hasG = false; F* gf = nullptr; int gi = 0;
  for (auto& f : fielders) {
    for (int i = 0; i < end; i++) {
      const Pt& p = pts[i]; if (p.z > 1.6) continue;
      double av = std::max(0.0, p.t - f.react) * f.speed, d = hyp(p.x - f.x, p.y - f.y);
      if (d <= av + (p.sp > 22 ? .7 : 1.0)) { if (!hasG || i < gi) { gf = &f; gi = i; hasG = true; } break; }
    }
  }
  if (hasG) return fieldedAt(sim, gf, gi, {});
  if (sim.bIdx >= 0) { Outcome o; o.type = O_BOUNDARY; o.i = sim.bIdx; o.six = sim.six; return o; }
  const Pt& last = pts.back(); F* bf = nullptr; double bt = 0;
  for (auto& f : fielders) { double t = f.react + std::max(0.0, hyp(last.x - f.x, last.y - f.y) - .8) / f.speed; if (!bf || t < bt) { bf = &f; bt = t; } }
  FieldOpt fo; fo.hasT = true; fo.tOverride = std::max(bt, last.t);
  return fieldedAt(sim, bf, (int)pts.size() - 1, fo);
}
static void launch(double x, double y, double z, double vx, double vy, double vz) {
  recordHit(vx, vy, 1 + (hyp(vx, vy) > 20 ? .8 : 0) + (vz > 5 ? .5 : 0));
  Sim sim = simulate(x, y, z, vx, vy, vz); Outcome o = predict(sim);
  playStore = Play{}; playStore.sim = std::move(sim); playStore.o = o; play = &playStore;
  const auto& pts = play->sim.pts;
  play->t = 0; play->phase = PH_FLIGHT; play->done = false; play->msgDrop = false; play->runCap = 0;
  play->dropT = o.hasDrop ? pts[o.drop.i].t : -1;
  Pt target;
  if (o.type == O_CATCH) { target = pts[o.i]; o.f->tx = target.x; o.f->ty = target.y; o.f->go = true; play->runCap = .45; }
  else if (o.type == O_FIELD) { target = o.p; o.f->tx = o.p.x; o.f->ty = o.p.y; o.f->go = true; play->runCap = o.runs + (o.ro ? .82 : 0); }
  else {
    target = pts[o.i]; F* best = nullptr; double bd = 0;
    for (auto& f : fielders) { double d = hyp(f.x - target.x, f.y - target.y); if (!best || d < bd) { best = &f; bd = d; } }
    best->tx = target.x; best->ty = target.y; best->go = true; play->runCap = 1;
  }
  if (o.hasDrop) { o.drop.f->tx = pts[o.drop.i].x; o.drop.f->ty = pts[o.drop.i].y; o.drop.f->go = true; }
  for (auto& f : fielders) {
    if (f.go) continue;
    if (&f == keeper) { f.tx = 0; f.ty = 10.9; f.go = true; continue; }
    if (&f == bowler) { f.tx = 0; f.ty = -10.9; f.go = true; continue; }
    double d = hyp(f.x - target.x, f.y - target.y);
    if (d < 32 && d > 8) { double k = (d - 8) / d; f.tx = f.x + (target.x - f.x) * k; f.ty = f.y + (target.y - f.y) * k; f.go = true; }
  }
  state = S_PLAY;
}
static void stepPlay(double dt) {
  Play& Pl = *play; Outcome& o = Pl.o; const auto& pts = Pl.sim.pts;
  Pl.t += dt;
  for (auto& f : fielders) if (f.go && Pl.t > f.react) {
    if (o.hasDrop && &f == o.drop.f && Pl.t > Pl.dropT && o.type == O_FIELD) { f.tx = o.p.x; f.ty = o.p.y; }
    moveToward(f, f.tx, f.ty, f.speed * dt);
  }
  // runners
  double p = clampd((Pl.t - .25) / 3.0, 0, Pl.runCap); int k = (int)std::floor(p); double fr = p - k;
  double from = k % 2 == 0 ? 8.9 : -8.9;
  batS.y = from + (-from - from) * fr; batS.x = -.7;
  batN.y = -batS.y; batN.x = .7;
  if (o.hasDrop && !Pl.msgDrop && Pl.t >= Pl.dropT) { Pl.msgDrop = true; flash("DROPPED", o.drop.dive ? "Full-length dive" : "Put down"); }
  if (Pl.phase == PH_FLIGHT) {
    double evT = o.type == O_FIELD ? o.tF : pts[o.i].t;
    int idx = std::min((int)pts.size() - 1, (int)std::floor(Pl.t * 60));
    const Pt& q = pts[o.type == O_FIELD ? std::min(idx, o.i) : idx];
    ball.x = q.x; ball.y = q.y; ball.z = q.z;
    if (Pl.t >= evT) {
      if (o.type == O_CATCH) {
        Pl.phase = PH_DEAD;
        std::string who = o.f == keeper ? "Caught behind" : o.f == bowler ? "Caught and bowled" : o.dive ? "Diving catch" : "Taken in the deep";
        outBall("CAUGHT", o.f->role == R_F && hyp(o.f->x, o.f->y) < 35 && !o.dive ? "Straight to the fielder" : who);
      } else if (o.type == O_BOUNDARY) {
        Pl.phase = PH_DEAD; Pl.runCap = std::min(Pl.runCap, p);
        int r = o.six ? 6 : 4; if (r == 6) M->sixes++; else M->fours++;
        if (M->lastSec >= 0) M->heat[M->lastSec] += r == 6 ? 2 : 1.4;
        celebrate = 2; if (r == 6) sfx::six(); else sfx::four();
        flash(r == 6 ? "SIX!" : "FOUR!", r == 6 ? std::to_string(jround(hyp(pts[o.i].x, pts[o.i].y - 8.9))) + " m" : "Into the rope");
        scoreBall(r, (char)('0' + r)); endBall(1.8);
      } else {
        ball.x = o.f->x; ball.y = o.f->y; ball.z = .6; Pl.phase = PH_HOLD;
      }
    }
  } else if (Pl.phase == PH_HOLD) {
    ball.x = o.f->x; ball.y = o.f->y;
    if (Pl.t >= o.tF + .45) { Pl.phase = PH_THROW; Pl.thx = o.f->x; Pl.thy = o.f->y; Pl.tht0 = Pl.t; Pl.thT = o.total - Pl.t; }
  } else if (Pl.phase == PH_THROW) {
    double u = clampd((Pl.t - Pl.tht0) / std::max(.05, Pl.thT), 0, 1);
    double L = hyp(Pl.thx, Pl.thy - o.end);
    ball.x = Pl.thx + (0 - Pl.thx) * u; ball.y = Pl.thy + (o.end - Pl.thy) * u; ball.z = .6 + 4 * std::min(6.0, L * .08) * u * (1 - u);
    if (u >= 1) {
      Pl.phase = PH_DEAD;
      if (o.ro) { breakStumps(o.end); M->score += o.runs; outBall("RUN OUT", o.runs ? "Went for " + std::to_string(o.runs + 1) : "Went for a quick single"); }
      else { scoreBall(o.runs, o.runs ? (char)('0' + o.runs) : '\x7f'); if (o.runs) hint(o.runs == 1 ? "1 run" : std::to_string(o.runs) + " runs"); endBall(.9); }
    }
  }
}

/* ---------------- training ---------------- */
static const double MRANGE = .45;
static void setMeter() {
  const Diff& D = DIFF[opts.diff]; double lateW = std::min(MRANGE, (10.25 - YC) / del->speed), e = std::min(MRANGE, D.win);
  auto pct = [](double v) { return .5 + v / MRANGE * .5; };
  meter.zoneL = pct(-e); meter.zoneR = pct(lateW); meter.perfL = pct(-.05); meter.perfR = pct(.05);
}
// kind: 0 hit, 1 miss, 2 none
static void judge(double err, int kind) {
  if (!M) return;
  bool T = M->training;
  std::string label; char sym;
  if (kind == 2) { label = "No shot"; sym = '-'; meter.pinHidden = true; }
  else {
    int ms = jround(std::abs(err) * 1000);
    if (std::abs(err) <= .05) { label = "Perfect"; sym = 'P'; }
    else if (kind == 0) { label = std::string(err < 0 ? "Early " : "Late ") + std::to_string(ms) + " ms"; sym = err < 0 ? 'E' : 'L'; }
    else { label = std::string(err < 0 ? "Way early " : "Too late ") + std::to_string(ms) + " ms"; sym = 'X'; }
    meter.pinHidden = false; meter.pin = .5 + clampd(err / MRANGE, -1, 1) * .5;
  }
  meter.text = label;
  if (sym == 'P') tone(1320, .08, V_SQUARE, .05);
  if (T) {
    M->tn++; if (sym == 'P') { M->tp++; M->streak++; } else M->streak = 0;
    M->best = std::max(M->best, M->streak);
    M->thisOver.push_back(sym); if (M->thisOver.size() > 8) M->thisOver.erase(0, 1);
  }
}

/* ---------------- scoring ---------------- */
static double afterDelay = 1.2;
static void scoreBall(int r, char sym) { M->score += r; M->balls++; if (!M->training) M->thisOver.push_back(sym); }
static void outBall(const std::string& how, const std::string& sub) {
  M->wk++; M->balls++; if (!M->training) M->thisOver.push_back('W');
  sfx::out(); flash(how, !sub.empty() ? sub : std::to_string(M->score) + "/" + std::to_string(M->wk)); endBall(1.9);
}
static void endBall(double delay) {
  state = S_AFTER; stateT = 0; if (play) play->done = true; afterDelay = delay;
  for (int k = 0; k < SECT; k++) M->heat[k] *= .78;
  planField(false);
}
static void endMatch(int res);
static void nextOrEnd() {
  if (M->training) return newBall();
  int tot = M->overs * 6;
  if (M->score >= M->target) return endMatch(0);
  if (M->wk >= 10 || M->balls >= tot) return endMatch(M->score == M->target - 1 ? 2 : 1);
  if (M->balls % 6 == 0) {
    M->over++; setHomes();
    flash("OVER " + std::to_string(M->over), std::to_string(M->score) + "/" + std::to_string(M->wk) + " " DOT " need " + std::to_string(M->target - M->score));
    state = S_INTRO; stateT = .3; return;
  }
  newBall();
}

/* ---------------- match flow ---------------- */
struct Rec { int p = 0, w = 0; };
static std::string savePath() {
#ifdef __SWITCH__
  mkdir("sdmc:/switch", 0777); mkdir("sdmc:/switch/OneBitCricket", 0777);
  return "sdmc:/switch/OneBitCricket/records.txt";
#else
  const char* base = SDL_GetPrefPath("onebit", "OneBitCricket");
  std::string p = base ? std::string(base) + "records.txt" : "records.txt";
  return p;
#endif
}
static std::map<std::string, Rec> store() {
  std::map<std::string, Rec> s; FILE* f = fopen(savePath().c_str(), "r"); if (!f) return s;
  char id[64]; int p, w; while (fscanf(f, "%63s %d %d", id, &p, &w) == 3) s[id] = { p, w };
  fclose(f); return s;
}
static void save(const std::map<std::string, Rec>& s) {
  FILE* f = fopen(savePath().c_str(), "w"); if (!f) return;
  for (auto& kv : s) fprintf(f, "%s %d %d\n", kv.first.c_str(), kv.second.p, kv.second.w);
  fclose(f);
}
static void showScreen(Screen which) { screen = which; paused = false; }
static void startMatch(bool training = false) {
  int balls = opts.overs * 6;
  int target = jround(balls * 1.32 * st->rate * DIFF[opts.diff].rate * rand2(.92, 1.08)) + 1;
  Mstore = Match{}; M = &Mstore;
  M->overs = opts.overs; M->target = target; M->training = training;
  setupField();
  showScreen(SC_GAME);
  meter.text = "Hit in the white"; meter.pinHidden = true;
  hint("Get ready");
  if (training) flash("TRAINING", "Press as the ball reaches the bat");
  else flash("CHASE " + std::to_string(target), std::to_string(opts.overs) + " overs at " + sname(*st));
  state = S_INTRO; stateT = 0; del = nullptr; ballOn = false; shot = nullptr; play = nullptr;
}
static void endMatch(int res) {  // 0 win, 1 lose, 2 tie
  state = S_END;
  auto s = store(); Rec& r = s[st->id]; r.p++; if (res == 0) r.w++; save(s);
  int tot = M->overs * 6, wl = 10 - M->wk, bl = tot - M->balls, by = M->target - 1 - M->score;
  endInfo.ground = std::string(st->name) + " " DOT " " + st->city;
  endInfo.title = res == 0 ? "YOU WIN" : res == 2 ? "MATCH TIED" : "YOU LOSE";
  endInfo.line = res == 0 ? "Won by " + std::to_string(wl) + " wicket" + (wl == 1 ? "" : "s") + " with " + std::to_string(bl) + " ball" + (bl == 1 ? "" : "s") + " to spare."
    : res == 2 ? "Scores level on " + std::to_string(M->score) + ". One more run needed."
    : "Lost by " + std::to_string(by) + " run" + (by == 1 ? "" : "s") + ". Target was " + std::to_string(M->target) + ".";
  endInfo.score = std::to_string(M->score) + "/" + std::to_string(M->wk);
  endInfo.fours = std::to_string(M->fours); endInfo.sixes = std::to_string(M->sixes);
  if (res == 0) { celebrate = 3; sfx::six(); }
  showScreen(SC_END);
}
static void setPaused(bool v) { if (!M || state == S_END || state == S_MENU) return; paused = v; }
static void pickStadium(int i) {
  sIdx = (i + NSTAD) % NSTAD; st = &STADIUMS[sIdx];
  makeCrowd(st->crowd, sIdx * 97 + 11);
  M = nullptr; setupField();
}
static void toMenu() { paused = false; state = S_MENU; M = nullptr; ballOn = false; del = nullptr; setupField(); showScreen(SC_MENU); pickStadium(sIdx); }

/* ---------------- camera + update ---------------- */
// 16:9 layout: HUD panel left, field in the middle, shot controls right
static const int LPW = 152, RPX = 488, FX0 = 154, FX1 = 486;
struct CamT { double x, y, s, k; };
static CamT camTarget() {
  if (M && M->training && state != S_END) return { 0, 0, std::min(cam.visH / 58, cam.visW / 34), 6 };
  double s = std::min(cam.visW / (2 * (st->rx + 7)), cam.visH / (2 * (st->ry + 7)));
  return { 0, 0, s, 6 };
}
static void update(double dt) {
  stateT += dt; celebrate = std::max(0.0, celebrate - dt);
  for (auto& p : parts) { p.vz -= G * dt; p.x += p.vx * dt; p.y += p.vy * dt; p.z = std::max(0.0, p.z + p.vz * dt); if (p.z == 0) { p.vx *= .8; p.vy *= .8; } p.life -= dt; }
  parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Part& p) { return p.life <= 0; }), parts.end());
  if (batS.sw > 0) batS.sw = std::max(0.0, batS.sw - dt);
  if (state == S_RUNUP || state == S_DELIVERY || state == S_INTRO) for (auto& f : fielders) if (f.role == R_F) moveToward(f, f.hx, f.hy, 11 * dt);
  if (state == S_INTRO && stateT > 1.9) newBall();
  else if (state == S_RUNUP) {
    double u = clampd(stateT / RUNUP, 0, 1); bowler->x = .9 - .2 * u; bowler->y = -24 + (24 - 10.6) * u;
    if (u >= 1) release();
  } else if (state == S_DELIVERY) stepDelivery(dt);
  else if (state == S_PLAY) stepPlay(dt);
  else if (state == S_AFTER) {
    if (play && play->phase != PH_DEAD) stepPlay(dt);
    if (keeperBall) { ball.x = keeper->x; ball.y = keeper->y; }
    else if (play && play->o.type == O_CATCH) { ball.x = play->o.f->x; ball.y = play->o.f->y; ball.z = .6; }
    if (stateT > afterDelay * .35) for (auto& f : fielders) if (f.role != R_BOWLER) moveToward(f, f.hx, f.hy, 11 * dt);
    if (stateT > afterDelay) nextOrEnd();
  }
  CamT tg = camTarget(); double a = 1 - std::exp(-dt * tg.k);
  cam.s += (tg.s - cam.s) * a; cam.x += (tg.x - cam.x) * a; cam.y += (tg.y - cam.y) * a;
  if (flashT >= 0) { flashT += dt; if (flashT > 1.5) flashT = -1; }
}

/* ---------------- render: world ---------------- */
static void renderWorld() {
  clipOn = false;
  fillRect(0, 0, W, H, BLACK);
  anchor();
  featBack();
  sePath(30); fillPath(pp((celebrate > 0 && ((int)(gT * 8)) % 2) ? crowdB : crowdA));
  sePath(30); strokePath(1, WHITE); sePath(18); strokePath(1, WHITE);
  for (int k = 0; k < 20; k++) { double a = k / 20.0 * TAU + 0.08; V2 p = sePt(a, 6), q = sePt(a, 30); pline(X(p.x), Y(p.y), X(q.x), Y(q.y), WHITE); }
  if (st->lights) lightsTowers(st->lights);
  featFront();
  sePath(6); fillPath(WHITE);
  sePath(4.2); strokePath(std::max(1.0, 1.4 * cam.s), BLACK);
  // field with mowing stripes
  sePath(0); clipPath();
  for (double yb = -st->ry - 12; yb < st->ry + 12; yb += 7) { int y0 = Y(yb); fillRect(0, y0, W, Y(yb + 7) - y0, pp(((int)std::floor(yb / 7) & 1) ? Pgrass2 : Pgrass)); }
  clipOn = false;
  sePath(0); strokePath(std::max(1.0, 0.45 * cam.s), BLACK);
  // 30-yard circle (dotted)
  int dots = std::max(24, jround(27 * TAU * cam.s / 5));
  for (int i = 0; i < dots; i++) { double a = (double)i / dots * TAU; fillRect(X(std::cos(a) * 27.4), Y(std::sin(a) * 27.4), 1, 1, BLACK); }
  drawPitch();
  // pitch marker
  if (del && M && DIFF[opts.diff].marker && ((state == S_RUNUP && stateT > 0.5) || (state == S_DELIVERY && !del->bounced))) {
    int sx = X(del->px), sy = Y(del->py), r = std::max(2, jround(0.32 * cam.s));
    for (int a = 0; a < 12; a++) { double t = a / 12.0 * TAU; fillRect(sx + jround(std::cos(t) * r), sy + jround(std::sin(t) * r), 1, 1, BLACK); }
    fillRect(sx, sy, 1, 1, BLACK);
  }
  if (M && M->training && del && state != S_MENU && state != S_END) {
    double half = std::max(.5, del->speed * .05); int y0 = Y(YC - half), y1 = Y(YC + half);
    for (double gx : {-2.1, 2.1}) { int sx = X(gx); fillRect(sx, y0, 1, y1 - y0 + 1, BLACK); int d = gx < 0 ? 1 : -2; fillRect(sx + std::min(0, d), y0, 2, 1, BLACK); fillRect(sx + std::min(0, d), y1, 2, 1, BLACK); }
    if (M->guide && state == S_DELIVERY && std::abs(dT - del->tc) < .07) {
      int sx = X(batS.x), sy = Y(batS.y), r = std::max(4, jround(1.5 * cam.s));
      for (int a = 0; a < 24; a++) { double t = a / 24.0 * TAU; fillRect(sx + jround(std::cos(t) * r), sy + jround(std::sin(t) * r), 2, 2, BLACK); }
    }
  }
  // people, sorted by y
  struct Person { double y; int kind; const F* f; };
  std::vector<Person> ppl;
  for (auto& f : fielders) ppl.push_back({ f.y, 0, &f });
  ppl.push_back({ -12.4, 1, nullptr }); ppl.push_back({ 9, 2, nullptr });
  if (M) { ppl.push_back({ batS.y, 3, nullptr }); ppl.push_back({ batN.y, 4, nullptr }); }
  std::stable_sort(ppl.begin(), ppl.end(), [](const Person& a, const Person& b) { return a.y < b.y; });
  for (auto& p : ppl) {
    if (p.kind == 0) drawFielder(*p.f);
    else if (p.kind == 1) drawUmpire(-0.9, -12.4);
    else if (p.kind == 2) drawUmpire(-27, 9);
    else if (p.kind == 3) drawBatter(batS, state != S_PLAY || !play || play->runCap == 0);
    else drawBatter(batN, false);
  }
  if (ballOn && ball.vis) drawBall(ball.x, ball.y, ball.z);
  for (auto& p : parts) { int sy = Y(p.y) - jround(p.z * cam.s * 0.45); fillRect(X(p.x), sy, 2, 1, BLACK); }
}

/* ---------------- text ---------------- */
static int FONT_TOP = 4;  // first row of a capital, found at start-up
static void initFont() { for (int r = 0; r < FONT_ROWS; r++) if (FONT_REG['A' - 32].rows[r]) { FONT_TOP = r; break; } }
static std::string upper(std::string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }
static const Glyph& glyph(unsigned char c, bool bold) { if (c < 32 || c > 126) c = '?'; return (bold ? FONT_BOLD : FONT_REG)[c - 32]; }
static int textW(const std::string& s, int sc, bool bold) {
  int w = 0; for (unsigned char c : s) w += (c == 127 ? 3 : glyph(c, bold).adv) * sc;
  return std::max(0, w - sc);
}
static int lineH(int sc) { return 9 * sc; }
static void drawText(int x, int y, const std::string& s, int sc, bool bold, const Paint& c, int align = 0) {
  if (align == 1) x -= textW(s, sc, bold) / 2; else if (align == 2) x -= textW(s, sc, bold);
  for (unsigned char ch : s) {
    if (ch == 127) { fillRect(x + sc, y + 2 * sc, sc, sc, c); x += 3 * sc; continue; }
    const Glyph& g = glyph(ch, bold);
    for (int r = 0; r < FONT_ROWS; r++) {
      uint16_t bits = g.rows[r]; if (!bits) continue;
      for (int bx = 0; bx < 16; bx++) if (bits >> bx & 1) fillRect(x + bx * sc, y + (r - FONT_TOP) * sc, sc, sc, c);
    }
    x += g.adv * sc;
  }
}
static std::vector<std::string> wrap(const std::string& s, int maxW, int sc, bool bold) {
  std::vector<std::string> lines; std::string cur, word;
  auto flush = [&]() {
    if (word.empty()) return;
    std::string t = cur.empty() ? word : cur + " " + word;
    if (!cur.empty() && textW(t, sc, bold) > maxW) { lines.push_back(cur); cur = word; } else cur = t;
    word.clear();
  };
  for (char c : s) { if (c == ' ') flush(); else word += c; }
  flush(); if (!cur.empty()) lines.push_back(cur);
  return lines;
}
// returns the y below the block
static int drawWrapped(int x, int y, int maxW, const std::string& s, int sc, bool bold, int align = 0, const Paint& c = WHITE) {
  for (auto& l : wrap(s, maxW, sc, bold)) { drawText(align == 1 ? x + maxW / 2 : align == 2 ? x + maxW : x, y, l, sc, bold, c, align); y += lineH(sc); }
  return y;
}
static int fitScale(const std::string& s, int maxW, int maxSc, bool bold) { int sc = maxSc; while (sc > 1 && textW(s, sc, bold) > maxW) sc--; return sc; }
static void frameRect(int x, int y, int w, int h, int t, const Paint& c) {
  fillRect(x, y, w, t, c); fillRect(x, y + h - t, w, t, c); fillRect(x, y, t, h, c); fillRect(x + w - t, y, t, h, c);
}

/* ---------------- buttons (focus + touch) ---------------- */
enum BtnId {
  B_NONE, B_PREV, B_NEXT, B_OV2, B_OV5, B_OV10, B_EASY, B_NORMAL, B_HARD, B_PLAY, B_TRAIN,
  B_AGAIN, B_GROUNDS, B_RESUME, B_QUIT, B_PAUSE, B_TTYPE, B_TSPD, B_TGUIDE,
};
struct Btn { int x, y, w, h, id; };
static std::vector<Btn> btns, lastBtns;
static int focus = B_PLAY;
static bool showFocus = true;
static void button(int x, int y, int w, int h, int id, const std::string& label, bool pressed, int sc = 1, bool bold = false, bool focusable = true) {
  // JS button: black, 2px white border, 3px white drop shadow; aria-pressed inverts
  fillRect(x + 3, y + 3, w, h, WHITE);
  fillRect(x, y, w, h, pressed ? WHITE : BLACK);
  frameRect(x, y, w, h, 2, WHITE);
  const Paint& ink = pressed ? BLACK : WHITE;
  auto lines = wrap(label, w - 8, sc, bold);
  int th = (int)lines.size() * lineH(sc) - 4 * sc, ty = y + (h - th) / 2 + 1;
  for (auto& l : lines) { drawText(x + w / 2, ty, l, sc, bold, ink, 1); ty += lineH(sc); }
  if (focusable && showFocus && focus == id) {
    for (int i = x - 5; i < x + w + 8; i += 2) { put(i, y - 5, WHITE); put(i, y + h + 7, WHITE); }
    for (int j = y - 5; j < y + h + 8; j += 2) { put(x - 5, j, WHITE); put(x + w + 7, j, WHITE); }
  }
  btns.push_back({ x, y, w, h, focusable ? id : -id });
}

/* ---------------- input ---------------- */
enum PadB { P_A, P_B, P_X, P_Y, P_L, P_R, P_ZL, P_ZR, P_PLUS, P_MINUS, P_UP, P_DOWN, P_LEFT, P_RIGHT, P_N };
static bool padDown[P_N], padPrev[P_N];
static double stickX = 0, stickY = 0;
static bool pressed(int b) { return padDown[b] && !padPrev[b]; }
struct Aim { bool on = false; double a = -PI / 2, p = 0; } aim;
static bool pointerAim = false; static SDL_FingerID pointerFinger = 0;
static struct { double a, p; } hist[8]; static int histN = 0;
static double knobDX = 0, knobDY = 0;  // knob offset, -1..1 of the pull radius
// the on-screen stick, right panel
static const int STK_X = 564, STK_Y = 208, STK_R = 54, KNOB_R = 15, PULL_R = STK_R - 17;

static void doAction(int id) {
  switch (id) {
    case B_PREV: pickStadium(sIdx - 1); break;
    case B_NEXT: pickStadium(sIdx + 1); break;
    case B_OV2: opts.overs = 2; break;
    case B_OV5: opts.overs = 5; break;
    case B_OV10: opts.overs = 10; break;
    case B_EASY: opts.diff = EASY; break;
    case B_NORMAL: opts.diff = NORMAL; break;
    case B_HARD: opts.diff = HARD; break;
    case B_PLAY: startMatch(false); break;
    case B_TRAIN: startMatch(true); break;
    case B_AGAIN: startMatch(false); break;
    case B_GROUNDS: toMenu(); focus = B_PLAY; break;
    case B_RESUME: setPaused(false); break;
    case B_QUIT: toMenu(); focus = B_PLAY; break;
    case B_PAUSE: setPaused(true); focus = B_RESUME; break;
    case B_TTYPE: if (M) { M->tSpin = !M->tSpin; setHomes(); } break;
    case B_TSPD: if (M) M->tSpeed = M->tSpeed < .75 ? .85 : M->tSpeed < .9 ? 1 : .7; break;
    case B_TGUIDE: if (M) M->guide = !M->guide; break;
  }
}
static bool menuNav() { return screen == SC_MENU || screen == SC_END || paused; }
static void navigate(int dx, int dy) {
  const Btn* cur = nullptr; for (auto& b : lastBtns) if (b.id == focus) cur = &b;
  if (!cur) { for (auto& b : lastBtns) if (b.id > 0) { focus = b.id; return; } return; }
  double cx = cur->x + cur->w / 2.0, cy = cur->y + cur->h / 2.0, best = 1e18; int bid = focus;
  for (auto& b : lastBtns) {
    if (b.id <= 0 || b.id == focus) continue;
    double bx = b.x + b.w / 2.0 - cx, by = b.y + b.h / 2.0 - cy, along = bx * dx + by * dy, across = std::abs(bx * dy - by * dx);
    if (along <= 2) continue;
    double sc = along + 2.5 * across;
    if (sc < best) { best = sc; bid = b.id; }
  }
  focus = bid;
}
static void stickAimFromPointer(double px, double py) {
  double dx = px - STK_X, dy = py - STK_Y, d = hyp(dx, dy);
  if (d > PULL_R) { dx *= PULL_R / d; dy *= PULL_R / d; }
  aim.p = std::min(1.0, d / PULL_R);
  if (aim.p > .05) aim.a = std::atan2(-dy, -dx);
  knobDX = dx / PULL_R; knobDY = dy / PULL_R;
}
static void pointerDown(double px, double py, SDL_FingerID fid) {
  showFocus = false;
  if (screen == SC_GAME && !paused && M && hyp(px - STK_X, py - STK_Y) <= STK_R + 8) {
    pointerAim = true; pointerFinger = fid; aim.on = true; aim.p = 0; stickAimFromPointer(px, py); return;
  }
  for (auto& b : lastBtns) if (px >= b.x && px < b.x + b.w + 3 && py >= b.y && py < b.y + b.h + 3) { int id = std::abs(b.id); if (b.id > 0) focus = id; doAction(id); return; }
}
static void pointerMove(double px, double py, SDL_FingerID fid) { if (pointerAim && fid == pointerFinger) stickAimFromPointer(px, py); }
static void pointerUp(SDL_FingerID fid, bool fire) {
  if (!pointerAim || fid != pointerFinger) return;
  pointerAim = false;
  if (fire && M && !paused) triggerShot(aim.a, aim.p);
  aim.on = false; aim.p = 0; knobDX = knobDY = 0;
}
// Analog stick version of the pull-and-release joystick: pulling engages
// it, and letting the stick spring back plays the shot with the strongest
// pull of the last few frames (the spring takes a frame or two to return).
static void analogAim() {
  if (pointerAim) return;
  double mag = hyp(stickX, stickY), p = clampd((mag - .12) / .83, 0, 1);
  double a = mag > .05 ? std::atan2(-stickY, -stickX) : aim.a;
  bool live = screen == SC_GAME && !paused && M;
  if (!aim.on) {
    if (live && p > .08) { aim.on = true; histN = 0; }
    else { knobDX = knobDY = 0; aim.p = 0; return; }
  }
  if (histN == 8) { memmove(hist, hist + 1, sizeof(hist[0]) * 7); histN = 7; }
  hist[histN++] = { a, p };
  aim.p = p; aim.a = a;
  knobDX = mag > 1e-3 ? stickX / mag * p : 0; knobDY = mag > 1e-3 ? stickY / mag * p : 0;
  if (p < .04) {
    int bi = 0; for (int i = 1; i < histN; i++) if (hist[i].p > hist[bi].p) bi = i;
    if (live) triggerShot(hist[bi].a, hist[bi].p);
    aim.on = false; aim.p = 0; knobDX = knobDY = 0;
  }
}
static void playButton() {  // A: play the shot at the current pull, or defend
  if (!(screen == SC_GAME && !paused && M)) return;
  if (aim.on) { triggerShot(aim.a, aim.p); aim.on = false; histN = 0; } else triggerShot(-PI / 2, 0);
}
static bool quitRequested = false;
static void handleButtons() {
  int dirs[4][3] = { { P_UP, 0, -1 }, { P_DOWN, 0, 1 }, { P_LEFT, -1, 0 }, { P_RIGHT, 1, 0 } };
  static int stickDir = -1;
  int sd = -1;
  if (std::abs(stickX) > .6 || std::abs(stickY) > .6) sd = std::abs(stickX) > std::abs(stickY) ? (stickX < 0 ? 2 : 3) : (stickY < 0 ? 0 : 1);
  bool stickEdge = sd >= 0 && sd != stickDir; stickDir = sd;
  if (menuNav()) {
    for (auto& d : dirs) if (pressed(d[0])) { showFocus = true; navigate(d[1], d[2]); }
    if (stickEdge && !aim.on) { showFocus = true; navigate(dirs[sd][1], dirs[sd][2]); }
    if (pressed(P_A)) { showFocus = true; doAction(focus); }
    if (screen == SC_MENU) {
      if (pressed(P_L)) pickStadium(sIdx - 1);
      if (pressed(P_R)) pickStadium(sIdx + 1);
      if (pressed(P_PLUS)) quitRequested = true;
    }
    if (paused && (pressed(P_B) || pressed(P_PLUS))) setPaused(false);
    else if (screen == SC_END && pressed(P_B)) { toMenu(); focus = B_PLAY; }
    return;
  }
  if (screen != SC_GAME) return;
  if (pressed(P_PLUS)) { setPaused(true); focus = B_RESUME; return; }
  if (pressed(P_A)) playButton();
  // the JS keyboard shots, on the d-pad
  if (pressed(P_LEFT)) triggerShot(-2.3, .6);
  if (pressed(P_RIGHT)) triggerShot(-.85, .6);
  if (pressed(P_UP)) triggerShot(-PI / 2, .95);
  if (pressed(P_DOWN)) triggerShot(-PI / 2, 0);
  if (M && M->training) {
    if (pressed(P_X)) doAction(B_TTYPE);
    if (pressed(P_Y)) doAction(B_TSPD);
    if (pressed(P_R)) doAction(B_TGUIDE);
  }
}

/* ---------------- render: HUD + sheets ---------------- */
static void drawStick() {
  // checker outer ring = loft zone, solid inside, as the JS stick
  patOX = 0; patOY = 0;
  Paint chk = pp(Pcheck);
  fillCircle(STK_X + .5, STK_Y + .5, STK_R, chk);
  fillCircle(STK_X + .5, STK_Y + .5, STK_R * .79, WHITE);
  fillCircle(STK_X + .5, STK_Y + .5, STK_R * .77, BLACK);
  ellipsePath(STK_X + .5, STK_Y + .5, STK_R + 1, STK_R + 1); strokePath(2.5, WHITE);
  fillRect(STK_X - 1, STK_Y - 1, 3, 3, WHITE);
  double kx = STK_X + .5 + knobDX * PULL_R, ky = STK_Y + .5 + knobDY * PULL_R;
  fillCircle(kx, ky, KNOB_R + 3, WHITE);
  fillCircle(kx, ky, KNOB_R + 1.5, BLACK);
  fillCircle(kx, ky, KNOB_R, aim.p >= LOFT_AT ? chk : WHITE);
}
static void drawMeter(int x, int y, int w) {
  patOX = 0; patOY = 0;
  int h = 10;
  fillRect(x, y, w, h, BLACK); frameRect(x, y, w, h, 2, WHITE);
  int zl = x + jround(meter.zoneL * w), zr = x + jround(meter.zoneR * w);
  fillRect(zl, y + 2, zr - zl, h - 4, pp(Pcheck));
  int pl = x + jround(meter.perfL * w), pr = x + jround(meter.perfR * w);
  fillRect(pl, y + 2, pr - pl, h - 4, WHITE);
  fillRect(x + w / 2 - 1, y - 3, 2, h + 6, WHITE);
  if (!meter.pinHidden) { int px = x + jround(meter.pin * w); fillRect(px - 3, y - 4, 7, h + 8, WHITE); fillRect(px - 1, y - 2, 3, h + 4, BLACK); }
}
static void drawBalls(int x, int y) {
  for (char s : M->thisOver) {
    int bw = 12; std::string t(1, s);
    bool hit = s == '4' || s == '6' || s == 'P', w = s == 'W';
    if (w) frameRect(x - 2, y - 2, bw + 4, bw + 4, 1, WHITE);
    fillRect(x, y, bw, bw, hit || w ? WHITE : BLACK); frameRect(x, y, bw, bw, 1, WHITE);
    drawText(x + bw / 2, y + 4, t, 1, true, hit || w ? BLACK : WHITE, 1);
    x += bw + 5;
  }
}
static void drawFlash() {
  if (flashT < 0) return;
  int k = (int)(flashT / 1.5 * 8);  // steps(8)
  if (k >= 7) return;
  int sc = k == 0 ? 3 : 4, cx = (FX0 + FX1) / 2, cy = jround(H * .38);
  sc = std::min(sc, fitScale(flashBig, FX1 - FX0 - 16, sc, true));
  for (int dx = -2; dx <= 2; dx++) for (int dy = -2; dy <= 2; dy++) drawText(cx + dx, cy + dy, flashBig, sc, true, BLACK, 1);
  drawText(cx + 4, cy + 4, flashBig, sc, true, BLACK, 1);
  drawText(cx, cy, flashBig, sc, true, WHITE, 1);
  if (!flashSub.empty()) {
    std::string s = upper(flashSub); int tw = textW(s, 2, false), y = cy + 5 * sc + 10;
    fillRect(cx - tw / 2 - 6, y - 4, tw + 12, 18, BLACK);
    drawText(cx, y, s, 2, false, WHITE, 1);
  }
}
static void panels() {
  fillRect(0, 0, LPW, H, BLACK); fillRect(LPW, 0, 2, H, WHITE);
  fillRect(RPX, 0, W - RPX, H, BLACK); fillRect(RPX - 2, 0, 2, H, WHITE);
}
static const int LX = 10, LW = LPW - 20, RX = RPX + 10, RW = W - RPX - 20;
static void drawMenu() {
  panels();
  int y = 12;
  drawText(LX, y, "1-BIT", 3, true, WHITE); y += 21;
  drawText(LX, y, "CRICKET", 3, true, WHITE); y += 23;
  y = drawWrapped(LX, y, LW, "10 GROUNDS " DOT " CHASE THE TARGET", 1, false) + 6;
  for (int i = LX; i < LX + LW; i += 2) put(i, y, WHITE);
  y += 8;
  drawText(LX, y, upper("Ground " + std::to_string(sIdx + 1) + " of " + std::to_string(NSTAD)), 1, false, WHITE); y += 12;
  button(LX, y, 18, 22, B_PREV, "<", false, 2, true);
  button(LX + LW - 21, y, 18, 22, B_NEXT, ">", false, 2, true);
  std::string nm = upper(st->name);
  int nsc = fitScale(nm, LW - 50, 2, true), ny = y;
  auto nl = wrap(nm, LW - 50, nsc, true);
  if (nl.size() == 1) ny += (22 - 5 * nsc) / 2;
  for (auto& l : nl) { drawText(LX + LW / 2, ny, l, nsc, true, WHITE, 1); ny += lineH(nsc); }
  y = std::max(y + 30, ny + 4);
  drawText(LX + LW / 2, y, upper(st->city), 1, false, WHITE, 1); y += 14;
  for (int i = LX; i < LX + LW; i += 2) put(i, y, WHITE);
  y += 6;
  char buf[64];
  const char* lab[3] = { "STRAIGHT", "SQUARE", "PITCH" };
  std::string val[3];
  snprintf(buf, sizeof buf, "%d M", (int)st->ry); val[0] = buf;
  snprintf(buf, sizeof buf, "%d M", (int)st->rx); val[1] = buf;
  val[2] = upper(st->pitch);
  for (int i = 0; i < 3; i++) { drawText(LX, y, lab[i], 1, false, WHITE); drawText(LX + LW, y, val[i], 1, true, WHITE, 2); y += 11; }
  for (int i = LX; i < LX + LW; i += 2) put(i, y, WHITE);
  y += 7;
  y = drawWrapped(LX, y, LW, upper(st->note), 1, false) + 4;
  auto s = store(); auto it = s.find(st->id);
  if (it != s.end()) drawWrapped(LX, y, LW, upper("Your record here: won " + std::to_string(it->second.w) + " of " + std::to_string(it->second.p) + "."), 1, true);
  drawText(LX, H - 14, "L/R GROUND", 1, false, WHITE);

  y = 12;
  drawText(RX, y, "OVERS", 1, false, WHITE); y += 11;
  int ovs[3] = { 2, 5, 10 }, oid[3] = { B_OV2, B_OV5, B_OV10 };
  for (int i = 0; i < 3; i++) button(RX + i * 44, y, 38, 20, oid[i], std::to_string(ovs[i]), opts.overs == ovs[i], 1, true);
  y += 34;
  drawText(RX, y, "BOWLING", 1, false, WHITE); y += 11;
  const char* dn[3] = { "EASY", "NORMAL", "HARD" }; int did[3] = { B_EASY, B_NORMAL, B_HARD }, dw[3] = { 34, 50, 34 }, dx = RX;
  for (int i = 0; i < 3; i++) { button(dx, y, dw[i], 20, did[i], dn[i], opts.diff == i, 1, true); dx += dw[i] + 7; }
  y += 36;
  button(RX, y, RW - 3, 42, B_PLAY, upper(std::string("Play at ") + sname(*st)), false, 2, true); y += 54;
  button(RX, y, RW - 3, 24, B_TRAIN, "TRAINING", false, 1, true); y += 38;
  y = drawWrapped(RX, y, RW, "PULL THE STICK AWAY FROM WHERE YOU WANT TO HIT AND LET GO AS THE BALL ARRIVES. PULL FURTHER FOR POWER; THE OUTER RING GOES FOR A LOFT. PULL SHORT BALLS SQUARE ON THE LEG SIDE. A NUDGE DEFENDS.", 1, false);
#ifdef __SWITCH__
  drawText(RX + RW, H - 14, "+ EXIT", 1, false, WHITE, 2);
#endif
}
static void drawGameHud() {
  panels();
  // left: scoreboard
  drawText(LX, 12, std::to_string(M->score) + "/" + std::to_string(M->wk), 3, true, WHITE);
  button(LPW - 34, 9, 22, 20, B_PAUSE, "II", false, 1, true, false);
  int y = 38;
  if (M->training) {
    drawText(LX, y, "TRAINING " DOT " BEST " + std::to_string(M->best), 1, true, WHITE); y += 14;
    drawText(LX, y, "PERFECT " + std::to_string(M->tp) + "/" + std::to_string(M->tn), 2, false, WHITE); y += 20;
    drawText(LX, y, "STREAK " + std::to_string(M->streak), 2, false, WHITE); y += 22;
  } else {
    int tot = M->overs * 6, left = tot - M->balls, need = M->target - M->score;
    drawText(LX, y, std::to_string(M->balls / 6) + "." + std::to_string(M->balls % 6) + " / " + std::to_string(M->overs) + " OV", 1, true, WHITE); y += 14;
    if (need > 0) {
      drawText(LX, y, "NEED " + std::to_string(need), 2, true, WHITE); y += 20;
      drawText(LX, y, "FROM " + std::to_string(left) + " BALL" + (left == 1 ? "" : "S"), 1, false, WHITE); y += 14;
    } else { drawText(LX, y, "TARGET REACHED", 1, true, WHITE); y += 14; }
    drawText(LX, y, upper(std::string(M->over % 2 ? "Spin" : "Pace") + " " DOT " " + sname(*st)), 1, false, WHITE); y += 14;
  }
  y += 4;
  drawBalls(LX + 2, y); y += 26;
  if (M->training) {
    y += 2;
    drawText(LX, y, "X BOWLER", 1, false, WHITE);
    button(LX + 60, y - 5, 66, 18, B_TTYPE, M->tSpin ? "SPIN" : "PACE", false, 1, true, false); y += 26;
    drawText(LX, y, "Y SPEED", 1, false, WHITE);
    button(LX + 60, y - 5, 66, 18, B_TSPD, M->tSpeed < .75 ? "SLOW" : M->tSpeed < .9 ? "MED" : "FAST", false, 1, true, false); y += 26;
    drawText(LX, y, "R GUIDE", 1, false, WHITE);
    button(LX + 60, y - 5, 66, 18, B_TGUIDE, M->guide ? "ON" : "OFF", M->guide, 1, true, false); y += 26;
  }
  int hy = H - 4 * 11 - 6;
  const char* help[4] = { "STICK: PULL + LET GO", "A: PLAY SHOT / DEFEND", "D-PAD: QUICK SHOTS", "+: PAUSE" };
  for (auto h : help) { drawText(LX, hy, h, 1, false, WHITE); hy += 11; }
  // right: hint, timing meter, stick
  y = drawWrapped(RX, 12, RW, upper(hintText), 2, false, 1);
  y = std::max(y, 12 + 3 * lineH(2)) + 6;
  drawMeter(RX, y, RW); y += 16;
  drawText(RX, y, "EARLY", 1, false, WHITE); drawText(RX + RW, y, "LATE", 1, false, WHITE, 2); y += 12;
  drawText(RX + RW / 2, y, upper(meter.text), 1, true, WHITE, 1);
  drawStick();
  int sy = STK_Y + STK_R + 14;
  drawText(RX + RW / 2, sy, "PULL BACK, LET GO", 1, false, WHITE, 1);
  drawText(RX + RW / 2, sy + 11, "OUTER RING: LOFT", 1, true, WHITE, 1);
  drawText(RX + RW / 2, sy + 22, "NUDGE: DEFEND", 1, false, WHITE, 1);
}
static void modalBox(int y, int h) {
  int x = FX0 + 14, w = FX1 - FX0 - 28;
  fillRect(x, y, w, h, BLACK); frameRect(x, y, w, h, 3, WHITE);
}
static void drawPause() {
  int y = 120; modalBox(y, 104);
  drawText((FX0 + FX1) / 2, y + 16, "PAUSED", 4, true, WHITE, 1);
  button(FX0 + 34, y + 58, 140, 28, B_RESUME, "RESUME", false, 2, true);
  button(FX0 + 190, y + 58, 106, 28, B_QUIT, "QUIT", false, 2, true);
}
static void drawEnd() {
  if (M) drawGameHud();
  drawFlash();  // under the sheet, as in the DOM
  int y = 74, x = FX0 + 30, w = FX1 - FX0 - 60; modalBox(y, 210);
  drawText(x, y + 14, upper(endInfo.ground), 1, false, WHITE);
  drawText(x, y + 28, endInfo.title, fitScale(endInfo.title, w, 4, true), true, WHITE);
  int yy = drawWrapped(x, y + 58, w, upper(endInfo.line), 1, false) + 4;
  for (int i = x; i < x + w; i += 2) { put(i, yy, WHITE); put(i, yy + 34, WHITE); }
  const char* lab[3] = { "SCORE", "FOURS", "SIXES" }; std::string val[3] = { endInfo.score, endInfo.fours, endInfo.sixes };
  for (int i = 0; i < 3; i++) { int cx = x + i * w / 3; drawText(cx, yy + 6, lab[i], 1, false, WHITE); drawText(cx, yy + 17, val[i], 2, true, WHITE); }
  button(x, y + 160, 150, 30, B_AGAIN, "PLAY AGAIN", false, 2, true);
  button(x + 164, y + 160, w - 167, 30, B_GROUNDS, "GROUNDS", false, 1, true);
}
static void render() {
  btns.clear();
  renderWorld();
  if (screen == SC_MENU) drawMenu();
  else if (screen == SC_END) drawEnd();
  else { drawGameHud(); drawFlash(); if (paused) drawPause(); }
  lastBtns = btns;
  bool ok = false; for (auto& b : lastBtns) if (b.id == focus) ok = true;
  if (!ok && menuNav()) { int want = screen == SC_END ? B_AGAIN : paused ? B_RESUME : B_PLAY; focus = want; }
}

/* ---------------- platform ---------------- */
static SDL_Window* win; static SDL_Renderer* ren; static SDL_Texture* tex;
#ifdef __SWITCH__
static SDL_Joystick* joy = nullptr;
#else
static SDL_GameController* gc = nullptr;
#endif
static uint32_t pixels[W * H];
static void present() {
  for (int i = 0; i < W * H; i++) pixels[i] = fb[i] ? 0xFFFFFFFFu : 0xFF000000u;
  SDL_UpdateTexture(tex, nullptr, pixels, W * 4);
  SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
  SDL_RenderCopy(ren, tex, nullptr, nullptr); SDL_RenderPresent(ren);
}
static void pollPad() {
  memcpy(padPrev, padDown, sizeof padDown);
  memset(padDown, 0, sizeof padDown);
  stickX = stickY = 0;
#ifdef __SWITCH__
  // libnx button order as exposed by SDL's Switch joystick driver
  static const int map[P_N] = { 0, 1, 2, 3, 6, 7, 8, 9, 10, 11, 13, 15, 12, 14 };
  if (joy) {
    for (int b = 0; b < P_N; b++) padDown[b] = SDL_JoystickGetButton(joy, map[b]);
    stickX = SDL_JoystickGetAxis(joy, 0) / 32767.0; stickY = SDL_JoystickGetAxis(joy, 1) / 32767.0;
  }
#else
  if (gc) {
    static const SDL_GameControllerButton map[P_N] = {
      SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
      SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_INVALID, SDL_CONTROLLER_BUTTON_INVALID,
      SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
      SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT };
    for (int b = 0; b < P_N; b++) if (map[b] != SDL_CONTROLLER_BUTTON_INVALID) padDown[b] = SDL_GameControllerGetButton(gc, map[b]);
    stickX = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0; stickY = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0;
  }
  // keyboard: arrows = d-pad, WASD = stick, Enter/Space = A, Esc = +/B, Q/E = L/R
  const Uint8* k = SDL_GetKeyboardState(nullptr);
  padDown[P_A] |= k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_SPACE];
  padDown[P_B] |= k[SDL_SCANCODE_BACKSPACE];
  padDown[P_PLUS] |= k[SDL_SCANCODE_ESCAPE];
  padDown[P_L] |= k[SDL_SCANCODE_Q]; padDown[P_R] |= k[SDL_SCANCODE_E];
  padDown[P_X] |= k[SDL_SCANCODE_X]; padDown[P_Y] |= k[SDL_SCANCODE_Y];
  padDown[P_UP] |= k[SDL_SCANCODE_UP]; padDown[P_DOWN] |= k[SDL_SCANCODE_DOWN];
  padDown[P_LEFT] |= k[SDL_SCANCODE_LEFT]; padDown[P_RIGHT] |= k[SDL_SCANCODE_RIGHT];
  double kx = (k[SDL_SCANCODE_D] ? 1 : 0) - (k[SDL_SCANCODE_A] ? 1 : 0), ky = (k[SDL_SCANCODE_S] ? 1 : 0) - (k[SDL_SCANCODE_W] ? 1 : 0);
  if (kx || ky) { double m = hyp(kx, ky); stickX = kx / m; stickY = ky / m; }
#endif
  if (std::abs(stickX) < .12 && std::abs(stickY) < .12) { if (hyp(stickX, stickY) < .12) stickX = stickY = 0; }
  for (int b = 0; b < P_N; b++) if (padDown[b]) showFocus = true;
}
static void openPads() {
#ifdef __SWITCH__
  if (!joy && SDL_NumJoysticks() > 0) joy = SDL_JoystickOpen(0);
#else
  for (int i = 0; i < SDL_NumJoysticks() && !gc; i++) if (SDL_IsGameController(i)) gc = SDL_GameControllerOpen(i);
#endif
}
#ifndef __SWITCH__
static void toCanvas(int wx, int wy, double& px, double& py) {
  int ww, wh; SDL_GetWindowSize(win, &ww, &wh);
  double s = std::min((double)ww / W, (double)wh / H), ox = (ww - W * s) / 2, oy = (wh - H * s) / 2;
  px = (wx - ox) / s; py = (wy - oy) / s;
}
#endif

// --dump DIR [--frames N] [--every N] [--demo] [--play|--train|--end|--pause]
// [--ground I]: test run that writes PGM frames, an event log and a summary
// to DIR, playing shots automatically with --demo. On the Switch the same
// arguments are read from sdmc:/switch/OneBitCricket/autotest.txt, so the
// shipped .nro can be tested in an emulator; without that file it never runs.
static bool dumpMode = false, demo = false; static std::string dumpDir; static int dumpEvery = 30, maxFrames = 0;
static void dumpFrame(int n) {
  char name[512]; snprintf(name, sizeof name, "%s/f%05d.pgm", dumpDir.c_str(), n);
  FILE* f = fopen(name, "wb"); if (!f) return;
  fprintf(f, "P5\n%d %d\n255\n", W, H);
  std::vector<uint8_t> row(W * H); for (int i = 0; i < W * H; i++) row[i] = fb[i] ? 255 : 0;
  fwrite(row.data(), 1, row.size(), f); fclose(f);
}
static void demoStep() {
  if (screen == SC_GAME && state == S_DELIVERY && !shot && dT >= del->tc - rand2(-.04, .08)) {
    double a = rand2(-2.9, -.5), p = rand2(.2, 1);
    triggerShot(a, p);
  }
}

int main(int argc, char** argv) {
  bool startTrain = false, startPlay = false, startEnd = false, startPause = false; int ground = 0;
  std::vector<std::string> args(argv + 1, argv + argc);
#ifdef __SWITCH__
  if (FILE* af = fopen("sdmc:/switch/OneBitCricket/autotest.txt", "r")) {
    char tok[512]; while (fscanf(af, "%511s", tok) == 1) args.push_back(tok);
    fclose(af);
  }
#endif
  int argn = (int)args.size();
  for (int i = 0; i < argn; i++) {
    std::string a = args[i];
    if (a == "--dump" && i + 1 < argn) { dumpMode = true; dumpDir = args[++i]; }
    else if (a == "--frames" && i + 1 < argn) maxFrames = atoi(args[++i].c_str());
    else if (a == "--every" && i + 1 < argn) dumpEvery = atoi(args[++i].c_str());
    else if (a == "--ground" && i + 1 < argn) ground = atoi(args[++i].c_str());
    else if (a == "--demo") demo = true;
    else if (a == "--play") startPlay = true;
    else if (a == "--train") startTrain = true;
    else if (a == "--end") startEnd = true;
    else if (a == "--pause") startPause = true;
  }
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) < 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
  win = SDL_CreateWindow("1-Bit Cricket", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_RESIZABLE);
  ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
  if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  SDL_RenderSetLogicalSize(ren, W, H);
  tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
  SDL_AudioSpec want{}, have{};
  want.freq = 48000; want.format = AUDIO_F32SYS; want.channels = 1; want.samples = 1024; want.callback = audioCb;
  adev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (adev) { SR = have.freq; SDL_PauseAudioDevice(adev, 0); }
  openPads();
  if (dumpMode) {
    mkdir(dumpDir.c_str(), 0777);
    tlog = fopen((dumpDir + "/events.log").c_str(), "w");
    SDL_RendererInfo ri{}; if (ren) SDL_GetRendererInfo(ren, &ri);
    if (tlog) fprintf(tlog, "boot renderer=%s audio=%d joysticks=%d\n", ren ? ri.name : "none", adev ? 1 : 0, SDL_NumJoysticks());
  }

  initPatterns(); initFont();
  pickStadium(ground);
  cam.ox = (FX0 + FX1) / 2.0; cam.oy = H / 2.0; cam.visW = FX1 - FX0; cam.visH = H;
  CamT t0 = camTarget(); cam.x = t0.x; cam.y = t0.y; cam.s = t0.s;
  if (startPlay || startEnd || startPause) startMatch(false);
  if (startTrain) startMatch(true);
  if (startPause) setPaused(true);

  Uint64 last = SDL_GetPerformanceCounter(); int frameN = 0;
  bool running = true;
  while (running && !quitRequested) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
#ifndef __SWITCH__
      double px, py;
#endif
      switch (e.type) {
        case SDL_QUIT: running = false; break;
        case SDL_JOYDEVICEADDED: case SDL_CONTROLLERDEVICEADDED: openPads(); break;
        case SDL_WINDOWEVENT: if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST && !dumpMode && M && state != S_END) { setPaused(true); focus = B_RESUME; } break;
        case SDL_FINGERDOWN: pointerDown(e.tfinger.x * W, e.tfinger.y * H, e.tfinger.fingerId); break;
        case SDL_FINGERMOTION: pointerMove(e.tfinger.x * W, e.tfinger.y * H, e.tfinger.fingerId); break;
        case SDL_FINGERUP: pointerUp(e.tfinger.fingerId, true); break;
#ifndef __SWITCH__
        case SDL_MOUSEBUTTONDOWN: if (e.button.which != SDL_TOUCH_MOUSEID) { toCanvas(e.button.x, e.button.y, px, py); pointerDown(px, py, -1); } break;
        case SDL_MOUSEMOTION: if (e.motion.which != SDL_TOUCH_MOUSEID) { toCanvas(e.motion.x, e.motion.y, px, py); pointerMove(px, py, -1); } break;
        case SDL_MOUSEBUTTONUP: if (e.button.which != SDL_TOUCH_MOUSEID) pointerUp(-1, true); break;
#endif
      }
    }
    Uint64 now = SDL_GetPerformanceCounter();
    double dt = std::min(.05, (double)(now - last) / SDL_GetPerformanceFrequency()); last = now;
    if (dumpMode) dt = 1 / 60.0;
    pollPad(); handleButtons(); analogAim();
    if (demo) demoStep();
    if (paused) dt = 0;
    gT += dt;
    update(dt); render();
    if (startEnd && state != S_END && frameN == 10) { M->score = M->target; M->balls = 17; M->fours = 3; M->sixes = 2; endMatch(0); }
    if (dumpMode && frameN % dumpEvery == 0) dumpFrame(frameN);
    present();
    if (maxFrames && ++frameN >= maxFrames) break;
    if (!maxFrames) frameN++;
  }
  if (dumpMode) {
    if (FILE* f = fopen((dumpDir + "/summary.txt").c_str(), "w")) {
      fprintf(f, "frames %d\nscreen %d\nstate %d\n", frameN, (int)screen, (int)state);
      if (M) fprintf(f, "score %d/%d\nballs %d\nfours %d\nsixes %d\ntarget %d\n", M->score, M->wk, M->balls, M->fours, M->sixes, M->target);
      fclose(f);
    }
    if (tlog) fclose(tlog);
  }
  if (adev) SDL_CloseAudioDevice(adev);
  SDL_Quit();
  return 0;
}
