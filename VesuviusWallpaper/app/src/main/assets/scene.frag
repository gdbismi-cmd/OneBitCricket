// Mount Vesuvius at any hour, with hovering spaceships.
// Single full-screen fragment shader (GLSL ES 1.00). All scene state comes in as uniforms;
// the Java side simulates time of day, ship flight paths and lightning.
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

uniform vec2  uRes;
uniform float uTime;        // seconds, wrapped hourly
uniform float uPan;         // -1..1 home-screen scroll
uniform vec4  uSun;         // xy sky position, z = sin(elevation)
uniform vec3  uSunDir;      // light direction for shading
uniform vec4  uMoon;        // xy sky position, z = sin(elevation), w = phase 0..1
uniform float uLava;        // 0..1 lava flow
uniform float uErupt;       // 0..1 ash plume / ejecta
uniform float uSnowFall;    // 0..1
uniform float uSnowCover;   // 0..1
uniform float uClouds;      // 0..1
uniform float uHaze;        // 0..1
uniform float uWind;        // -1..1
uniform float uFlash;       // 0..1 volcanic lightning
uniform float uBoltSeed;
uniform float uStars;       // 0..1
uniform float uCityLights;  // 0..1
uniform float uShipCount;
uniform vec4  uShipA[6];    // x, y, scale, type (0 saucer, 1 cruiser, 2 mothership)
uniform vec4  uShipB[6];    // tilt, beam 0..1, lights 0..1, phase

#define HORIZON 0.0
#define BASE    0.018
#define CONE_X  0.04
#define SOMMA_X (-0.13)
#define PI 3.14159265
#define CONE_H 0.27
#define CONE_K 0.19
#define CONE_TOP 0.196
#define SOMMA_H 0.172

// ---------------------------------------------------------------- noise
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}
float hash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i), b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0)), d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float noise1(float x) {
    float i = floor(x), f = fract(x);
    return mix(hash11(i), hash11(i + 1.0), f * f * (3.0 - 2.0 * f));
}
const mat2 ROT = mat2(0.80, 0.60, -0.60, 0.80);
float fbm(vec2 p, int oct) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 6; i++) {
        if (i >= oct) break;
        s += a * vnoise(p);
        p = ROT * p * 2.03 + 17.1;
        a *= 0.5;
    }
    return s;
}
float fbm1(float x, int oct) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++) {
        if (i >= oct) break;
        s += a * noise1(x);
        x = x * 2.07 + 11.3;
        a *= 0.5;
    }
    return s;
}

// ---------------------------------------------------------------- lighting environment
float dayF()    { return smoothstep(-0.10, 0.22, uSun.z); }
float duskF()   { return exp(-pow((uSun.z - 0.02) / 0.13, 2.0)); }
float nightF()  { return 1.0 - smoothstep(-0.20, 0.02, uSun.z); }
float moonF()   { return smoothstep(-0.05, 0.15, uMoon.z) * nightF() * (0.35 + 0.65 * (1.0 - abs(uMoon.w * 2.0 - 1.0))); }

vec3 sunColor() {
    vec3 low = vec3(1.0, 0.36, 0.10), high = vec3(1.0, 0.94, 0.86);
    return mix(low, high, smoothstep(0.0, 0.40, uSun.z)) * smoothstep(-0.06, 0.04, uSun.z) * 2.3;
}
vec3 moonLight() { return vec3(0.45, 0.55, 0.80) * 0.12 * moonF(); }
vec3 skyAmbient() {
    vec3 day = vec3(0.32, 0.45, 0.70);
    vec3 dusk = vec3(0.45, 0.30, 0.35);
    vec3 night = vec3(0.012, 0.018, 0.035);
    vec3 c = mix(night, dusk, smoothstep(-0.20, 0.0, uSun.z));
    c = mix(c, day, smoothstep(0.0, 0.25, uSun.z));
    return c + moonLight() * 0.5;
}
vec3 lightDir() { return normalize(uSunDir); }
vec3 moonDir() { return normalize(vec3(uMoon.x * 1.2, max(uMoon.z, 0.05), 0.4)); }

// Horizon/haze colour (also used for atmospheric perspective)
vec3 hazeColor() {
    vec3 day = vec3(0.62, 0.72, 0.85);
    vec3 dusk = vec3(0.95, 0.52, 0.32);
    vec3 night = vec3(0.025, 0.03, 0.05) + vec3(0.06, 0.035, 0.015) * uCityLights;
    vec3 c = mix(night, dusk, smoothstep(-0.20, -0.01, uSun.z));
    c = mix(c, day, smoothstep(0.02, 0.30, uSun.z));
    return c + moonLight() * 0.4;
}

// ---------------------------------------------------------------- Vesuvius geometry
float coneH(float x) {
    float r = abs(x - CONE_X);
    float h = CONE_H * exp(-r / CONE_K);
    float cap = CONE_TOP + 0.01 * (fbm1(x * 90.0, 2) - 0.5);
    h = h - max(h - cap, 0.0) * 0.92;                     // truncated summit
    h -= 0.008 * smoothstep(0.040, 0.018, r);             // crater dip
    h += 0.003 * smoothstep(0.052, 0.040, r) * smoothstep(0.018, 0.04, r); // rim lip
    return h;
}
float sommaH(float x) {
    float d = x - SOMMA_X;
    float r = d > 0.0 ? d / 0.055 : max(-d - 0.07, 0.0) / 0.20;
    float h = SOMMA_H * exp(-r);
    h += 0.014 * (fbm1(x * 55.0, 4) - 0.5) * smoothstep(0.03, 0.15, h);
    h += 0.02 * (fbm1(x * 9.0 + 2.0, 3) - 0.5) * smoothstep(0.0, 0.1, -d);
    return h;
}
float apronH(float x) { return 0.04 * exp(-abs(x - CONE_X) / 0.5) + 0.004 * fbm1(x * 25.0, 2); }
float mountainTop(float x) { return BASE + max(max(coneH(x), sommaH(x)), apronH(x)); }

