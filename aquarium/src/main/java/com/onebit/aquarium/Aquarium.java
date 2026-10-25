package com.onebit.aquarium;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;

import java.util.ArrayList;
import java.util.List;
import java.util.Random;

/**
 * The whole tank: simulation plus Canvas rendering. Coordinates are pixels.
 * Two depth layers (far and near) scroll at different rates with the launcher's
 * page offset; only near-layer fish chase food dropped by taps.
 */
final class Aquarium {
    private static final int CLOWN = 0, TANG = 1, ANGEL = 2, NEON = 3, GOLD = 4, YELLOW = 5;

    private final Random rnd = new Random();
    private final float dp;
    private int w, h;
    private float unit;
    private float time;

    // Parallax: the launcher reports 0..1 across home pages; each layer slides a fraction of the width.
    private float targetOffset = 0.5f, offset = 0.5f;
    private float bgRange, farRange, nearRange;

    private Bitmap background, nearSand;
    private float nearSandTop;

    private final List<Fish> farFish = new ArrayList<>();
    private final List<Fish> nearFish = new ArrayList<>();
    private final List<Plant> farPlants = new ArrayList<>();
    private final List<Plant> nearPlants = new ArrayList<>();
    private final List<Plant> frontPlants = new ArrayList<>();
    private final List<Bubble> bubbles = new ArrayList<>();
    private final List<Food> food = new ArrayList<>();
    private final List<Ray> rays = new ArrayList<>();
    private final List<School> schools = new ArrayList<>();
    private final Jelly jelly = new Jelly();
    private float bubblerX, bubblerY, bubbleTimer;

    private final Species[] species = {
            new Species(CLOWN, 0.55f, 0.45f, 0xFFFF8A2A, 0xFFE85500, 0xFFFF7A1A),
            new Species(TANG, 0.62f, 0.55f, 0xFF3A78FF, 0xFF0B3BCF, 0xFFFFD21E),
            new Species(ANGEL, 0.85f, 0.75f, 0xFFF4F4EC, 0xFFB9C0BC, 0xCCE8E8DC),
            new Species(NEON, 0.32f, 0.35f, 0xFF3A4766, 0xFF1C2438, 0x88AFC8FF),
            new Species(GOLD, 0.6f, 0.75f, 0xFFFFC233, 0xFFFF7F00, 0xAAFFA020),
            new Species(YELLOW, 0.78f, 0.5f, 0xFFFFF04A, 0xFFF2C200, 0xFFFFE11A),
    };

    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint bitmapPaint = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Paint haze = new Paint();
    private final Paint vignette = new Paint();
    private final Paint rayPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint caustic = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private final RectF oval = new RectF();

    Aquarium(float density) {
        dp = density;
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeCap(Paint.Cap.ROUND);
        haze.setColor(0x50062A44);
    }

    boolean hasSize() {
        return w > 0 && h > 0;
    }

    void setOffset(float x) {
        if (!Float.isNaN(x)) targetOffset = Math.max(0f, Math.min(1f, x));
    }

    void release() {
        if (background != null) background.recycle();
        if (nearSand != null) nearSand.recycle();
        background = nearSand = null;
    }

    // ---------------------------------------------------------------- setup