// lava flows: positions in cone azimuth space
float lavaFlowMask(float th, float r, float t, out float heat) {
    float m = 0.0;
    heat = 0.0;
    float reach = mix(0.04, 0.42, uLava);
    for (int k = 0; k < 4; k++) {
        float fk = float(k);
        float th0 = -0.9 + fk * 0.62 + 0.15 * sin(fk * 3.7);
        float wig = 0.20 * (fbm1(r * 9.0 + fk * 13.0, 3) - 0.5) + 0.05 * sin(r * 30.0 + fk);
        float w = 0.025 + r * 0.22 + 0.03 * fbm1(r * 20.0 + fk * 7.0, 2);
        float len = reach * (0.55 + 0.45 * hash11(fk + 3.0));
        float d = abs(th - th0 - wig);
        float body = smoothstep(w, w * 0.35, d) * smoothstep(len, len - 0.05, r);
        // flowing crust cracks
        float cr = fbm(vec2((th - th0) * 26.0, r * 55.0 - t * 0.9 + fk * 5.0), 3);
        float core = smoothstep(w * 0.5, 0.0, d);
        float hot = body * (0.2 + 0.5 * core + 0.9 * smoothstep(0.5, 0.78, cr)) * (1.0 - 0.7 * r / max(len, 0.01));
        m = max(m, body);
        heat = max(heat, hot);
    }
    return m;
}

vec3 lavaColor(float h) {
    // black-body-ish ramp
    vec3 c = mix(vec3(0.25, 0.02, 0.0), vec3(1.0, 0.18, 0.02), smoothstep(0.0, 0.4, h));
    c = mix(c, vec3(1.0, 0.55, 0.10), smoothstep(0.4, 0.75, h));
    c = mix(c, vec3(1.0, 0.92, 0.6), smoothstep(0.75, 1.1, h));
    return c * (1.0 + 5.0 * h * h);
}

// glow cast by the crater / lava onto nearby things (plume, clouds, ships)
vec3 lavaGlowAt(vec2 p) {
    vec2 c = vec2(CONE_X, BASE + CONE_TOP - 0.006);
    float d = length((p - c) * vec2(1.0, 1.4));
    float g = uLava * (0.9 * exp(-d * 9.0) + 0.25 * exp(-d * 3.0));
    g += uErupt * 0.6 * exp(-d * 6.0);
    return vec3(1.0, 0.32, 0.06) * g;
}

vec3 mountainColor(vec2 p, float lod, out vec3 emissive) {
    emissive = vec3(0.0);
    float t = uTime;
    float hc = coneH(p.x), hs = sommaH(p.x);
    float hh = p.y - BASE;
    bool onCone = hh <= hc + 0.0005 && hc > 0.02;
    float cx = onCone ? CONE_X : SOMMA_X;
    float H = onCone ? CONE_H : SOMMA_H;
    float dx = p.x - cx;
    // decay length and plateau of the profile on this side (matches coneH / sommaH)
    float k = onCone ? CONE_K : (dx > 0.0 ? 0.055 : 0.20);
    float plat = (!onCone && dx < 0.0) ? 0.07 : 0.0;
    float hcl = max(hh, 0.002);
    float rad = plat + -k * log(clamp(hcl / H, 0.0005, 1.0));   // radius of the mountain at this height
    float s = clamp(dx / max(rad, 0.001), -0.999, 0.999);
    float th = asin(s);
    float tanA = clamp(hcl / k, 0.05, 2.5) * (onCone ? 1.4 : 0.8);

    // radial erosion gullies
    float gs = onCone ? 16.0 : 24.0;
    vec2 gq = vec2(th * gs + 2.0 * fbm1(rad * 12.0, 2), rad * 9.0);
    float g0 = fbm(gq, 4 - int(lod));
    float g1 = fbm(gq + vec2(0.35, 0.0), 4 - int(lod));
    float gully = (g1 - g0) * 1.6 * smoothstep(0.0, 0.06, rad);
    float tang = gully;
    vec3 n = normalize(vec3(s * tanA + tang * cos(th), 1.0, cos(th) * tanA - tang * s));
    if (!onCone && dx > 0.0) n = normalize(n + vec3(0.0, 0.0, -0.5)); // inner caldera wall faces away

    // albedo: vegetation low, ash/rock high
    float alt = hh / 0.20;
    float vn = fbm(p * vec2(60.0, 90.0), 3 - int(lod));
    vec3 veg = mix(vec3(0.05, 0.075, 0.03), vec3(0.10, 0.12, 0.05), vn);
    vec3 ash = mix(vec3(0.085, 0.075, 0.068), vec3(0.17, 0.15, 0.13), vn);
    ash = mix(ash, vec3(0.30, 0.16, 0.10), smoothstep(0.75, 1.0, alt) * 0.5);  // oxidised summit
    float vegLine = onCone ? 0.42 : 0.80;
    vec3 alb = mix(veg, ash, smoothstep(vegLine - 0.12, vegLine + 0.1, alt + (vn - 0.5) * 0.3 + g0 * 0.15));

    // lava (cone only)
    float heat = 0.0;
    float lavaM = 0.0;
    if (onCone && uLava > 0.01) {
        float rr = rad - 0.025;
        lavaM = lavaFlowMask(th, max(rr, 0.0), t, heat) * uLava;
        heat *= uLava;
        // crater glow
        float cr = smoothstep(0.035, 0.0, abs(p.x - CONE_X)) * smoothstep(-0.008, 0.012, hh - (CONE_TOP - 0.02));
        float flick = 0.75 + 0.25 * noise1(t * 6.0);
        heat = max(heat, cr * uLava * flick * 0.9);
        lavaM = max(lavaM, cr * uLava);
    }

    // snow
    float snowLine = mix(1.25, 0.08, uSnowCover);
    float sn = alt + (g0 - 0.5) * 0.5 + (vn - 0.5) * 0.25;
    float snow = smoothstep(snowLine, snowLine + 0.08, sn) * uSnowCover;
    snow = max(snow, smoothstep(0.85, 1.0, uSnowCover) * 0.5 * smoothstep(0.4, 0.7, vn));
    snow *= 1.0 - smoothstep(0.0, 0.3, lavaM + heat);         // lava melts snow
    alb = mix(alb, vec3(0.86, 0.89, 0.95), snow);
    alb = mix(alb, vec3(0.05, 0.04, 0.04), lavaM * 0.85);    // dark cooled crust

    // lighting
    vec3 L = lightDir();
    float dif = max(dot(n, L), 0.0);
    float wrap = max(dot(n, L) * 0.5 + 0.5, 0.0);
    vec3 col = alb * (sunColor() * (dif * 0.85 + wrap * 0.08));
    col += alb * skyAmbient() * (0.55 + 0.45 * n.y);
    col += alb * moonLight() * max(dot(n, moonDir()), 0.0) * 3.0;
    col += alb * lavaGlowAt(p) * 0.6;
    col += alb * snow * 0.15 * dayF() * pow(max(dot(reflect(-L, n), vec3(0.0, 0.0, 1.0)), 0.0), 8.0) * sunColor();

    emissive += lavaColor(heat) * smoothstep(0.02, 0.2, heat);

    // towns on the lower slopes / coast
    if (hh < 0.06) {
        vec2 g = p * vec2(650.0, 650.0);
        vec2 id = floor(g);
        vec2 f = fract(g) - 0.5 - (hash22(id + 5.0) - 0.5) * 0.6;
        float hr = hash12(id);
        float dens = smoothstep(0.045, 0.0, hh) * smoothstep(0.45, 0.75, fbm(p * vec2(40.0, 60.0), 3));
        float lit = step(1.0 - dens * 0.35, hr);
        float tw = 0.75 + 0.25 * sin(t * (1.0 + hr * 3.0) + hr * 40.0);
        vec3 lc = mix(vec3(1.0, 0.62, 0.28), vec3(1.0, 0.92, 0.78), hash12(id + 7.0));
        float dot_ = smoothstep(0.35, 0.0, length(f));
        emissive += lc * lit * dot_ * tw * 1.2 * nightF() * uCityLights;
        // daytime: pale buildings
        float bld = smoothstep(0.3, 0.9, vnoise(p * vec2(900.0, 1300.0))) * dens;
        col = mix(col, (vec3(0.75, 0.70, 0.62)) * (sunColor() * 0.30 + skyAmbient() * 0.8), bld * 0.25 * (1.0 - snow));
    }
    return col;
}