    void setSize(int width, int height) {
        if (width == w && height == h) return;
        w = width;
        h = height;
        unit = Math.min(w, h);
        bgRange = w * 0.06f;
        farRange = w * 0.10f;
        nearRange = w * 0.18f;
        release();
        buildBackground();
        buildNearSand();

        vignette.setShader(new RadialGradient(w / 2f, h * 0.45f, Math.max(w, h) * 0.75f,
                new int[]{0x00000000, 0x00000000, 0x88000814}, new float[]{0f, 0.6f, 1f}, Shader.TileMode.CLAMP));
        rayPaint.setShader(new LinearGradient(0, 0, 0, h * 0.85f, 0x55E8FFFF, 0x00E8FFFF, Shader.TileMode.CLAMP));
        caustic.setColor(0xFFDFFFFF);

        rays.clear();
        for (int i = 0; i < 6; i++) {
            Ray r = new Ray();
            r.x = w * (i + 0.3f + rnd.nextFloat() * 0.4f) / 6f;
            r.top = w * (0.015f + rnd.nextFloat() * 0.03f);
            r.bottom = w * (0.06f + rnd.nextFloat() * 0.08f);
            r.phase = rnd.nextFloat() * 6.28f;
            r.speed = 0.2f + rnd.nextFloat() * 0.35f;
            rays.add(r);
        }

        farPlants.clear();
        nearPlants.clear();
        frontPlants.clear();
        float farSand = h * 0.84f;
        for (int i = 0; i < 12; i++) {
            float x = -farRange / 2 + (w + farRange) * (i + rnd.nextFloat()) / 12f;
            farPlants.add(newPlant(x, farSand + h * 0.02f * rnd.nextFloat(), h * (0.12f + rnd.nextFloat() * 0.18f),
                    unit * (0.025f + rnd.nextFloat() * 0.02f), 0xFF0E4A3C, 0xFF2E8C62));
        }
        float nearX0 = -nearRange / 2, nearSpan = w + nearRange;
        for (int i = 0; i < 9; i++) {
            float x = nearX0 + nearSpan * (i + rnd.nextFloat()) / 9f;
            int kind = rnd.nextInt(3);
            float ph = kind == 0 ? h * (0.3f + rnd.nextFloat() * 0.25f) : h * (0.1f + rnd.nextFloat() * 0.12f);
            float pw = kind == 0 ? unit * (0.045f + rnd.nextFloat() * 0.025f) : unit * (0.025f + rnd.nextFloat() * 0.015f);
            int base = kind == 2 ? 0xFF6A1F2A : 0xFF0F5A32;
            int tip = kind == 2 ? 0xFFE0526A : (kind == 0 ? 0xFF5BC25A : 0xFF8FE36A);
            // Clumps: a few blades sharing a root.
            int blades = kind == 0 ? 1 + rnd.nextInt(2) : 3 + rnd.nextInt(3);
            for (int b = 0; b < blades; b++) {
                nearPlants.add(newPlant(x + (b - blades / 2f) * pw * 0.7f, nearSandTop + h * 0.02f,
                        ph * (0.7f + rnd.nextFloat() * 0.3f), pw, base, tip));
            }
        }
        for (int i = 0; i < 4; i++) {
            // Short foreground grass near the edges so the middle stays open.
            float x = i < 2 ? nearX0 + nearSpan * (0.02f + 0.12f * rnd.nextFloat() + i * 0.08f)
                    : nearX0 + nearSpan * (0.78f + 0.1f * rnd.nextFloat() + (i - 2) * 0.08f);
            for (int b = 0; b < 4; b++) {
                frontPlants.add(newPlant(x + b * unit * 0.018f, nearSandTop + h * 0.03f,
                        h * (0.06f + rnd.nextFloat() * 0.06f), unit * 0.022f, 0xFF0B4A2A, 0xFF7CD65E));
            }
        }

        bubblerX = w * 0.78f;
        bubblerY = nearSandTop + h * 0.012f;
        bubbles.clear();
        food.clear();

        farFish.clear();
        nearFish.clear();
        schools.clear();
        int[] near = {CLOWN, CLOWN, TANG, ANGEL, GOLD, YELLOW, TANG, GOLD};
        for (int s : near) nearFish.add(newFish(species[s], true, null));
        int[] far = {YELLOW, ANGEL, CLOWN, TANG, GOLD, YELLOW};
        for (int s : far) farFish.add(newFish(species[s], false, null));
        School school = new School();
        school.x = w * 0.3f;
        school.y = h * 0.35f;
        schools.add(school);
        pickSchoolTarget(school);
        for (int i = 0; i < 9; i++) nearFish.add(newFish(species[NEON], true, school));

        jelly.size = unit * 0.07f;
        jelly.x = w * 0.25f;
        jelly.y = h * 0.3f;
        jelly.vx = unit * 0.01f;
        jelly.shader = new RadialGradient(0, -jelly.size * 0.3f, jelly.size * 1.2f,
                new int[]{0xCCFFD6F2, 0x88F58AD2, 0x22C060C0}, new float[]{0f, 0.6f, 1f}, Shader.TileMode.CLAMP);
    }

    private void buildBackground() {
        int bw = w + (int) Math.ceil(bgRange);
        background = Bitmap.createBitmap(bw, h, Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(background);
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);

        p.setShader(new LinearGradient(0, 0, 0, h,
                new int[]{0xFF2AA3C4, 0xFF117099, 0xFF0A4670, 0xFF052540},
                new float[]{0f, 0.22f, 0.6f, 1f}, Shader.TileMode.CLAMP));
        c.drawRect(0, 0, bw, h, p);
        // Bright band just under the surface.
        p.setShader(new LinearGradient(0, 0, 0, h * 0.1f, 0x55FFFFFF, 0x00FFFFFF, Shader.TileMode.CLAMP));
        c.drawRect(0, 0, bw, h * 0.1f, p);
        p.setShader(null);

        // Distant rock ridge.
        Path ridge = new Path();
        ridge.moveTo(0, h);
        for (int x = 0; x <= bw; x += Math.max(4, bw / 60)) {
            float y = h * 0.78f - (float) (Math.sin(x * 0.004 + 1) * h * 0.03 + Math.sin(x * 0.011) * h * 0.015
                    + Math.max(0, Math.sin(x * 0.0021 + 2)) * h * 0.06);
            ridge.lineTo(x, y);
        }
        ridge.lineTo(bw, h);
        ridge.close();
        p.setColor(0xFF0A3A58);
        c.drawPath(ridge, p);

        // Far sand bed.
        Path sand = new Path();
        sand.moveTo(0, h);
        for (int x = 0; x <= bw; x += Math.max(4, bw / 80)) {
            sand.lineTo(x, h * 0.84f + (float) (Math.sin(x * 0.006) * h * 0.008 + Math.sin(x * 0.017 + 2) * h * 0.004));
        }
        sand.lineTo(bw, h);
        sand.close();
        p.setShader(new LinearGradient(0, h * 0.82f, 0, h, 0xFF9C9A7C, 0xFF4A4C40, Shader.TileMode.CLAMP));
        c.drawPath(sand, p);
        p.setShader(null);

        // Speckles and a few rocks.
        for (int i = 0; i < 500; i++) {
            float x = rnd.nextFloat() * bw, y = h * (0.85f + rnd.nextFloat() * 0.15f);
            p.setColor(rnd.nextBoolean() ? 0x30FFFFFF : 0x30000000);
            c.drawCircle(x, y, dp * (0.5f + rnd.nextFloat()), p);
        }
        for (int i = 0; i < 5; i++) {
            float x = rnd.nextFloat() * bw, y = h * (0.85f + rnd.nextFloat() * 0.03f);
            float rw = unit * (0.05f + rnd.nextFloat() * 0.07f), rh = rw * (0.45f + rnd.nextFloat() * 0.25f);
            p.setShader(new LinearGradient(0, y - rh, 0, y + rh * 0.4f, 0xFF5C6E74, 0xFF27323A, Shader.TileMode.CLAMP));
            c.drawOval(x - rw, y - rh, x + rw, y + rh * 0.4f, p);
        }
        p.setShader(null);
    }