// ---------------------------------------------------------------- sky
vec3 skyColor(vec2 p, float lod) {
    float h = max(p.y - HORIZON, 0.0);
    float t = pow(clamp(h / 0.75, 0.0, 1.0), 0.55);
    vec2 sd = p - uSun.xy;
    float sunD = length(sd);
    float sunSide = exp(-abs(p.x - uSun.x) * 2.2);

    vec3 dayZ = vec3(0.07, 0.22, 0.58), dayH = vec3(0.55, 0.68, 0.86);
    vec3 duskZ = vec3(0.14, 0.17, 0.38);
    vec3 duskH = mix(vec3(0.55, 0.30, 0.42), vec3(1.15, 0.48, 0.16), sunSide);
    vec3 nightZ = vec3(0.004, 0.007, 0.02), nightH = vec3(0.02, 0.028, 0.05) + vec3(0.07, 0.04, 0.015) * uCityLights;

    vec3 zen = mix(nightZ, duskZ, smoothstep(-0.25, -0.02, uSun.z));
    zen = mix(zen, dayZ, smoothstep(0.0, 0.30, uSun.z));
    vec3 hor = mix(nightH, duskH, smoothstep(-0.22, -0.02, uSun.z));
    hor = mix(hor, dayH, smoothstep(0.04, 0.35, uSun.z));
    // dusk band hugging the horizon
    hor += vec3(1.0, 0.35, 0.08) * duskF() * exp(-h * 9.0) * sunSide * 0.8;
    vec3 col = mix(hor, zen, t);
    col += moonLight() * 0.25 * (1.0 - t);

    // sun glow and disc
    vec3 sc = sunColor();
    col += sc * (0.10 * exp(-sunD * 5.0) + 0.05 * exp(-sunD * 1.5)) * (0.6 + 0.4 * duskF());
    float disc = smoothstep(0.024, 0.020, sunD);
    col += sc * disc * 6.0 * step(HORIZON, p.y);

    // stars + milky way
    float nf = nightF() * uStars;
    if (nf > 0.01) {
        vec2 sp = p + vec2(uTime * 0.0006, 0.0);
        for (int l = 0; l < 2; l++) {
            float sc2 = l == 0 ? 90.0 : 170.0;
            vec2 g = sp * sc2 + float(l) * 31.0;
            vec2 id = floor(g);
            vec2 f = fract(g) - 0.5;
            float r = hash12(id);
            vec2 o = hash22(id) - 0.5;
            float d = length(f - o * 0.7);
            float b = step(l == 0 ? 0.93 : 0.88, r) * smoothstep(0.12, 0.0, d);
            float tw = 0.6 + 0.4 * sin(uTime * (2.0 + r * 5.0) + r * 60.0);
            vec3 tint = mix(vec3(0.75, 0.85, 1.0), vec3(1.0, 0.85, 0.7), hash12(id + 3.0));
            col += tint * b * tw * (l == 0 ? 1.6 : 0.6) * nf * smoothstep(0.02, 0.2, h);
        }
        vec2 mw = ROT * (p - vec2(0.1, 0.35));
        float band = exp(-pow(mw.y / 0.09, 2.0));
        float mwn = fbm(mw * vec2(6.0, 14.0) + 3.0, 4 - int(lod));
        col += vec3(0.11, 0.11, 0.15) * band * smoothstep(0.35, 0.8, mwn) * nf * smoothstep(0.05, 0.3, h) * (1.0 - moonF());
    }

    // moon
    float mf = smoothstep(-0.03, 0.05, uMoon.z) * (1.0 - dayF() * 0.85);
    if (mf > 0.0) {
        vec2 md = p - uMoon.xy;
        float R = 0.026;
        float dl = length(md);
        if (dl < R) {
            vec2 q = md / R;
            vec3 nn = vec3(q, sqrt(max(1.0 - dot(q, q), 0.0)));
            float ph = uMoon.w * 2.0 * PI;
            vec3 ld = vec3(sin(ph), 0.0, -cos(ph));   // waxing: lit on the right
            float lit = smoothstep(-0.05, 0.08, dot(nn, ld));
            float mare = fbm(q * 3.0 + 5.0, 4);
            float tex = 0.75 + 0.35 * smoothstep(0.35, 0.65, mare);
            vec3 mc = vec3(1.0, 0.97, 0.9) * tex * (lit * 1.6 + 0.015);
            col = mix(col, mc + col * 0.3, mf * smoothstep(R, R * 0.92, dl));
        }
        col += vec3(0.6, 0.65, 0.8) * 0.12 * exp(-dl * 22.0) * mf * (1.0 - abs(uMoon.w * 2.0 - 1.0));
    }
    return col;
}

vec4 clouds(vec2 p, float lod) {
    float h = p.y - HORIZON;
    if (h < 0.015 || uClouds < 0.01) return vec4(0.0);
    float z = 1.0 / (h + 0.05);
    vec2 uv = vec2(p.x * z * 1.2, z * 2.0);
    vec2 drift = vec2(uTime * 0.012 * (0.3 + uWind), 0.0);
    int oct = 5 - int(lod) * 2;
    float cov = mix(0.72, 0.30, uClouds);
    float d = fbm(uv * 0.8 + drift, oct);
    d = smoothstep(cov, cov + 0.28, d);
    if (d < 0.003) return vec4(0.0);
    vec2 toSun = normalize(uSun.xy - p + vec2(0.0001)) * 0.25;
    float d2 = smoothstep(cov, cov + 0.28, fbm(uv * 0.8 + drift + toSun * vec2(1.0, -1.0), oct));
    float shade = exp(-d2 * 2.2);
    vec3 lit = sunColor() * (0.30 + 0.5 * shade) + skyAmbient() * 0.9;
    vec3 dark = skyAmbient() * 0.55 + hazeColor() * 0.15;
    vec3 c = mix(dark, lit, shade * 0.85 + 0.15);
    c += moonLight() * 1.5 * shade;
    c += lavaGlowAt(p) * 1.2 * (1.0 - shade * 0.5);
    float fade = smoothstep(0.015, 0.12, h);
    float a = d * fade * 0.95;
    return vec4(c, a);
}

// ---------------------------------------------------------------- ash plume
vec4 plume(vec2 p, float lod) {
    float top = BASE + CONE_TOP - 0.006;
    float dy = p.y - top;
    float e = uErupt;
    if (dy < -0.01) return vec4(0.0);
    float maxH = mix(0.07, 0.75, e);
    if (dy > maxH + 0.1) return vec4(0.0);
    float bend = uWind * 0.55 * pow(max(dy, 0.0), 1.4) + 0.03 * sin(dy * 6.0 + uTime * 0.2);
    float x = p.x - CONE_X - bend;
    float w = mix(0.012, 0.03, e) + dy * mix(0.25, 0.55, e);
    if (abs(x) > w * 1.8 + 0.03) return vec4(0.0);
    float rise = uTime * mix(0.015, 0.05, e);
    float sw = w * 0.8 + 0.02;
    vec2 q = vec2(x / sw, (dy - rise) / sw * 0.9);
    q.y += log(sw) * 1.5;
    int oct = 5 - int(lod) * 2;
    vec2 warp = vec2(fbm(q * 1.3 + 3.1, 3), fbm(q * 1.3 + 7.7, 3));
    vec2 nq = q * 2.6 + warp * 1.5;
    float n = fbm(nq, oct);
    float prof = smoothstep(1.15, 0.2, abs(x) / w + (n - 0.5) * 0.6);
    float vfade = smoothstep(maxH + 0.08, maxH * 0.5, dy) * smoothstep(-0.01, 0.01, dy);
    float dens = smoothstep(0.25, 0.65, n * 0.8 + prof * 0.55 - 0.25) * prof * vfade;
    dens *= mix(0.45, 1.0, e);
    if (dens < 0.002) return vec4(0.0);

    // lighting: self-shadowing toward the sun, lava from below
    float n2 = fbm(nq + normalize(vec2(uSunDir.x, uSunDir.y)) * 0.18, oct);
    float shade = clamp(0.55 + (n2 - n) * 4.5, 0.0, 1.0);
    vec3 ashAlb = mix(vec3(0.55, 0.55, 0.57), vec3(0.24, 0.22, 0.21), e);    // steam -> ash
    vec3 c = ashAlb * (sunColor() * (0.25 + 0.55 * shade) + skyAmbient() * 0.9 + moonLight() * 2.0 * shade);
    c += ashAlb * lavaGlowAt(p) * (0.8 + 1.6 * (1.0 - shade)) * exp(-max(dy, 0.0) * 7.0);
    // lightning inside the plume
    c += vec3(0.65, 0.70, 1.0) * uFlash * 0.9 * exp(-abs(dy - 0.15) * 5.0) * (0.3 + shade);
    return vec4(c, clamp(dens, 0.0, 1.0));
}

// lava bombs
vec3 ejecta(vec2 p) {
    vec2 c = vec2(CONE_X, BASE + CONE_TOP - 0.004);
    vec2 d = p - c;
    if (d.y < -0.12 || d.y > 0.20 || abs(d.x) > 0.22) return vec3(0.0);
    float e = max(uErupt - 0.15, 0.0) * uLava;
    if (e <= 0.0) return vec3(0.0);
    vec3 col = vec3(0.0);
    for (int i = 0; i < 14; i++) {
        float fi = float(i);
        float rate = 0.35 + 0.3 * hash11(fi * 1.7);
        float ph = fract(uTime * rate + hash11(fi * 9.1));
        float cyc = floor(uTime * rate + hash11(fi * 9.1));
        float hs = hash11(fi * 3.3 + cyc * 7.7);
        vec2 v = vec2((hs - 0.5) * 0.28, 0.25 + 0.30 * hash11(fi * 5.5 + cyc));
        float tt = ph * 1.6;
        vec2 pos = v * tt * mix(0.5, 1.0, e) + vec2(0.0, -0.20 * tt * tt);
        vec2 prev = v * (tt - 0.06) * mix(0.5, 1.0, e) + vec2(0.0, -0.20 * (tt - 0.06) * (tt - 0.06));
        vec2 pa = d - prev, ba = pos - prev;
        float hseg = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
        float dist = length(pa - ba * hseg);
        float heat = (1.0 - ph) * step(hs, e * 1.2 + 0.1);
        col += lavaColor(0.4 + 0.6 * heat) * smoothstep(0.0025, 0.0, dist) * heat * (0.3 + 0.7 * hseg);
    }
    // fountain glow above the crater
    col += vec3(1.0, 0.4, 0.08) * e * 0.8 * exp(-length(d * vec2(3.0, 1.5)) * 30.0) * (0.7 + 0.3 * noise1(uTime * 9.0));
    return col;
}

vec3 lightningBolt(vec2 p) {
    if (uFlash < 0.05) return vec3(0.0);
    float top = BASE + CONE_TOP - 0.006;
    float y0 = top + 0.06, y1 = top + 0.06 + 0.10 + 0.12 * hash11(uBoltSeed);
    if (p.y < y0 || p.y > y1) return vec3(0.0);
    float bend = uWind * 0.55 * pow(max(p.y - top, 0.0), 1.4);
    float xb = CONE_X + bend + (hash11(uBoltSeed * 3.1) - 0.5) * 0.06
             + (fbm1(p.y * 40.0 + uBoltSeed * 17.0, 4) - 0.5) * 0.05;
    float d = abs(p.x - xb);
    float ends = smoothstep(y0, y0 + 0.02, p.y) * smoothstep(y1, y1 - 0.03, p.y);
    return vec3(0.8, 0.85, 1.0) * uFlash * ends * (smoothstep(0.0018, 0.0, d) * 6.0 + exp(-d * 150.0) * 0.5);
}

// ---------------------------------------------------------------- spaceships
float sdBox(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}
float sdEllipse(vec2 p, vec2 r) {
    float k0 = length(p / r), k1 = length(p / (r * r));
    return k0 * (k0 - 1.0) / max(k1, 1e-4);
}
float shipSDF(vec2 q, float type) {
    if (type < 0.5) {               // saucer
        float body = sdEllipse(q, vec2(1.0, 0.17));
        float dome = sdEllipse(q - vec2(0.0, 0.10), vec2(0.36, 0.25));
        float under = sdEllipse(q - vec2(0.0, -0.08), vec2(0.45, 0.12));
        return min(min(body, max(dome, -q.y + 0.05)), under);
    } else if (type < 1.5) {        // cruiser
        float x = clamp(q.x, -0.95, 1.0);
        float t = (x + 0.95) / 1.95;                         // 0 tail .. 1 nose
        float top = mix(0.15, 0.0, pow(t, 1.6)) + 0.04 * smoothstep(0.55, 0.25, abs(t - 0.4)) ;
        float bot = mix(-0.10, -0.005, pow(t, 1.2));
        float hull = max(max(q.y - top, bot - q.y), max(q.x - 1.0, -0.95 - q.x));
        float fin = sdBox(q - vec2(-0.70, 0.20), vec2(0.14, 0.08)) ;
        fin = max(fin, (q.x + 0.55) * 0.6 - (q.y - 0.12));
        float pod = sdEllipse(q - vec2(-0.55, -0.12), vec2(0.35, 0.05));
        return min(min(hull, fin), pod);
    } else {                        // mothership
        float hull = sdBox(q, vec2(1.0, 0.07)) - 0.01;
        hull = max(hull, (q.x - 0.65) * 0.5 - (0.07 - abs(q.y)) * 1.2);   // tapered prow
        float deck = sdBox(q - vec2(-0.12, 0.10), vec2(0.62, 0.035));
        float tower = sdBox(q - vec2(-0.48, 0.17), vec2(0.11, 0.045));
        float keel = sdBox(q - vec2(0.05, -0.095), vec2(0.72, 0.03));
        keel = max(keel, (q.x - 0.45) * 0.4 - (0.03 - abs(q.y + 0.095)));
        float ant = sdBox(q - vec2(-0.48, 0.25), vec2(0.004, 0.04));
        return min(min(min(hull, deck), min(tower, keel)), ant);
    }
}