    private void buildNearSand() {
        int bw = w + (int) Math.ceil(nearRange);
        nearSandTop = h * 0.915f;
        int top = (int) (nearSandTop - h * 0.02f);
        nearSand = Bitmap.createBitmap(bw, h - top, Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(nearSand);
        c.translate(0, -top);
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        Path sand = new Path();
        sand.moveTo(0, h);
        for (int x = 0; x <= bw; x += Math.max(4, bw / 80)) sand.lineTo(x, nearSandY(x - nearRange / 2));
        sand.lineTo(bw, h);
        sand.close();
        p.setShader(new LinearGradient(0, top, 0, h, 0xFFE0C893, 0xFF7A6640, Shader.TileMode.CLAMP));
        c.drawPath(sand, p);
        p.setShader(null);
        c.clipPath(sand);
        for (int i = 0; i < 900; i++) {
            p.setColor(rnd.nextInt(3) == 0 ? 0x40FFFFFF : 0x35402A10);
            c.drawCircle(rnd.nextFloat() * bw, top + rnd.nextFloat() * (h - top), dp * (0.6f + rnd.nextFloat() * 1.2f), p);
        }
        int[] pebbles = {0xFF8C8478, 0xFF6E6A66, 0xFFB0A28A, 0xFF5A5450};
        for (int i = 0; i < 40; i++) {
            float x = rnd.nextFloat() * bw, y = nearSandTop + rnd.nextFloat() * (h - nearSandTop);
            float r = dp * (2 + rnd.nextFloat() * 4);
            p.setColor(pebbles[rnd.nextInt(pebbles.length)]);
            c.drawOval(x - r, y - r * 0.6f, x + r, y + r * 0.6f, p);
            p.setColor(0x33FFFFFF);
            c.drawOval(x - r * 0.6f, y - r * 0.5f, x + r * 0.2f, y - r * 0.1f, p);
        }
    }

    /** Top of the foreground sand at near-layer x. */
    private float nearSandY(float x) {
        return nearSandTop + (float) (Math.sin(x * 2 * Math.PI / w * 1.3) * h * 0.007
                + Math.sin(x * 2 * Math.PI / w * 3.7 + 1) * h * 0.003);
    }

    private Plant newPlant(float x, float baseY, float height, float width, int base, int tip) {
        Plant p = new Plant();
        p.x = x;
        p.baseY = baseY;
        p.height = height;
        p.width = width;
        p.phase = rnd.nextFloat() * 6.28f;
        p.speed = 0.6f + rnd.nextFloat() * 0.6f;
        p.amp = 0.25f + rnd.nextFloat() * 0.25f;
        p.shader = new LinearGradient(0, baseY, 0, baseY - height, base, tip, Shader.TileMode.CLAMP);
        return p;
    }

    private Fish newFish(Species sp, boolean near, School school) {
        Fish f = new Fish();
        f.sp = sp;
        f.near = near;
        f.school = school;
        float base;
        switch (sp.kind) {
            case CLOWN: base = 0.05f; break;
            case TANG: base = 0.07f; break;
            case ANGEL: base = 0.065f; break;
            case NEON: base = 0.022f; break;
            case GOLD: base = 0.055f; break;
            default: base = 0.06f; break;
        }
        f.size = unit * base * (0.85f + rnd.nextFloat() * 0.3f) * (near ? 1f : 0.55f);
        f.speed = f.size * (sp.kind == NEON ? 5f : 1.1f + rnd.nextFloat() * 0.5f) * (near ? 1f : 0.7f);
        f.x = rnd.nextFloat() * w;
        f.y = h * (0.15f + rnd.nextFloat() * 0.6f);
        f.turn = rnd.nextBoolean() ? 1 : -1;
        f.phase = rnd.nextFloat() * 6.28f;
        if (school != null) {
            f.ox = (rnd.nextFloat() - 0.5f) * unit * 0.22f;
            f.oy = (rnd.nextFloat() - 0.5f) * unit * 0.12f;
            f.x = school.x + f.ox;
            f.y = school.y + f.oy;
        }
        pickTarget(f);
        return f;
    }

    private float minY(Fish f) {
        return h * 0.1f + f.size;
    }

    private float maxY(Fish f) {
        return (f.near ? nearSandTop : h * 0.83f) - f.size * 1.2f;
    }

    private void pickTarget(Fish f) {
        if (f.school != null) {
            f.tx = f.school.x + f.ox;
            f.ty = f.school.y + f.oy;
            f.retarget = 0.3f + rnd.nextFloat() * 0.4f;
            return;
        }
        float margin = f.size * 1.5f;
        // Mostly horizontal cruising: long legs across the tank, gentle depth changes.
        f.tx = margin + rnd.nextFloat() * (w - 2 * margin);
        f.ty = clamp(f.y + (rnd.nextFloat() - 0.5f) * h * 0.3f, minY(f), maxY(f));
        f.retarget = 4f + rnd.nextFloat() * 8f;
    }

    private void pickSchoolTarget(School s) {
        float m = unit * 0.15f;
        s.tx = m + rnd.nextFloat() * (w - 2 * m);
        s.ty = h * (0.2f + rnd.nextFloat() * 0.5f);
        s.timer = 5f + rnd.nextFloat() * 6f;
    }

    // ---------------------------------------------------------------- input

    void tap(float sx, float sy) {
        if (!hasSize()) return;
        float x = sx - layerShift(nearRange);
        int n = 3 + rnd.nextInt(3);
        for (int i = 0; i < n && food.size() < 40; i++) {
            Food p = new Food();
            p.x = x + (rnd.nextFloat() - 0.5f) * unit * 0.06f;
            p.y = Math.min(sy, nearSandTop - unit * 0.05f) + (rnd.nextFloat() - 0.5f) * unit * 0.03f;
            p.vy = h * (0.035f + rnd.nextFloat() * 0.02f);
            p.phase = rnd.nextFloat() * 6.28f;
            p.life = 12f;
            p.color = rnd.nextBoolean() ? 0xFFB5651D : 0xFF8E4A1A;
            food.add(p);
        }
        for (int i = 0; i < 4; i++) spawnBubble(x + (rnd.nextFloat() - 0.5f) * unit * 0.04f, sy, dp * (1.5f + rnd.nextFloat() * 2.5f));
    }

    private float layerShift(float range) {
        return (0.5f - offset) * range;
    }

    // ---------------------------------------------------------------- simulation

    void update(float dt) {
        if (!hasSize()) return;
        time += dt;
        offset += (targetOffset - offset) * Math.min(1f, dt * 8f);

        for (School s : schools) {
            s.timer -= dt;
            float dx = s.tx - s.x, dy = s.ty - s.y, d = (float) Math.hypot(dx, dy);
            if (s.timer <= 0 || d < unit * 0.05f) pickSchoolTarget(s);
            float v = unit * 0.06f;
            if (d > 1) {
                s.x += dx / d * v * dt;
                s.y += dy / d * v * dt;
            }
        }
        for (Fish f : farFish) updateFish(f, dt);
        for (Fish f : nearFish) updateFish(f, dt);

        updateJelly(dt);

        // Bubbler: steady stream with occasional bursts.
        bubbleTimer -= dt;
        if (bubbleTimer <= 0) {
            boolean burst = rnd.nextInt(12) == 0;
            int n = burst ? 6 + rnd.nextInt(6) : 1;
            for (int i = 0; i < n; i++) {
                spawnBubble(bubblerX + (rnd.nextFloat() - 0.5f) * dp * 6, bubblerY - dp * 4 + i * dp * 6,
                        dp * (1.5f + rnd.nextFloat() * (burst ? 5f : 3.5f)));
            }
            bubbleTimer = 0.08f + rnd.nextFloat() * 0.25f;
        }
        for (int i = bubbles.size() - 1; i >= 0; i--) {
            Bubble b = bubbles.get(i);
            b.vy = Math.min(b.vy + h * 0.15f * dt, h * 0.12f + b.r * 6f);
            b.y -= b.vy * dt;
            b.phase += dt * 3f;
            b.x += (float) Math.sin(b.phase) * b.r * 1.2f * dt * 3f;
            if (b.y < h * 0.02f) bubbles.remove(i);
        }

        for (int i = food.size() - 1; i >= 0; i--) {
            Food p = food.get(i);
            if (p.eaten) {
                food.remove(i);
                continue;
            }
            float floor = nearSandY(p.x) + dp;
            if (p.y < floor) {
                p.phase += dt * 2f;
                p.y = Math.min(floor, p.y + p.vy * dt);
                p.x += (float) Math.sin(p.phase) * unit * 0.02f * dt;
            } else {
                p.life -= dt;
                if (p.life <= 0) food.remove(i);
            }
        }
    }

    private void updateFish(Fish f, float dt) {
        Food target = f.near ? nearestFood(f) : null;
        float gx, gy, maxSpeed = f.speed;
        if (target != null) {
            // Line the mouth up with the pellet rather than the body centre.
            gx = target.x + (target.x > f.x ? -f.size * 0.8f : f.size * 0.8f);
            gy = target.y;
            maxSpeed *= f.school != null ? 1.3f : 2.2f;
            if (Math.hypot(target.x - f.x, target.y - f.y) < f.size * 1.05f) {
                target.eaten = true;
                if (rnd.nextInt(3) == 0) spawnBubble(f.x + f.size * Math.signum(f.turn), f.y, dp * 1.5f);
            }
        } else {
            f.retarget -= dt;
            if (f.retarget <= 0 || Math.hypot(f.tx - f.x, f.ty - f.y) < f.size) pickTarget(f);
            gx = f.tx;
            gy = f.ty;
        }
        float dx = gx - f.x, dy = gy - f.y, d = (float) Math.hypot(dx, dy);
        float want = maxSpeed * Math.min(1f, d / (f.size * 3f));
        float dvx = d > 0.01f ? dx / d * want : 0, dvy = d > 0.01f ? dy / d * want * 0.6f : 0;
        float k = Math.min(1f, dt * (target != null ? 2.5f : 1.2f));
        f.vx += (dvx - f.vx) * k;
        f.vy += (dvy - f.vy) * k;
        f.x += f.vx * dt;
        f.y += f.vy * dt;
        if (target == null) f.y = clamp(f.y, minY(f), maxY(f));

        if (Math.abs(f.vx) > f.size * 0.05f) {
            float face = Math.signum(f.vx);
            f.turn += (face - f.turn) * Math.min(1f, dt * 3.5f);
        }
        float v = (float) Math.hypot(f.vx, f.vy);
        f.phase += dt * (3f + 9f * Math.min(2f, v / f.speed));
        if (rnd.nextFloat() < dt * 0.03f) spawnBubble(f.x + f.size * Math.signum(f.turn), f.y, dp * (1 + rnd.nextFloat() * 1.5f));
    }

    private Food nearestFood(Fish f) {
        Food best = null;
        float bestD = unit * (f.school != null ? 0.35f : 0.7f);
        for (Food p : food) {
            if (p.eaten) continue;
            float d = (float) Math.hypot(p.x - f.x, p.y - f.y);
            if (d < bestD) {
                bestD = d;
                best = p;
            }
        }
        return best;
    }

    private void updateJelly(float dt) {
        Jelly j = jelly;
        j.phase += dt * 3.6f;
        float contract = (float) Math.max(0, Math.sin(j.phase));
        // Pulse harder when low in the tank, barely at all near the surface: keeps it bobbing mid-water.
        float urge = clamp((j.y - h * 0.15f) / (h * 0.45f), 0.1f, 1.4f);
        float sink = unit * 0.03f * (1f + 0.6f * (float) Math.sin(time * 0.13f));
        j.vy += (-contract * contract * unit * 0.12f * urge + sink) * dt;
        j.vy *= (float) Math.pow(0.4, dt);
        j.y += j.vy * dt;
        j.x += j.vx * dt;
        if (j.x < j.size * 2 && j.vx < 0 || j.x > w - j.size * 2 && j.vx > 0) j.vx = -j.vx;
        j.y = clamp(j.y, h * 0.08f, h * 0.7f);
    }

    private void spawnBubble(float x, float y, float r) {
        if (bubbles.size() > 140) return;
        Bubble b = new Bubble();
        b.x = x;
        b.y = y;
        b.r = r;
        b.vy = h * 0.03f;
        b.phase = rnd.nextFloat() * 6.28f;
        bubbles.add(b);
    }

    // ---------------------------------------------------------------- rendering

    void draw(Canvas c) {
        c.drawBitmap(background, -offset * bgRange, 0, bitmapPaint);
        drawRays(c);

        c.save();
        c.translate(layerShift(farRange), 0);
        for (Plant p : farPlants) drawPlant(c, p);
        for (Fish f : farFish) drawFish(c, f);
        c.restore();

        c.drawRect(0, 0, w, h, haze);
        drawJelly(c);

        c.save();
        c.translate(layerShift(nearRange), 0);
        for (Plant p : nearPlants) drawPlant(c, p);
        drawBubbles(c);
        for (Fish f : nearFish) drawFish(c, f);
        for (Plant p : frontPlants) drawPlant(c, p);
        c.drawBitmap(nearSand, -nearRange / 2, nearSandTop - h * 0.02f - 1, bitmapPaint);
        drawCaustics(c);
        drawBubbler(c);
        drawFood(c);
        c.restore();

        c.drawRect(0, 0, w, h, vignette);
    }

    private void drawRays(Canvas c) {
        float shift = layerShift(bgRange) * 0.5f;
        for (Ray r : rays) {
            float a = 0.5f + 0.5f * (float) Math.sin(time * r.speed + r.phase);
            float sway = (float) Math.sin(time * r.speed * 0.7f + r.phase) * w * 0.03f;
            float x = r.x + shift;
            float skew = w * 0.12f;
            path.reset();
            path.moveTo(x - r.top, -dp);
            path.lineTo(x + r.top, -dp);
            path.lineTo(x + skew + sway + r.bottom, h * 0.85f);
            path.lineTo(x + skew + sway - r.bottom, h * 0.85f);
            path.close();
            rayPaint.setAlpha((int) (40 + 160 * a));
            c.drawPath(path, rayPaint);
        }
    }

    private void drawCaustics(Canvas c) {
        // Shimmering light patches dancing over the foreground sand.
        float x0 = -nearRange / 2, span = w + nearRange;
        for (int i = 0; i < 14; i++) {
            float fx = x0 + span * ((i * 0.618f + (float) Math.sin(time * 0.15f + i) * 0.03f) % 1f);
            float fy = nearSandTop + h * 0.015f + (i % 4) * h * 0.016f;
            float a = 0.5f + 0.5f * (float) Math.sin(time * (0.8f + i * 0.07f) + i * 1.7f);
            float rw = unit * (0.03f + 0.02f * a), rh = rw * 0.3f;
            caustic.setAlpha((int) (10 + 35 * a));
            c.drawOval(fx - rw, fy - rh, fx + rw, fy + rh, caustic);
        }
    }

    private void drawPlant(Canvas c, Plant p) {
        int n = Plant.SEGMENTS;
        float seg = p.height / n;
        float px = p.x, py = p.baseY, ang = 0;
        for (int i = 0; i <= n; i++) {
            float t = i / (float) n;
            p.cx[i] = px;
            p.cy[i] = py;
            p.ca[i] = ang;
            ang = (float) Math.sin(time * p.speed + p.phase - i * 0.35f) * p.amp * t
                    + (float) Math.sin(time * 0.3f + p.phase) * 0.08f;
            px += (float) Math.sin(ang) * seg;
            py -= (float) Math.cos(ang) * seg;
        }
        path.reset();
        for (int i = 0; i <= n; i++) {
            float half = halfWidth(p, i);
            float lx = p.cx[i] - (float) Math.cos(p.ca[i]) * half, ly = p.cy[i] - (float) Math.sin(p.ca[i]) * half;
            if (i == 0) path.moveTo(lx, ly); else path.lineTo(lx, ly);
        }
        for (int i = n; i >= 0; i--) {
            float half = halfWidth(p, i);
            path.lineTo(p.cx[i] + (float) Math.cos(p.ca[i]) * half, p.cy[i] + (float) Math.sin(p.ca[i]) * half);
        }
        path.close();
        fill.setColor(0xFF000000); // shader fills still take the paint's alpha
        fill.setShader(p.shader);
        c.drawPath(path, fill);
        fill.setShader(null);
    }

    private static float halfWidth(Plant p, int i) {
        float t = i / (float) Plant.SEGMENTS;
        // Leaf-like: widest a third of the way up, tapering to a point.
        return p.width * 0.5f * (float) Math.sin(Math.PI * (0.15 + 0.85 * t)) * (1.1f - t * 0.6f);
    }

    private void drawFish(Canvas c, Fish f) {
        Species sp = f.sp;
        float hh = sp.bodyH;
        float dir = f.turn >= 0 ? 1 : -1;
        float sx = Math.abs(f.turn) < 0.12f ? 0.12f * dir : f.turn;
        float tilt = (float) Math.toDegrees(Math.atan2(f.vy, Math.abs(f.vx) + f.size * 0.2f));
        tilt = clamp(tilt, -25, 25) * dir;
        float wag = (float) Math.sin(f.phase);

        c.save();
        c.translate(f.x, f.y);
        c.rotate(tilt);
        c.scale(sx * f.size, f.size);

        // Tail and fins sit behind the body.
        fill.setColor(sp.fin);
        c.save();
        c.rotate(wag * (sp.kind == GOLD ? 24 : 16), -0.7f, 0);
        c.drawPath(sp.tail, fill);
        c.restore();
        c.drawPath(sp.dorsal, fill);
        c.drawPath(sp.ventral, fill);

        fill.setColor(0xFF000000); // shader fills still take the paint's alpha
        fill.setShader(sp.shader);
        c.drawPath(sp.body, fill);
        fill.setShader(null);

        if (sp.kind == CLOWN || sp.kind == ANGEL || sp.kind == NEON || sp.kind == TANG) {
            c.save();
            c.clipPath(sp.body);
            drawPattern(c, sp);
            c.restore();
        }

        // Pectoral fin paddles a little out of phase with the tail.
        c.save();
        c.rotate((float) Math.sin(f.phase * 1.4f + 1) * 25, 0.25f, hh * 0.15f);
        fill.setColor(sp.fin);
        fill.setAlpha(170);
        oval.set(-0.1f, hh * 0.05f, 0.3f, hh * 0.4f);
        c.drawOval(oval, fill);
        fill.setAlpha(255);
        c.restore();

        // Eye.
        float ex = 0.62f, ey = -hh * 0.18f;
        fill.setColor(0xFFFFFFFF);
        c.drawCircle(ex, ey, 0.11f, fill);
        fill.setColor(0xFF101418);
        c.drawCircle(ex + 0.02f, ey, 0.07f, fill);
        fill.setColor(0xCCFFFFFF);
        c.drawCircle(ex + 0.045f, ey - 0.03f, 0.022f, fill);

        c.restore();
    }

    private void drawPattern(Canvas c, Species sp) {
        float hh = sp.bodyH;
        switch (sp.kind) {
            case CLOWN: {
                float[] xs = {0.42f, -0.08f, -0.6f};
                float[] ws = {0.2f, 0.22f, 0.12f};
                for (int i = 0; i < 3; i++) {
                    fill.setColor(0xFF1A1A1A);
                    c.drawRect(xs[i] - ws[i] / 2 - 0.05f, -hh * 1.2f, xs[i] + ws[i] / 2 + 0.05f, hh * 1.2f, fill);
                    fill.setColor(0xFFFFFFFF);
                    c.drawRect(xs[i] - ws[i] / 2, -hh * 1.2f, xs[i] + ws[i] / 2, hh * 1.2f, fill);
                }
                break;
            }
            case TANG:
                fill.setColor(0xFF0A1530);
                c.drawPath(sp.pattern, fill);
                break;
            case ANGEL: {
                fill.setColor(0xFF1C1C1C);
                float[] xs = {0.42f, 0.0f, -0.42f};
                for (float x : xs) c.drawRect(x - 0.06f, -hh * 1.2f, x + 0.06f, hh * 1.2f, fill);
                break;
            }
            case NEON:
                fill.setColor(0xFFE0203A);
                c.drawRect(-1f, hh * 0.05f, 0.25f, hh * 1.2f, fill);
                fill.setColor(0xFF3FF2FF);
                c.drawRect(-0.8f, -hh * 0.35f, 0.9f, hh * 0.05f, fill);
                break;
        }
    }

    private void drawBubbles(Canvas c) {
        for (Bubble b : bubbles) {
            fill.setColor(0x28FFFFFF);
            c.drawCircle(b.x, b.y, b.r, fill);
            stroke.setStrokeWidth(Math.max(1f, b.r * 0.18f));
            stroke.setColor(0xB0E8FFFF);
            c.drawCircle(b.x, b.y, b.r, stroke);
            fill.setColor(0xDDFFFFFF);
            c.drawCircle(b.x - b.r * 0.35f, b.y - b.r * 0.35f, b.r * 0.25f, fill);
        }
    }

    private void drawFood(Canvas c) {
        float r = dp * 2.6f;
        for (Food p : food) {
            fill.setColor(p.color);
            fill.setAlpha((int) (255 * Math.min(1f, p.life / 2f)));
            c.drawCircle(p.x, p.y, r, fill);
        }
        fill.setAlpha(255);
    }

    private void drawBubbler(Canvas c) {
        float rw = unit * 0.05f, rh = unit * 0.022f;
        paint.setShader(null);
        paint.setColor(0xFF3A4048);
        c.drawOval(bubblerX - rw, bubblerY - rh, bubblerX + rw, bubblerY + rh, paint);
        paint.setColor(0xFF5E6872);
        c.drawOval(bubblerX - rw * 0.8f, bubblerY - rh, bubblerX + rw * 0.6f, bubblerY - rh * 0.1f, paint);
        paint.setColor(0x33FFFFFF);
        c.drawOval(bubblerX - rw * 0.5f, bubblerY - rh * 0.85f, bubblerX + rw * 0.1f, bubblerY - rh * 0.45f, paint);
    }

    private void drawJelly(Canvas c) {
        Jelly j = jelly;
        float contract = (float) Math.max(0, Math.sin(j.phase));
        float bw = j.size * (1f - 0.18f * contract), bh = j.size * (0.75f + 0.2f * contract);
        c.save();
        c.translate(j.x + layerShift((farRange + nearRange) / 2), j.y);

        stroke.setColor(0x66F7B8E6);
        stroke.setStrokeWidth(dp * 1.2f);
        for (int i = 0; i < 7; i++) {
            float tx = -bw * 0.85f + i * bw * 1.7f / 6f;
            path.reset();
            path.moveTo(tx, 0);
            float len = j.size * (2.2f + 0.3f * (i % 3));
            for (int k = 1; k <= 8; k++) {
                float t = k / 8f;
                path.lineTo(tx + (float) Math.sin(time * 2f + i + t * 5f) * j.size * 0.18f * t, len * t);
            }
            c.drawPath(path, stroke);
        }
        stroke.setColor(0x99F28CD4);
        stroke.setStrokeWidth(dp * 3f);
        for (int i = -1; i <= 1; i += 2) {
            path.reset();
            path.moveTo(i * bw * 0.2f, 0);
            for (int k = 1; k <= 6; k++) {
                float t = k / 6f;
                path.lineTo(i * bw * 0.2f + (float) Math.sin(time * 1.6f + i + t * 4f) * j.size * 0.25f, j.size * 1.4f * t);
            }
            c.drawPath(path, stroke);
        }

        path.reset();
        path.moveTo(-bw, 0);
        path.cubicTo(-bw, -bh * 1.35f, bw, -bh * 1.35f, bw, 0);
        path.quadTo(bw * 0.5f, -bh * 0.18f, 0, bh * 0.05f);
        path.quadTo(-bw * 0.5f, -bh * 0.18f, -bw, 0);
        path.close();
        fill.setColor(0xFF000000); // shader fills still take the paint's alpha
        fill.setShader(j.shader);
        c.drawPath(path, fill);
        fill.setShader(null);
        c.restore();
    }

    private static float clamp(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // ---------------------------------------------------------------- model types

    /** Shapes for one kind of fish in unit space: nose at x=+1, tail root near x=-0.7. */
    private static final class Species {
        final int kind;
        final float bodyH;
        final int fin;
        final Path body = new Path(), tail = new Path(), dorsal = new Path(), ventral = new Path(), pattern = new Path();
        final Shader shader;

        Species(int kind, float h, float spread, int top, int bottom, int fin) {
            this.kind = kind;
            this.bodyH = h;
            this.fin = fin;
            shader = new LinearGradient(0, -h, 0, h, top, bottom, Shader.TileMode.CLAMP);

            body.moveTo(1f, 0);
            body.cubicTo(0.92f, -h * 0.95f, -0.3f, -h * 1.15f, -0.8f, -h * 0.22f);
            body.lineTo(-0.8f, h * 0.22f);
            body.cubicTo(-0.3f, h * 1.15f, 0.92f, h * 0.95f, 1f, 0);
            body.close();

            float reach = kind == GOLD ? -1.6f : -1.45f;
            float t = spread;
            tail.moveTo(-0.7f, 0);
            tail.quadTo(-1.0f, -t * 0.35f, reach, -t);
            tail.quadTo(reach + 0.25f, 0, reach, t);
            tail.quadTo(-1.0f, t * 0.35f, -0.7f, 0);
            tail.close();

            if (kind == ANGEL) {
                dorsal.moveTo(0.3f, -h * 0.8f);
                dorsal.quadTo(-0.2f, -h * 1.9f, -0.8f, -h * 2.3f);
                dorsal.quadTo(-0.55f, -h * 1.2f, -0.55f, -h * 0.4f);
                dorsal.close();
                ventral.moveTo(0.3f, h * 0.8f);
                ventral.quadTo(-0.2f, h * 1.9f, -0.8f, h * 2.3f);
                ventral.quadTo(-0.55f, h * 1.2f, -0.55f, h * 0.4f);
                ventral.close();
            } else {
                dorsal.moveTo(0.4f, -h * 0.8f);
                dorsal.quadTo(0.0f, -h * 1.5f, -0.6f, -h * 0.55f);
                dorsal.close();
                ventral.moveTo(-0.05f, h * 0.8f);
                ventral.quadTo(-0.35f, h * 1.3f, -0.62f, h * 0.5f);
                ventral.close();
            }

            if (kind == TANG) {
                pattern.moveTo(0.55f, -h * 0.5f);
                pattern.cubicTo(0.15f, -h * 0.05f, -0.35f, -h * 0.15f, -0.85f, -h * 0.55f);
                pattern.cubicTo(-0.4f, h * 0.35f, 0.15f, h * 0.2f, 0.55f, -h * 0.5f);
                pattern.close();
            }
        }
    }

    private static final class Fish {
        Species sp;
        boolean near;
        School school;
        float x, y, vx, vy, size, speed, phase, turn, tx, ty, retarget, ox, oy;
    }

    private static final class School {
        float x, y, tx, ty, timer;
    }

    private static final class Plant {
        static final int SEGMENTS = 12;
        final float[] cx = new float[SEGMENTS + 1], cy = new float[SEGMENTS + 1], ca = new float[SEGMENTS + 1];
        float x, baseY, height, width, phase, speed, amp;
        Shader shader;
    }

    private static final class Bubble {
        float x, y, r, vy, phase;
    }

    private static final class Food {
        float x, y, vy, phase, life;
        int color;
        boolean eaten;
    }

    private static final class Ray {
        float x, top, bottom, phase, speed;
    }

    private static final class Jelly {
        float x, y, vx, vy, size, phase;
        Shader shader;
    }
}