vec4 drawShip(vec2 p, vec4 A, vec4 B, vec3 bg) {
    float sc = A.z;
    vec2 q = (p - A.xy) / sc;
    float ca = cos(B.x), sa = sin(B.x);
    q = mat2(ca, -sa, sa, ca) * q;
    if (abs(q.x) > 2.2 || abs(q.y) > 1.0) return vec4(0.0);
    float type = A.w;
    float mirror = 1.0;
    if (type > 2.5) { type -= 3.0; mirror = -1.0; q.x = -q.x; }   // facing left
    float px = 1.5 / (uRes.y * sc);
    float d = shipSDF(q, type);
    vec3 col = vec3(0.0);
    float alpha = smoothstep(px, -px, d);
    vec3 glow = vec3(0.0);
    float t = uTime + B.w * 10.0;
    float lightsOn = B.z;

    if (alpha > 0.0) {
        vec2 e = vec2(0.004, 0.0);
        vec2 g = vec2(shipSDF(q + e.xy, type) - shipSDF(q - e.xy, type),
                      shipSDF(q + e.yx, type) - shipSDF(q - e.yx, type));
        g = normalize(g + 1e-5);
        float inner = clamp(-d / 0.05, 0.0, 1.0);
        float curv = type < 0.5 ? q.y * 3.5 : (type < 1.5 ? (q.y - 0.02) * 6.0 : (fract(q.y * 7.0 + 0.5) - 0.5) * 1.2);
        vec3 n = normalize(vec3(g * (1.0 - inner), 0.6 + inner * 0.7) + vec3(0.0, curv, 0.0) * inner);
        n.x *= mirror;
        n = vec3(ca * n.x - sa * n.y, sa * n.x + ca * n.y, n.z);

        // hull material with panel lines
        vec2 pg = q * vec2(14.0, 26.0);
        float panel = smoothstep(0.0, 0.06, min(fract(pg.x), fract(pg.y))) ;
        float pv = hash12(floor(pg));
        vec3 alb = vec3(0.22, 0.235, 0.26) * (0.80 + 0.25 * pv) * mix(0.6, 1.0, panel);
        if (type < 0.5 && q.y > 0.07) alb = vec3(0.12, 0.20, 0.26);       // glass dome
        vec3 L = lightDir();
        vec3 V = vec3(0.0, 0.0, 1.0);
        float dif = max(dot(n, L), 0.0);
        float spec = pow(max(dot(reflect(-L, n), V), 0.0), type < 0.5 && q.y > 0.07 ? 80.0 : 30.0);
        vec3 env = mix(hazeColor() * 0.6, skyAmbient() * 1.2, n.y * 0.5 + 0.5);
        col = alb * (sunColor() * dif * 0.9 + env * 0.9);
        col += sunColor() * spec * 0.45 + moonLight() * 3.0 * pow(max(dot(reflect(-moonDir(), n), V), 0.0), 20.0);
        col += env * 0.18 * pow(1.0 - max(n.z, 0.0), 2.0);                   // fresnel rim
        col += alb * lavaGlowAt(p) * 2.5 * max(-n.y + 0.3, 0.0);             // lit from below by the volcano

        // windows
        if (type > 1.5) {
            vec2 wg = q * vec2(30.0, 22.0);
            vec2 wid = floor(wg);
            float on = step(0.55, hash12(wid + floor(t * 0.05)));
            float wmask = step(0.35, fract(wg.x)) * step(0.55, fract(wg.y)) * step(0.02, -d);
            col += vec3(1.0, 0.85, 0.6) * on * wmask * 1.4 * lightsOn * (0.3 + 0.7 * nightF());
        } else if (type > 0.5) {
            float cock = smoothstep(0.02, 0.0, abs(q.y - 0.045 + (q.x - 0.45) * 0.08)) * step(0.30, q.x) * step(q.x, 0.62);
            col = mix(col, vec3(0.3, 0.7, 1.0) * 1.5 * (0.4 + nightF()), cock * 0.8);
        }
    }

    // lights, engines and glow (outside the silhouette too)
    if (type < 0.5) {
        for (int k = 0; k < 8; k++) {
            float a = float(k) * PI * 0.25 + t * 1.6;
            float vis = smoothstep(-0.1, 0.3, cos(a));
            vec2 lp = vec2(sin(a) * 0.93, -0.01);
            float ld = length((q - lp) * vec2(1.0, 1.4));
            vec3 lc = mod(float(k), 2.0) < 1.0 ? vec3(1.0, 0.75, 0.3) : vec3(0.4, 0.9, 1.0);
            glow += lc * vis * (smoothstep(0.045, 0.0, ld) * 3.0 + exp(-ld * 18.0) * 0.5) * lightsOn;
        }
        float pulse = 0.7 + 0.3 * sin(t * 3.0);
        glow += vec3(0.35, 0.85, 1.0) * exp(-length((q - vec2(0.0, -0.18)) * vec2(1.6, 3.0)) * 5.0) * pulse * 1.2;
    } else if (type < 1.5) {
        vec2 ep = q - vec2(-0.98, 0.02);
        float flick = 0.85 + 0.15 * noise1(t * 30.0);
        glow += vec3(0.45, 0.70, 1.0) * (smoothstep(0.07, 0.0, length(ep * vec2(1.0, 1.3))) * 4.0 + exp(-length(ep * vec2(0.8, 2.0)) * 7.0) * 1.0) * flick;
        float strobe = step(0.92, fract(t * 0.9));
        glow += vec3(1.0) * strobe * exp(-length(q - vec2(-0.75, 0.29)) * 40.0) * 4.0 * lightsOn;
        glow += vec3(1.0, 0.1, 0.05) * exp(-length(q - vec2(-0.2, -0.1)) * 50.0) * 2.5 * lightsOn * (0.6 + 0.4 * sin(t * 4.0));
        glow += vec3(0.1, 1.0, 0.3) * exp(-length(q - vec2(0.2, 0.06)) * 50.0) * 2.0 * lightsOn;
    } else {
        for (int k = 0; k < 3; k++) {
            vec2 ep = q - vec2(-1.02, -0.04 + float(k) * 0.045);
            glow += vec3(0.4, 0.65, 1.0) * (smoothstep(0.03, 0.0, length(ep)) * 4.0 + exp(-length(ep * vec2(0.8, 2.5)) * 9.0) * 0.6);
        }
        float strobe = step(0.94, fract(t * 0.6));
        glow += vec3(1.0, 0.15, 0.1) * exp(-length(q - vec2(-0.48, 0.29)) * 45.0) * (2.0 + 4.0 * strobe) * lightsOn;
        glow += vec3(1.0) * step(0.9, fract(t * 0.7 + 0.5)) * exp(-length(q - vec2(0.98, 0.0)) * 50.0) * 4.0 * lightsOn;
    }

    float haze = (1.0 - smoothstep(0.03, 0.22, sc)) * uHaze * 0.45;
    col = mix(col, hazeColor(), haze);
    vec3 outc = mix(bg, col, alpha) + glow * (1.0 - haze);
    return vec4(outc, 1.0);
}

// tractor beams under ships, onto the volcano
vec3 beams(vec2 p) {
    vec3 c = vec3(0.0);
    for (int i = 0; i < 6; i++) {
        if (float(i) >= uShipCount) break;
        vec4 A = uShipA[i], B = uShipB[i];
        if (B.y < 0.01) continue;
        float y0 = A.y - A.z * 0.12;
        float ground = mountainTop(A.x);
        c += vec3(0.4, 0.9, 1.0) * B.y * exp(-length((p - vec2(A.x, ground)) * vec2(1.0, 3.0)) * 45.0) * 0.8;
        if (p.y > y0 || p.y < ground) continue;
        float dd = y0 - p.y;
        float w = A.z * 0.15 + dd * 0.22;
        float dx = abs(p.x - A.x);
        if (dx > w) continue;
        float edge = pow(smoothstep(w, 0.0, dx), 1.5);
        float bands = 0.7 + 0.3 * sin(dd * 140.0 - uTime * 5.0 + float(i)) * (0.6 + 0.4 * vnoise(vec2(p.x * 200.0, dd * 30.0 - uTime * 2.0)));
        float fade = smoothstep(ground, ground + 0.03, p.y) * smoothstep(0.0, 0.02, dd);
        c += vec3(0.30, 0.85, 1.0) * edge * bands * B.y * 0.22 * fade * (0.35 + 0.65 * nightF());
    }
    return c;
}

// ---------------------------------------------------------------- full scene above the waterline
vec3 scene(vec2 p, float lod) {
    vec2 skyP = vec2(p.x - uPan * 0.06, p.y);
    vec3 col = skyColor(skyP, lod);
    vec4 cl = clouds(skyP, lod);
    col = mix(col, cl.rgb, cl.a);

    // distant ranges: Sorrento peninsula (right), Campi Flegrei (left)
    float farTop = HORIZON + 0.004 + 0.05 * smoothstep(0.25, 0.6, p.x) * (0.6 + 0.5 * fbm1(p.x * 6.0, 3)) + 0.012 * fbm1(p.x * 14.0 + 4.0, 2);
    if (p.y < farTop) {
        vec3 fc = mix(skyAmbient() * 0.35, hazeColor(), 0.75);
        col = mix(col, fc, smoothstep(farTop + 0.001, farTop - 0.001, p.y));
    }

    float top = mountainTop(p.x);
    float px = 1.5 / uRes.y;
    float m = smoothstep(top + px, top - px, p.y);
    if (m > 0.0) {
        vec3 em;
        vec3 mc = mountainColor(p, lod, em);
        float fogAmt = clamp(uHaze * 0.38 + 0.04, 0.0, 1.0) * (0.55 + 0.45 * smoothstep(0.15, 0.0, p.y - BASE));
        mc = mix(mc, hazeColor(), fogAmt * (0.75 + 0.25 * dayF()));
        mc += em * (1.0 - fogAmt * 0.4);
        col = mix(col, mc, m);
    }
    // valley mist above the coast
    col = mix(col, hazeColor(), uHaze * 0.35 * exp(-max(p.y - BASE, 0.0) * 30.0));

    vec4 pl = plume(p, lod);
    col = mix(col, pl.rgb, pl.a);
    col += ejecta(p);
    col += lightningBolt(p);
    // soft bloom around the crater and lava
    col += lavaGlowAt(p) * 0.12 * (0.6 + 0.4 * nightF());
    col += beams(p);

    for (int i = 0; i < 6; i++) {
        if (float(i) >= uShipCount) break;
        vec4 s = drawShip(p, uShipA[i], uShipB[i], col);
        if (s.a > 0.0) col = s.rgb;
    }
    return col;
}

vec3 snowLayer(vec2 p, float scale, float speed, float size, float seed) {
    vec2 drift = vec2(uWind * 0.6 * speed * uTime, speed * uTime);
    vec2 g = p * scale + drift + seed;
    vec2 id = floor(g);
    vec2 f = fract(g) - 0.5;
    float r = hash12(id + seed);
    if (r > uSnowFall) return vec3(0.0);
    vec2 o = hash22(id + seed * 2.0) - 0.5;
    o.x += 0.25 * sin(uTime * (0.8 + r) + r * 20.0);
    float d = length(f - o * 0.6);
    float a = smoothstep(size, size * 0.2, d);
    vec3 lc = skyAmbient() * 1.3 + sunColor() * 0.25 + moonLight() * 2.0 + lavaGlowAt(p) * 0.6 + vec3(0.04, 0.03, 0.02) * uCityLights * nightF();
    return lc * a;
}

void main() {
    float S = 1.0 / clamp(uRes.x / uRes.y, 0.45, 0.62) * 0.5;   // keep the volcano framed in portrait
    S = max(S, 0.8);
    vec2 p = (gl_FragCoord.xy - vec2(0.5 * uRes.x, 0.36 * uRes.y)) / uRes.y * S;
    p.x += uPan * 0.22;

    vec3 col;
    if (p.y >= HORIZON) {
        col = scene(p, 0.0);
    } else {
        // the Bay of Naples: mirrored scene with waves, fresnel and glitter
        float depth = HORIZON - p.y;
        float persp = 0.02 / (depth + 0.02);
        vec2 wv = vec2(p.x * 40.0 * persp + uTime * 0.05, 1.0 / (depth + 0.004) * 0.8 + uTime * 0.25);
        float w1 = fbm(wv * vec2(1.0, 0.6) * 3.0, 3);
        float w2 = fbm(wv * vec2(1.0, 0.6) * 6.0 + 9.0, 2);
        float wave = (w1 - 0.5) + (w2 - 0.5) * 0.5;
        float amp = 0.006 + depth * 0.12;
        vec2 rp = vec2(p.x + wave * amp * 0.5, -p.y + wave * amp * 2.5 + 0.0015);
        vec3 refl = scene(rp, 1.0);
        float fres = 0.18 + 0.55 * exp(-depth * 8.0);
        vec3 water = mix(vec3(0.01, 0.05, 0.07), vec3(0.03, 0.12, 0.16), dayF()) * (skyAmbient() * 1.5 + 0.02);
        col = mix(water, refl, fres);
        // sun / moon glitter path
        float gl = pow(max(w1 + w2 * 0.4 - 0.55, 0.0) * 2.2, 6.0);
        float path = exp(-abs(p.x - uSun.x + uPan * 0.06) * (6.0 + 4.0 / (depth + 0.05)) * 0.6);
        col += sunColor() * gl * path * 3.0 * step(0.0, uSun.z);
        float mpath = exp(-abs(p.x - uMoon.x + uPan * 0.06) * (6.0 + 4.0 / (depth + 0.05)) * 0.6);
        col += vec3(0.7, 0.75, 0.9) * gl * mpath * moonF() * 1.5;
        // shimmering city / lava reflections boost
        col += lavaGlowAt(vec2(p.x, -p.y)) * gl * 0.6;
        col = mix(col, hazeColor(), uHaze * 0.25 * exp(-depth * 20.0));
        // shoreline edge
        col = mix(col, hazeColor() * 0.5, smoothstep(0.003, 0.0, depth) * 0.5);
    }

    // global lightning flash
    col += vec3(0.5, 0.55, 0.7) * uFlash * 0.08;

    // falling snow in front of everything
    if (uSnowFall > 0.01) {
        vec2 sp = vec2(p.x + uPan * 0.15, p.y);
        col += snowLayer(sp, 26.0, 0.07, 0.07, 1.0) * 0.5;
        col += snowLayer(sp, 14.0, 0.12, 0.06, 7.0) * 0.7;
        col += snowLayer(sp, 7.0, 0.20, 0.05, 13.0) * 0.8;
    }

    // tone map (ACES approx) + gamma, vignette, grain
    col *= mix(1.15, 0.95, dayF());
    col = (col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14);
    col = pow(clamp(col, 0.0, 1.0), vec3(1.0 / 2.2));
    vec2 uv = gl_FragCoord.xy / uRes;
    col *= 0.88 + 0.12 * pow(16.0 * uv.x * uv.y * (1.0 - uv.x) * (1.0 - uv.y), 0.25);
    col += (hash12(gl_FragCoord.xy + fract(uTime) * 100.0) - 0.5) * 0.012;
    gl_FragColor = vec4(col, 1.0);
}
