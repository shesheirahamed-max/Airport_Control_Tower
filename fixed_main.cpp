

#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/freeglut.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <vector>
#include <algorithm>

// Some old Windows gl.h files (OpenGL 1.1) lack these constants.
#ifndef GL_LIGHT_MODEL_COLOR_CONTROL
#define GL_LIGHT_MODEL_COLOR_CONTROL 0x81F8
#endif
#ifndef GL_SEPARATE_SPECULAR_COLOR
#define GL_SEPARATE_SPECULAR_COLOR 0x81FA
#endif

static const float PI  = 3.14159265f;
static const float DEG = PI / 180.0f;

// -----------------------------------------------------------------------------
//  World constants
//    Tower cab floor is at y = 0 (12 m tall tower shaft below it).
//    Ground is at y = G.  Runway runs along the X axis in front of the tower
//    (the tower window looks toward -Z).
// -----------------------------------------------------------------------------
static const float G  = -10.0f;        // ground level
static const float RW = G + 0.05f;     // runway / apron surface level

// -----------------------------------------------------------------------------
//  Global state
// -----------------------------------------------------------------------------
struct Camera { float x, y, z, yaw, pitch; };
static Camera cam = { 0.0f, 1.75f, 1.0f, 0.0f, -4.0f };   // yaw 0 => looks toward -Z
static bool  freeCam   = false;      // false: clamped inside the tower cab
static float fov       = 60.0f;
static int   winW = 1280, winH = 720;

static bool keyState[256]  = { false };   // normal keys (lower-case)
static bool specState[256] = { false };   // special keys (arrows)

static bool  lightSun = true;   // GL_LIGHT0 on/off
static bool  lightLamp = true;  // GL_LIGHT1 on/off
static bool  night = false;
static bool  wireframe = false;
static int   viewPreset = 0;

static float tNow = 0.0f;          // seconds since start
static float radarAngle = 0.0f;    // degrees, continuous rotation of the dish

struct Airplane {                  // object-transform state of the controllable plane
    float x, z, yaw, pitch, roll, alt, scale, speed;
};
static Airplane plane = { -260.0f, -95.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.6f, 3.0f };
static bool autoMode = true;       // automatic take-off loop

static GLuint texBrick, texAsphalt, texMetal, texConcrete, texTile, texGrass;
static GLUquadric* quad = NULL;

static std::vector<float> starDirs;   // x,y,z triplets for the night sky

// -----------------------------------------------------------------------------
//  Small utilities
// -----------------------------------------------------------------------------
// Material helper: ambient / diffuse / specular / shininess (all via glMaterialfv)
static void mat(float r, float g, float b, float spec = 0.3f, float shin = 32.0f, float a = 1.0f)
{
    GLfloat amb[4] = { r * 0.35f, g * 0.35f, b * 0.35f, a };
    GLfloat dif[4] = { r, g, b, a };
    GLfloat spe[4] = { spec, spec, spec, 1.0f };
    GLfloat emi[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT,   amb);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE,   dif);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR,  spe);
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION,  emi);
    glMaterialf (GL_FRONT_AND_BACK, GL_SHININESS, shin);
}
// Same as mat() but with an emissive (glowing) component
static void matEmit(float r, float g, float b, float e)
{
    mat(r, g, b, 0.5f, 30.0f);
    GLfloat em[4] = { r * e, g * e, b * e, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, em);
}
static void useTex(GLuint t) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, t); }
static void noTex()          { glDisable(GL_TEXTURE_2D); }

// -----------------------------------------------------------------------------
//  Procedural textures + BMP loader
// -----------------------------------------------------------------------------
typedef std::vector<unsigned char> Img;

static unsigned int hash2(int x, int y, int s)
{
    unsigned int h = (unsigned int)x * 374761393u + (unsigned int)y * 668265263u + (unsigned int)s * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static float rnd(int x, int y, int s = 0) { return (hash2(x, y, s) & 0xFFFF) / 65535.0f; }
static unsigned char clampB(float v) { return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v)); }
static void put(Img& im, int W, int x, int y, float r, float g, float b)
{
    int i = (y * W + x) * 3;
    im[i] = clampB(r); im[i + 1] = clampB(g); im[i + 2] = clampB(b);
}

static Img genBrick(int W, int H)          // red bricks + light mortar
{
    Img im(W * H * 3);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int row = y / 16, off = (row & 1) * 16, xx = (x + off) % W;
        int bx = xx % 32, by = y % 16;
        if (bx < 2 || by < 2) {
            float n = (rnd(x, y, 7) - 0.5f) * 20;
            put(im, W, x, y, 185 + n, 182 + n, 172 + n);
        } else {
            float v = rnd(xx / 32, row, 1), n = (rnd(x, y, 2) - 0.5f) * 26;
            put(im, W, x, y, 138 + v * 55 + n, 58 + v * 28 + n * 0.6f, 42 + v * 18 + n * 0.5f);
        }
    }
    return im;
}
static Img genAsphalt(int W, int H)        // dark grainy asphalt
{
    Img im(W * H * 3);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float v = 52 + rnd(x, y, 3) * 32 + (rnd(x / 8, y / 8, 4) - 0.5f) * 18;
        if (rnd(x, y, 5) > 0.985f) v += 70;          // light aggregate speckles
        put(im, W, x, y, v, v, v * 1.02f);
    }
    return im;
}
static Img genMetal(int W, int H)          // brushed steel panels with seams + rivets
{
    Img im(W * H * 3);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float v = 150 + (rnd(0, y, 4) - 0.5f) * 36 + (rnd(x, y, 5) - 0.5f) * 10;
        int px = x % 64, py = y % 64;
        if (px == 0 || py == 0) v *= 0.55f;
        int dx = px - 6, dy = py - 6, ex = px - 57, ey = py - 57;
        if (dx * dx + dy * dy < 5 || ex * ex + ey * ey < 5) v = 215;
        put(im, W, x, y, v * 0.90f, v * 0.95f, v);
    }
    return im;
}
static Img genConcrete(int W, int H)
{
    Img im(W * H * 3);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float v = 165 + (rnd(x, y, 8) - 0.5f) * 30 + (rnd(x / 6, y / 6, 9) - 0.5f) * 24;
        put(im, W, x, y, v, v, v * 0.97f);
    }
    return im;
}
static Img genTile(int W, int H)           // floor tiles with grout
{
    Img im(W * H * 3);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int bx = x % 32, by = y % 32;
        float v;
        if (bx == 0 || by == 0) v = 95;
        else v = 165 + (rnd(x / 32, y / 32, 6) - 0.5f) * 40 + (rnd(x, y, 10) - 0.5f) * 8;
        put(im, W, x, y, v * 0.95f, v * 0.97f, v);
    }
    return im;
}
static Img genGrass(int W, int H)
{
    Img im(W * H * 3);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float n = rnd(x / 3, y / 3, 11) * 0.6f + rnd(x, y, 12) * 0.4f;
        put(im, W, x, y, 40 + n * 35, 90 + n * 70, 30 + n * 25);
    }
    return im;
}

// Minimal 24-bit uncompressed BMP loader (optional external textures)
static bool loadBMP(const char* fn, int& w, int& h, Img& rgb)
{
    FILE* f = fopen(fn, "rb");
    if (!f) return false;
    unsigned char hdr[54];
    if (fread(hdr, 1, 54, f) != 54 || hdr[0] != 'B' || hdr[1] != 'M') { fclose(f); return false; }
    int dataPos = *(int*)&hdr[10];
    w = *(int*)&hdr[18];
    h = *(int*)&hdr[22];
    short bpp = *(short*)&hdr[28];
    int comp = *(int*)&hdr[30];
    if (h < 0) h = -h;
    if (bpp != 24 || comp != 0 || w <= 0 || h <= 0) { fclose(f); return false; }
    int rowSize = ((w * 3 + 3) / 4) * 4;
    rgb.assign(w * h * 3, 0);
    fseek(f, dataPos, SEEK_SET);
    std::vector<unsigned char> row(rowSize);
    for (int y = 0; y < h; y++) {
        if (fread(row.data(), 1, rowSize, f) != (size_t)rowSize) { fclose(f); return false; }
        for (int x = 0; x < w; x++) {              // BGR -> RGB
            rgb[(y * w + x) * 3 + 0] = row[x * 3 + 2];
            rgb[(y * w + x) * 3 + 1] = row[x * 3 + 1];
            rgb[(y * w + x) * 3 + 2] = row[x * 3 + 0];
        }
    }
    fclose(f);
    return true;
}

static GLuint makeTex(const char* file, Img (*gen)(int, int))
{
    int w = 128, h = 128;
    Img px;
    if (loadBMP(file, w, h, px)) printf("Loaded texture file: %s\n", file);
    else px = gen(w, h);                                   // procedural fallback
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    return id;
}

// -----------------------------------------------------------------------------
//  3D geometry primitives (with normals + texture coordinates)
// -----------------------------------------------------------------------------
// A flat, subdivided parallelogram: origin o, edges u and v (u x v = normal n).
// Texture repeats uRep x vRep times.  'cell' = max size of a subdivision quad
// (finer meshes give smoother per-vertex lighting from the point light).
static void face(float ox, float oy, float oz, float ux, float uy, float uz,
                 float vx, float vy, float vz, float nx, float ny, float nz,
                 float uRep, float vRep, float cell)
{
    float lu = sqrtf(ux * ux + uy * uy + uz * uz), lv = sqrtf(vx * vx + vy * vy + vz * vz);
    int nu = std::max(1, (int)ceilf(lu / cell)), nv = std::max(1, (int)ceilf(lv / cell));
    glNormal3f(nx, ny, nz);
    for (int i = 0; i < nu; i++) {
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= nv; j++) {
            float b = (float)j / nv;
            for (int k = 0; k < 2; k++) {
                float a = (float)(i + k) / nu;
                glTexCoord2f(a * uRep, b * vRep);
                glVertex3f(ox + ux * a + vx * b, oy + uy * a + vy * b, oz + uz * a + vz * b);
            }
        }
        glEnd();
    }
}

// Axis-aligned box centred at the origin.  'tile' = world size of one texture repeat.
static void drawBox(float sx, float sy, float sz, float tile = 1.0f, float cell = 1e9f)
{
    float x = sx / 2, y = sy / 2, z = sz / 2;
    face(-x, -y,  z,  2 * x, 0, 0,  0, 2 * y, 0,   0, 0, 1,  sx / tile, sy / tile, cell); // +Z
    face( x, -y, -z, -2 * x, 0, 0,  0, 2 * y, 0,   0, 0,-1,  sx / tile, sy / tile, cell); // -Z
    face( x, -y,  z,  0, 0, -2 * z, 0, 2 * y, 0,   1, 0, 0,  sz / tile, sy / tile, cell); // +X
    face(-x, -y, -z,  0, 0,  2 * z, 0, 2 * y, 0,  -1, 0, 0,  sz / tile, sy / tile, cell); // -X
    face(-x,  y,  z,  2 * x, 0, 0,  0, 0, -2 * z,  0, 1, 0,  sx / tile, sz / tile, cell); // +Y
    face(-x, -y, -z,  2 * x, 0, 0,  0, 0,  2 * z,  0,-1, 0,  sx / tile, sz / tile, cell); // -Y
}

// Horizontal quad on the XZ plane facing up
static void planeXZ(float x0, float x1, float z0, float z1, float y, float tile, float cell = 1e9f)
{
    face(x0, y, z1, x1 - x0, 0, 0, 0, 0, -(z1 - z0), 0, 1, 0, (x1 - x0) / tile, (z1 - z0) / tile, cell);
}

// Cylinder standing along +Y (with caps)
static void cylY(float r, float h, int sl = 20)
{
    glPushMatrix();
    glRotatef(-90, 1, 0, 0);
    gluCylinder(quad, r, r, h, sl, 1);
    gluDisk(quad, 0, r, sl, 1);
    glTranslatef(0, 0, h);
    gluDisk(quad, 0, r, sl, 1);
    glPopMatrix();
}
// Cylinder / cone along +X (radius r0 at x=0 to r1 at x=len)
static void cylX(float r0, float r1, float len, int sl = 20)
{
    glPushMatrix();
    glRotatef(90, 0, 1, 0);
    gluCylinder(quad, r0, r1, len, sl, 1);
    glPopMatrix();
}
static void sphere(float r, int sl = 16, int st = 12) { glutSolidSphere(r, sl, st); }

// =============================================================================
//  INTERIOR OBJECTS
// =============================================================================
// Draws animated "text lines" inside a screen (call with lighting disabled).
static void screenBars(float w, float h, int rows, float seed, float r, float g, float b)
{
    glLineWidth(1.5f);
    glBegin(GL_LINES);
    for (int i = 0; i < rows; i++) {
        float y = h / 2 - (i + 1) * (h / (rows + 1));
        float len = (0.25f + 0.7f * fabsf(sinf(i * 1.7f + seed))) * w * 0.8f;
        float blink = (sinf(tNow * 2.0f + i * 0.9f + seed) > -0.6f) ? 1.0f : 0.4f;
        glColor3f(r * blink, g * blink, b * blink);
        glVertex3f(-w * 0.45f, y, 0);
        glVertex3f(-w * 0.45f + len, y, 0);
    }
    glEnd();
}

// A 3D computer monitor (stand, neck, bezel, glowing screen). Faces +Z.
static void drawMonitor(float seed)
{
    mat(0.08f, 0.08f, 0.09f, 0.5f, 40.0f);
    cylY(0.13f, 0.02f);
    glPushMatrix(); glTranslatef(0, 0.11f, -0.02f); drawBox(0.05f, 0.20f, 0.04f); glPopMatrix();
    glPushMatrix(); glTranslatef(0, 0.40f, 0);      drawBox(0.62f, 0.38f, 0.05f); glPopMatrix();

    glPushAttrib(GL_ENABLE_BIT | GL_LIGHTING_BIT | GL_CURRENT_BIT | GL_LINE_BIT);
    glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D);
    glPushMatrix();
    glTranslatef(0, 0.40f, 0.0265f);
    glColor3f(0.02f, 0.07f, 0.12f);
    glBegin(GL_QUADS);
    glVertex3f(-0.28f, -0.16f, 0); glVertex3f(0.28f, -0.16f, 0);
    glVertex3f(0.28f, 0.16f, 0);   glVertex3f(-0.28f, 0.16f, 0);
    glEnd();
    glTranslatef(0, 0, 0.001f);
    screenBars(0.56f, 0.32f, 8, seed, 0.2f, 1.0f, 0.45f);
    glPopMatrix();
    glPopAttrib();
}

// Radar scope (drawn on the XZ plane, north = -Z). Lighting is switched off inside.
static void drawRadarScope(float R, float range)
{
    glPushAttrib(GL_ENABLE_BIT | GL_LIGHTING_BIT | GL_CURRENT_BIT | GL_LINE_BIT | GL_POINT_BIT | GL_COLOR_BUFFER_BIT);
    glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const int N = 48;
    glColor3f(0.0f, 0.10f, 0.04f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(0, 0, 0);
    for (int i = 0; i <= N; i++) { float a = 2 * PI * i / N; glVertex3f(R * sinf(a), 0, -R * cosf(a)); }
    glEnd();

    glLineWidth(1.0f);
    glColor3f(0.0f, 0.55f, 0.2f);
    for (int k = 1; k <= 3; k++) {                          // range rings
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < N; i++) { float a = 2 * PI * i / N; glVertex3f(R * k / 3 * sinf(a), 0.002f, -R * k / 3 * cosf(a)); }
        glEnd();
    }
    glBegin(GL_LINES);                                      // cross hairs
    glVertex3f(-R, 0.002f, 0); glVertex3f(R, 0.002f, 0);
    glVertex3f(0, 0.002f, -R); glVertex3f(0, 0.002f, R);
    glEnd();

    float sw = radarAngle;                                  // rotating sweep (same as the dish)
    glBegin(GL_TRIANGLE_FAN);
    glColor4f(0.1f, 1.0f, 0.3f, 0.55f);
    glVertex3f(0, 0.003f, 0);
    for (int i = 0; i <= 20; i++) {
        float a = (sw - i * 2.5f) * DEG, al = 0.55f * (1.0f - i / 20.0f);
        glColor4f(0.1f, 1.0f, 0.3f, al);
        glVertex3f(R * sinf(a), 0.003f, -R * cosf(a));
    }
    glEnd();
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glColor3f(0.4f, 1.0f, 0.5f);
    glVertex3f(0, 0.004f, 0); glVertex3f(R * sinf(sw * DEG), 0.004f, -R * cosf(sw * DEG));
    glEnd();

    // Targets: the real airplane + a few fixed contacts.  Phosphor decay after the sweep passes.
    struct T { float x, z, r, g, b; };
    T targets[4] = { { plane.x, plane.z, 1.0f, 1.0f, 0.2f }, { -120, -60, 0.3f, 1, 0.3f },
                     { 150, -200, 0.3f, 1, 0.3f }, { -40, -130, 0.3f, 1, 0.3f } };
    glPointSize(7.0f);
    glBegin(GL_POINTS);
    for (int i = 0; i < 4; i++) {
        float dx = targets[i].x * R / range, dz = targets[i].z * R / range;
        if (dx * dx + dz * dz > R * R * 0.9f) continue;
        float th = atan2f(targets[i].x, -targets[i].z) / DEG;
        if (th < 0) th += 360;
        float diff = fmodf(sw - th + 720.0f, 360.0f);
        float br = 1.0f - 0.85f * (diff / 360.0f);
        glColor3f(targets[i].r * br, targets[i].g * br, targets[i].b * br);
        glVertex3f(dx, 0.005f, dz);
    }
    glEnd();
    glPopAttrib();
}

// 3D radar console: cabinet + slanted panel with scope, blinking buttons, joystick. Operator on +Z.
static void drawRadarConsole(float range)
{
    mat(0.72f, 0.76f, 0.82f, 0.7f, 60.0f);
    useTex(texMetal);
    glPushMatrix(); glTranslatef(0, 0.4f, 0); drawBox(2.0f, 0.8f, 1.1f, 1.0f); glPopMatrix();
    noTex();

    glPushMatrix();
    glTranslatef(0, 0.88f, -0.05f);
    glRotatef(50, 1, 0, 0);                                 // tilt toward the operator
    mat(0.14f, 0.15f, 0.17f, 0.6f, 50.0f);
    drawBox(1.8f, 0.06f, 0.95f);

    glPushMatrix();                                         // scope + bezel
    glTranslatef(0, 0.031f, -0.02f);
    mat(0.05f, 0.05f, 0.06f, 0.9f, 80.0f);
    glPushMatrix(); glRotatef(90, 1, 0, 0); glutSolidTorus(0.02, 0.34, 8, 36); glPopMatrix();
    drawRadarScope(0.32f, range);
    glPopMatrix();

    for (int i = 0; i < 12; i++) {                          // button row
        float cr = (i % 3 == 0) ? 1.0f : 0.1f, cg = (i % 3 == 1) ? 1.0f : 0.3f, cb = (i % 3 == 2) ? 0.1f : 0.1f;
        bool lit = (((int)(tNow * 1.5f) + i) % 4 == 0);
        if (lit) matEmit(cr, cg, cb, 0.9f); else mat(cr * 0.5f, cg * 0.5f, cb * 0.5f, 0.4f, 20.0f);
        glPushMatrix(); glTranslatef(-0.8f + i * 0.145f, 0.045f, 0.36f); drawBox(0.07f, 0.03f, 0.05f); glPopMatrix();
    }
    mat(0.08f, 0.08f, 0.09f, 0.7f, 60.0f);                  // joystick
    glPushMatrix();
    glTranslatef(0.72f, 0.03f, 0.05f);
    cylY(0.05f, 0.03f);
    glTranslatef(0, 0.03f, 0); cylY(0.015f, 0.14f);
    glTranslatef(0, 0.14f, 0); sphere(0.04f, 12, 8);
    glPopMatrix();
    glPopMatrix();
}

// Tea / chair interaction state
enum HumanAction { HA_FREE, HA_SITTING, HA_DRINKING };
static HumanAction humanAction = HA_FREE;
static int activeChair = -1;                 // 0 = left side chair, 1 = right side chair
static float sitBlend = 0.0f;
static float drinkClock = 0.0f;
static const float CHAIR_ROLL_IN = 0.65f;

static bool sideChairInUse(int i) { return activeChair == i && humanAction != HA_FREE; }

static void drawTeaCupAndSteam(float x,float y,float z,int cupId);
// Controller desk with three monitors, keyboards, mouse, microphone. Operator on +Z.
static void drawWorkstation(float seed)
{
    mat(0.75f, 0.78f, 0.82f, 0.7f, 60.0f);
    useTex(texMetal);
    glPushMatrix(); glTranslatef(0, 0.90f, 0);            drawBox(3.6f, 0.06f, 1.0f, 1.0f);  glPopMatrix(); // top
    glPushMatrix(); glTranslatef(-1.75f, 0.435f, 0);      drawBox(0.08f, 0.87f, 0.95f, 1.0f); glPopMatrix();
    glPushMatrix(); glTranslatef(1.75f, 0.435f, 0);       drawBox(0.08f, 0.87f, 0.95f, 1.0f); glPopMatrix();
    glPushMatrix(); glTranslatef(0, 0.5f, -0.45f);        drawBox(3.4f, 0.7f, 0.05f, 1.0f);  glPopMatrix();
    glPushMatrix(); glTranslatef(1.3f, 0.33f, 0);         drawBox(0.7f, 0.6f, 0.85f, 0.5f);  glPopMatrix(); // drawers
    noTex();
    mat(0.05f, 0.05f, 0.05f, 0.9f, 80.0f);                // drawer handles
    glPushMatrix(); glTranslatef(1.3f, 0.5f, 0.44f);  drawBox(0.3f, 0.02f, 0.02f); glPopMatrix();
    glPushMatrix(); glTranslatef(1.3f, 0.2f, 0.44f);  drawBox(0.3f, 0.02f, 0.02f); glPopMatrix();

    for (int i = 0; i < 3; i++) {                          // three monitors, angled inward
        float x = (i - 1) * 1.1f;
        glPushMatrix();
        glTranslatef(x, 0.93f, -0.2f);
        glRotatef((1 - i) * 18.0f, 0, 1, 0);
        drawMonitor(seed + i * 2.3f);
        glPopMatrix();
    }
    mat(0.12f, 0.12f, 0.13f, 0.4f, 20.0f);                 // keyboards
    for (int i = 0; i < 3; i++) {
        glPushMatrix(); glTranslatef((i - 1) * 1.1f, 0.94f, 0.28f); drawBox(0.5f, 0.02f, 0.16f); glPopMatrix();
    }
    glPushMatrix(); glTranslatef(0.35f, 0.945f, 0.28f); glScalef(1, 0.5f, 1.5f); sphere(0.03f, 10, 8); glPopMatrix(); // mouse
    glPushMatrix(); glTranslatef(1.55f, 0.93f, 0.3f);                                               // microphone
    cylY(0.01f, 0.25f); glTranslatef(0, 0.25f, 0); sphere(0.03f, 10, 8); glPopMatrix();
}

// Swivel controller chair. Faces -Z (seat front), backrest on +Z.
static void drawChair(float swivel, float slide = 0.0f)
{
    glPushMatrix();
    glTranslatef(0, 0, -slide);
    glRotatef(swivel, 0, 1, 0);
    mat(0.10f, 0.10f, 0.12f, 0.6f, 50.0f);
    for (int i = 0; i < 5; i++) {                          // 5-star base with wheels
        glPushMatrix();
        glRotatef(i * 72.0f, 0, 1, 0);
        glPushMatrix(); glTranslatef(0.2f, 0.09f, 0); drawBox(0.4f, 0.03f, 0.05f); glPopMatrix();
        glPushMatrix(); glTranslatef(0.38f, 0.04f, 0); sphere(0.04f, 10, 8); glPopMatrix();
        glPopMatrix();
    }
    glPushMatrix(); glTranslatef(0, 0.1f, 0); cylY(0.03f, 0.4f); glPopMatrix();  // gas-lift
    mat(0.10f, 0.20f, 0.45f, 0.15f, 10.0f);                                     // fabric
    glPushMatrix(); glTranslatef(0, 0.52f, 0);             drawBox(0.5f, 0.08f, 0.5f);  glPopMatrix();
    glPushMatrix(); glTranslatef(0, 0.84f, 0.24f); glRotatef(-8, 1, 0, 0); drawBox(0.46f, 0.5f, 0.07f); glPopMatrix();
    mat(0.08f, 0.08f, 0.09f, 0.6f, 40.0f);
    for (int s = -1; s <= 1; s += 2) {                     // armrests
        glPushMatrix(); glTranslatef(s * 0.28f, 0.68f, 0.0f); drawBox(0.05f, 0.03f, 0.3f); glPopMatrix();
        glPushMatrix(); glTranslatef(s * 0.28f, 0.6f, 0.1f);  drawBox(0.04f, 0.14f, 0.04f); glPopMatrix();
    }
    glPopMatrix();
}

// Wall-mounted flight information board (faces -Z when placed on the back wall)
static void drawFlightBoard()
{
    glPushMatrix();
    glTranslatef(0, 2.4f, 5.9f);
    glRotatef(180, 0, 1, 0);
    mat(0.06f, 0.06f, 0.07f, 0.6f, 40.0f);
    drawBox(7.0f, 1.8f, 0.1f);
    glPushAttrib(GL_ENABLE_BIT | GL_LIGHTING_BIT | GL_CURRENT_BIT | GL_LINE_BIT);
    glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D);
    glTranslatef(0, 0, 0.055f);
    glColor3f(0.02f, 0.04f, 0.08f);
    glBegin(GL_QUADS);
    glVertex3f(-3.4f, -0.85f, 0); glVertex3f(3.4f, -0.85f, 0);
    glVertex3f(3.4f, 0.85f, 0);   glVertex3f(-3.4f, 0.85f, 0);
    glEnd();
    glTranslatef(0, 0, 0.002f);
    screenBars(6.6f, 1.6f, 9, 1.3f, 1.0f, 0.75f, 0.1f);
    glPopAttrib();
    glPopMatrix();
}

// Ceiling light fixture (glows when lamp is on)
static void drawCeilingLamp(float x, float z)
{
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT);
    glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D);
    if (lightLamp) glColor3f(1.0f, 0.97f, 0.85f); else glColor3f(0.35f, 0.35f, 0.35f);
    glPushMatrix(); glTranslatef(x, 3.96f, z); glScalef(1.4f, 0.06f, 0.5f); glutSolidCube(1.0); glPopMatrix();
    glPopAttrib();
}

// =============================================================================
//  TOWER STRUCTURE (walls with panoramic windows)
// =============================================================================
// Wall in local coordinates: inner face at z = 0, body toward -z (0.4 thick),
// x from -len/2..len/2.  Glass spans [gs,ge] (gs>=ge => solid wall).
static void drawWindowWall(float len, float gs, float ge)
{
    const float H = 4.0f, sill = 0.4f, head = 3.6f, T = 0.4f;
    mat(0.85f, 0.8f, 0.78f, 0.15f, 12.0f);
    useTex(texBrick);
    if (ge - gs < 0.01f) {                                  // solid wall
        glPushMatrix(); glTranslatef(0, H / 2, -T / 2); drawBox(len, H, T, 2.0f, 1.5f); glPopMatrix();
        noTex(); return;
    }
    glPushMatrix(); glTranslatef(0, sill / 2, -T / 2); drawBox(len, sill, T, 2.0f, 1.5f); glPopMatrix();          // below window
    glPushMatrix(); glTranslatef(0, (head + H) / 2, -T / 2); drawBox(len, H - head, T, 2.0f, 1.5f); glPopMatrix(); // above window
    float w1 = gs + len / 2;                                                                                       // left solid piece
    if (w1 > 0.01f) { glPushMatrix(); glTranslatef(-len / 2 + w1 / 2, (sill + head) / 2, -T / 2); drawBox(w1, head - sill, T, 2.0f, 1.5f); glPopMatrix(); }
    float w2 = len / 2 - ge;                                                                                       // right solid piece
    if (w2 > 0.01f) { glPushMatrix(); glTranslatef(len / 2 - w2 / 2, (sill + head) / 2, -T / 2); drawBox(w2, head - sill, T, 2.0f, 1.5f); glPopMatrix(); }
    noTex();

    // window mullions (metal frame posts) + interior window sill
    mat(0.30f, 0.32f, 0.36f, 0.8f, 60.0f);
    useTex(texMetal);
    int panes = (int)ceilf((ge - gs) / 3.2f);
    for (int i = 0; i <= panes; i++) {
        float x = gs + (ge - gs) * i / panes;
        glPushMatrix(); glTranslatef(x, (sill + head) / 2, -0.1f); drawBox(0.16f, head - sill, 0.2f, 0.5f); glPopMatrix();
    }
    glPushMatrix(); glTranslatef((gs + ge) / 2, sill + 0.025f, 0.08f); drawBox(ge - gs, 0.05f, 0.3f, 0.5f); glPopMatrix();
    noTex();
}

// Semi-transparent glass pane for the wall above (drawn last, after all opaque geometry)
static void drawWindowGlass(float gs, float ge)
{
    glPushAttrib(GL_ENABLE_BIT | GL_LIGHTING_BIT | GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glBegin(GL_QUADS);
    glColor4f(0.55f, 0.75f, 0.85f, 0.10f); glVertex3f(gs, 0.4f, -0.2f); glVertex3f(ge, 0.4f, -0.2f);
    glColor4f(0.60f, 0.80f, 0.95f, 0.20f); glVertex3f(ge, 3.6f, -0.2f); glVertex3f(gs, 3.6f, -0.2f);
    glEnd();
    glPopAttrib();
}

static void drawRoom()
{
    // floor (tile) and ceiling (acoustic concrete): finely tessellated for smooth point-light shading
    mat(0.85f, 0.85f, 0.9f, 0.4f, 48.0f);
    useTex(texTile);
    glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1.0f, -1.0f);   // floor always wins over the slab just below it
    planeXZ(-8, 8, -6, 6, 0.0f, 1.0f, 1.0f);
    glDisable(GL_POLYGON_OFFSET_FILL);
    mat(0.9f, 0.9f, 0.9f, 0.05f, 8.0f);
    useTex(texConcrete);
    face(-8, 4.0f, -6, 16, 0, 0, 0, 0, 12, 0, -1, 0, 8, 6, 1.0f);
    noTex();

    // four walls (front & sides carry the panoramic windows)
    glPushMatrix(); glTranslatef(0, 0, -6);                       drawWindowWall(16.8f, -8, 8);   glPopMatrix();
    glPushMatrix(); glTranslatef(-8, 0, 0); glRotatef(90, 0, 1, 0);  drawWindowWall(12.0f, -2, 6);  glPopMatrix();
    glPushMatrix(); glTranslatef(8, 0, 0);  glRotatef(-90, 0, 1, 0); drawWindowWall(12.0f, -6, 2);  glPopMatrix();
    glPushMatrix(); glTranslatef(0, 0, 6);  glRotatef(180, 0, 1, 0); drawWindowWall(16.8f, 0, 0);   glPopMatrix();

    drawCeilingLamp(0.0f, 0.5f);
    drawCeilingLamp(-4.5f, -2.5f);
    drawCeilingLamp(4.5f, -2.5f);
    drawFlightBoard();
}

static void drawFurniture()
{
    // two radar consoles in front (left / right) leaving the centre view clear
    glPushMatrix(); glTranslatef(-4.8f, 0, -3.6f); drawRadarConsole(300.0f); glPopMatrix();
    glPushMatrix(); glTranslatef( 4.8f, 0, -3.6f); drawRadarConsole(120.0f); glPopMatrix();
    // controller chairs (gently swivelling)
    glPushMatrix(); glTranslatef(-4.8f, 0, -2.0f); drawChair(15.0f * sinf(tNow * 0.7f)); glPopMatrix();
    glPushMatrix(); glTranslatef( 4.8f, 0, -2.0f); drawChair(15.0f * sinf(tNow * 0.6f + 1.0f)); glPopMatrix();
    // side-wall workstations with 3 monitors each
    glPushMatrix(); glTranslatef(-7.35f, 0, 3.8f); glRotatef(90, 0, 1, 0);  drawWorkstation(0.5f); glPopMatrix();
    glPushMatrix(); glTranslatef( 7.35f, 0, 3.8f); glRotatef(-90, 0, 1, 0); drawWorkstation(2.0f); glPopMatrix();
    glPushMatrix(); glTranslatef(-5.9f, 0, 3.8f); glRotatef(90, 0, 1, 0);
    drawChair(sideChairInUse(0) ? 0.0f : 10.0f * sinf(tNow * 0.5f), sideChairInUse(0) ? CHAIR_ROLL_IN : 0.0f);
    glPopMatrix();
    glPushMatrix(); glTranslatef( 5.9f, 0, 3.8f); glRotatef(-90, 0, 1, 0);
    drawChair(sideChairInUse(1) ? 0.0f : -10.0f * sinf(tNow * 0.4f), sideChairInUse(1) ? CHAIR_ROLL_IN : 0.0f);
    glPopMatrix();

    // Two hot tea cups, one on each side workstation.
    for (int side = -1; side <= 1; side += 2) {
        glPushMatrix();
        glTranslatef(side * 7.35f, 0.0f, 3.8f);
        glRotatef(side == -1 ? 90.0f : -90.0f, 0, 1, 0);
        drawTeaCupAndSteam(0.0f, 0.96f, 0.20f, side == -1 ? 0 : 1);
        glPopMatrix();
    }
}

// =============================================================================
//  3D HUMAN (air-traffic controller) - built only from GLU/GLUT primitives
//  Model space: feet on y = 0, front of the body faces +Z, total height about 1.8 m.
// =============================================================================
struct Human {
    float x, z;        // position on the cab floor
    float yaw;         // facing angle in degrees (0 => faces +Z, 180 => faces -Z)
    float vx, vz;      // smoothed velocity (m/s)
    float phase;       // walk-cycle phase (radians)
    float amt;         // walk animation amount 0..1 (0 = standing still)
};
static Human human = { 0.0f, -2.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

static const float HUMAN_SPEED  = 2.0f;     // metres / second
static const float HUMAN_RADIUS = 0.28f;    // collision radius
static bool  humanCollide = true;           // set false to walk through furniture (room walls always block)

struct Obstacle { float cx, cz, hx, hz; };  // axis-aligned furniture footprint (centre + half sizes)
static const Obstacle obstacles[] = {
    { -4.8f, -3.6f, 1.00f, 0.55f },   // left radar console
    {  4.8f, -3.6f, 1.00f, 0.55f },   // right radar console
    { -4.8f, -2.0f, 0.42f, 0.42f },   // left front chair
    {  4.8f, -2.0f, 0.42f, 0.42f },   // right front chair
    { -7.35f, 3.8f, 0.50f, 1.80f },   // left side workstation
    {  7.35f, 3.8f, 0.50f, 1.80f },   // right side workstation
    { -5.9f,  3.8f, 0.42f, 0.42f },   // left side chair
    {  5.9f,  3.8f, 0.42f, 0.42f },   // right side chair
};

// true if a human standing at (x,z) would be outside the room or inside furniture
static bool humanBlocked(float x, float z)
{
    if (x < -7.5f || x > 7.5f || z < -5.5f || z > 5.5f) return true;      // room walls (inner faces at +-8 / +-6)
    if (!humanCollide) return false;
    for (size_t i = 0; i < sizeof(obstacles) / sizeof(obstacles[0]); i++) {
        const Obstacle& o = obstacles[i];
        if (fabsf(x - o.cx) < o.hx + HUMAN_RADIUS && fabsf(z - o.cz) < o.hz + HUMAN_RADIUS) return true;
    }
    return false;
}

// Tapered limb segment hanging down from the origin (length len, radius r0 at the top -> r1 at the bottom)
// with a round joint ball at the lower end so segments blend smoothly.
static void limb(float len, float r0, float r1)
{
    glPushMatrix();
    glRotatef(90, 1, 0, 0);                    // cylinder axis +Z -> -Y
    gluCylinder(quad, r0, r1, len, 16, 1);
    glPopMatrix();
    glPushMatrix(); glTranslatef(0, -len, 0); sphere(r1, 14, 10); glPopMatrix();
}

// Scaled sphere helper (ellipsoid) centred at (x,y,z)
static void ellipsoid(float x, float y, float z, float rx, float ry, float rz, int sl = 18, int st = 14)
{
    glPushMatrix(); glTranslatef(x, y, z); glScalef(rx, ry, rz); sphere(1.0f, sl, st); glPopMatrix();
}

static void humanHand(float side)               // palm + 4 fingers + thumb, hanging down from the wrist
{
    mat(0.88f, 0.69f, 0.55f, 0.2f, 12.0f);
    ellipsoid(0, -0.045f, 0, 0.034f, 0.05f, 0.018f, 12, 8);                         // palm
    for (int i = 0; i < 4; i++)                                                     // fingers
        ellipsoid(-0.021f + i * 0.014f, -0.105f - ((i == 1 || i == 2) ? 0.004f : 0.0f), 0.0f, 0.007f, 0.032f, 0.009f, 8, 6);
    glPushMatrix();                                                                  // thumb
    glTranslatef(side * 0.034f, -0.04f, 0.012f); glRotatef(side * 25.0f, 0, 0, 1); glRotatef(-20, 1, 0, 0);
    ellipsoid(0, -0.02f, 0, 0.009f, 0.028f, 0.010f, 8, 6);
    glPopMatrix();
}

static void humanArm(float side, float swing, float elbow)   // side: -1 left / +1 right, angles in degrees
{
    glPushMatrix();
    glTranslatef(side * 0.255f, 1.41f, 0);
    mat(0.93f, 0.93f, 0.96f, 0.2f, 12.0f);                                           // white shirt sleeve
    sphere(0.056f, 14, 10);                                                          // shoulder ball
    glRotatef(swing, 1, 0, 0);                                                       // shoulder swing
    glRotatef(side * 4.0f, 0, 0, 1);                                                 // arms hang slightly away from the body
    limb(0.29f, 0.050f, 0.042f);                                                     // upper arm
    glTranslatef(0, -0.29f, 0);
    glRotatef(elbow, 1, 0, 0);                                                       // elbow bend
    limb(0.25f, 0.040f, 0.032f);                                                     // forearm
    glTranslatef(0, -0.25f, 0);
    humanHand(side);
    glPopMatrix();
}

static void humanLeg(float side, float hip, float knee)
{
    glPushMatrix();
    glTranslatef(side * 0.095f, 0.93f, 0);
    glRotatef(hip, 1, 0, 0);                                                         // hip swing
    mat(0.08f, 0.10f, 0.20f, 0.15f, 10.0f);                                          // navy trousers
    limb(0.45f, 0.078f, 0.060f);                                                     // thigh
    glTranslatef(0, -0.45f, 0);
    glRotatef(knee, 1, 0, 0);                                                        // knee bend
    limb(0.43f, 0.057f, 0.043f);                                                     // shin
    glTranslatef(0, -0.43f, 0);
    glRotatef(-(hip + knee) * 0.75f, 1, 0, 0);                                       // ankle keeps the foot nearly flat
    mat(0.03f, 0.03f, 0.035f, 0.5f, 40.0f);                                          // black shoe
    glPushMatrix(); glTranslatef(0, -0.015f, 0.065f); drawBox(0.095f, 0.075f, 0.20f); glPopMatrix();   // sole + body
    ellipsoid(0, -0.015f, 0.165f, 0.0475f, 0.0375f, 0.045f, 12, 8);                                      // rounded toe
    ellipsoid(0, 0.0f, 0.0f, 0.048f, 0.05f, 0.055f, 12, 8);                                              // heel / ankle
    glPopMatrix();
}

struct V3 { float x, y, z; };

static V3 v3(float x, float y, float z) { V3 v = {x,y,z}; return v; }
static V3 vadd(V3 a, V3 b) { return v3(a.x+b.x,a.y+b.y,a.z+b.z); }
static V3 vmix(V3 a, V3 b, float t) { return v3(a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t); }

static void drawSegmentBetween(V3 a, V3 b, float r0, float r1)
{
    float dx=b.x-a.x, dy=b.y-a.y, dz=b.z-a.z;
    float len=sqrtf(dx*dx+dy*dy+dz*dz);
    if (len < 1e-5f) return;
    dx/=len; dy/=len; dz/=len;
    glPushMatrix();
    glTranslatef(a.x,a.y,a.z);
    float dot=std::max(-1.0f,std::min(1.0f,dy));
    float angle=acosf(dot)/DEG;
    float ax=dz, ay=0.0f, az=-dx;
    float al=sqrtf(ax*ax+az*az);
    if (al>1e-5f) glRotatef(angle,ax/al,ay,az/al);
    else if (dy<0) glRotatef(180,1,0,0);
    gluCylinder(quad,r0,r1,len,14,1);
    glPopMatrix();
}

static void drawPoseHand(V3 p, float side)
{
    glPushMatrix();
    glTranslatef(p.x,p.y,p.z);
    mat(0.88f,0.69f,0.55f,0.2f,12.0f);
    ellipsoid(0,0,0,0.035f,0.055f,0.025f,12,8);
    for(int i=0;i<4;i++) ellipsoid(-0.018f+i*0.012f,-0.045f,0,0.006f,0.025f,0.008f,8,6);
    glPopMatrix();
}

static void drawTeaCup(float x,float y,float z,float tilt=0.0f)
{
    glPushMatrix();
    glTranslatef(x,y,z);
    glRotatef(tilt,1,0,0);
    mat(0.93f,0.93f,0.95f,0.55f,50.0f);
    glPushMatrix();
    glTranslatef(0,0.08f,0);
    gluCylinder(quad,0.105f,0.085f,0.16f,24,2);
    glPopMatrix();
    mat(0.38f,0.16f,0.06f,0.25f,20.0f);
    glPushMatrix(); glTranslatef(0,0.245f,0); gluDisk(quad,0,0.075f,24,1); glPopMatrix();
    mat(0.93f,0.93f,0.95f,0.55f,50.0f);
    glPushMatrix(); glTranslatef(0.12f,0.16f,0); glutSolidTorus(0.012,0.055,10,20); glPopMatrix();
    glPushMatrix(); glTranslatef(0,0.045f,0); glutSolidTorus(0.012,0.108f,12,28); glPopMatrix();
    glPopMatrix();
}

static void drawSteamAt(float x,float y,float z)
{
    glPushAttrib(GL_ENABLE_BIT|GL_CURRENT_BIT|GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_POINT_BIT);
    glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glPointSize(4.0f);
    glBegin(GL_POINTS);
    for(int i=0;i<26;i++) {
        float phase=fmodf(tNow*0.65f+i*0.37f,1.0f);
        float xoff=0.045f*sinf(tNow*1.6f+i*1.7f);
        float zoff=0.045f*cosf(tNow*1.3f+i*1.1f);
        float px=x+xoff, py=y+phase*0.55f, pz=z+zoff;
        float a=0.55f*(1.0f-phase);
        glColor4f(0.9f,0.9f,0.9f,a);
        glVertex3f(px,py,pz);
    }
    glEnd();
    glPopAttrib();
}

static void drawTeaCupAndSteam(float x,float y,float z,int cupId)
{
    bool held = (humanAction==HA_DRINKING && activeChair==cupId);
    if (!held) drawTeaCup(x,y,z,0.0f);
    drawSteamAt(x,y+0.25f,z);
}

static void drawSittingHuman()
{
    // Human model in local space: feet at floor, front is +Z.
    float drinkT = (humanAction==HA_DRINKING) ? drinkClock : 0.0f;
    float phase=std::max(0.0f,std::min(1.0f,drinkT/8.6f));

    // Sitting legs: thighs forward, shins down, feet forward.
    mat(0.08f,0.10f,0.20f,0.15f,10.0f);
    for(int sd=-1;sd<=1;sd+=2) {
        V3 hip=v3(sd*0.095f,0.64f,0.0f);
        V3 knee=v3(sd*0.105f,0.58f,0.38f);
        V3 ankle=v3(sd*0.105f,0.13f,0.38f);
        drawSegmentBetween(hip,knee,0.078f,0.060f);
        drawSegmentBetween(knee,ankle,0.057f,0.043f);
        mat(0.03f,0.03f,0.035f,0.5f,40.0f);
        glPushMatrix(); glTranslatef(sd*0.105f,0.07f,0.43f); drawBox(0.095f,0.075f,0.22f); glPopMatrix();
    }

    // torso / pelvis
    mat(0.08f,0.10f,0.20f,0.15f,10.0f); ellipsoid(0,0.65f,0,0.18f,0.11f,0.12f);
    mat(0.93f,0.93f,0.96f,0.2f,12.0f);
    glPushMatrix(); glTranslatef(0,0.92f,0); glRotatef(-90,1,0,0); glScalef(1,0.62f,1); gluCylinder(quad,0.17f,0.21f,0.50f,24,4); glPopMatrix();
    ellipsoid(0,1.17f,0,0.21f,0.085f,0.13f,24,10);
    mat(0.45f,0.06f,0.08f,0.3f,20.0f);
    glPushMatrix(); glTranslatef(0,1.02f,0.126f); drawBox(0.034f,0.18f,0.012f); glPopMatrix();

    // neck and head
    mat(0.88f,0.69f,0.55f,0.2f,12.0f);
    glPushMatrix(); glTranslatef(0,1.18f,0); glRotatef(-90,1,0,0); gluCylinder(quad,0.058f,0.052f,0.14f,14,1); glPopMatrix();
    ellipsoid(0,1.40f,0,0.10f,0.122f,0.112f,24,18);
    ellipsoid(0,1.365f,0.04f,0.075f,0.08f,0.075f,18,12);
    mat(0.12f,0.08f,0.05f,0.3f,16.0f); ellipsoid(0,1.425f,-0.012f,0.107f,0.117f,0.117f,24,18);
    mat(0.97f,0.97f,0.97f,0.6f,60.0f);
    ellipsoid(-0.04f,1.41f,0.097f,0.016f,0.012f,0.012f,10,8); ellipsoid(0.04f,1.41f,0.097f,0.016f,0.012f,0.012f,10,8);
    mat(0.02f,0.02f,0.03f,0.8f,80.0f);
    ellipsoid(-0.04f,1.41f,0.107f,0.0075f,0.0075f,0.006f,8,6); ellipsoid(0.04f,1.41f,0.107f,0.0075f,0.0075f,0.006f,8,6);

    V3 shoulderL=v3(-0.255f,1.14f,0), shoulderR=v3(0.255f,1.14f,0);
    V3 restElbowL=v3(-0.32f,0.92f,0.25f), restElbowR=v3(0.32f,0.92f,0.25f);
    V3 restHandL=v3(-0.25f,0.78f,0.46f), restHandR=v3(0.25f,0.78f,0.46f);

    V3 cup=v3(0.0f,0.98f,0.85f);
    V3 mouth=v3(0.0f,1.35f,0.31f);
    V3 drinkHand=restHandR;
    if(humanAction==HA_DRINKING) {
        if(drinkT<1.8f) { float q=drinkT/1.8f; q=q*q*(3-2*q); drinkHand=vmix(restHandR,v3(0.22f,0.96f,0.73f),q); }
        else if(drinkT<3.8f) { float q=(drinkT-1.8f)/2.0f; q=q*q*(3-2*q); drinkHand=vmix(v3(0.22f,0.96f,0.73f),mouth,q); }
        else if(drinkT<5.4f) drinkHand=mouth;
        else if(drinkT<7.2f) { float q=(drinkT-5.4f)/1.8f; q=q*q*(3-2*q); drinkHand=vmix(mouth,v3(0.22f,0.96f,0.73f),q); }
        else { float q=(drinkT-7.2f)/1.4f; q=q*q*(3-2*q); drinkHand=vmix(v3(0.22f,0.96f,0.73f),restHandR,q); }
    }

    mat(0.93f,0.93f,0.96f,0.2f,12.0f);
    drawSegmentBetween(shoulderL,restElbowL,0.05f,0.042f); drawSegmentBetween(restElbowL,restHandL,0.04f,0.032f); drawPoseHand(restHandL,-1);
    V3 drinkElbow=vmix(restElbowR,v3(0.35f,1.0f,0.40f),0.55f);
    drawSegmentBetween(shoulderR,drinkElbow,0.05f,0.042f); drawSegmentBetween(drinkElbow,drinkHand,0.04f,0.032f); drawPoseHand(drinkHand,1);

    // Hold the cup with the right hand while drinking. Cup follows the hand.
    if(humanAction==HA_DRINKING && drinkT>=1.8f && drinkT<=7.2f) {
        float tilt=0.0f;
        if(drinkT>=3.8f && drinkT<=5.4f) tilt=-22.0f;
        drawTeaCup(drinkHand.x,drinkHand.y-0.02f,drinkHand.z,tilt);
        drawSteamAt(drinkHand.x,drinkHand.y+0.20f,drinkHand.z);
    }
}

static void drawHumanModel(float phase, float amt)
{
    float s = sinf(phase), c = cosf(phase);
    float legA = 28.0f * amt, armA = 24.0f * amt;
    float hipR = -legA * s,  hipL =  legA * s;                       // legs swing in opposite directions
    float kneeR = 38.0f * amt * std::max(0.0f,  c);                  // knee bends while the leg swings forward
    float kneeL = 38.0f * amt * std::max(0.0f, -c);
    float armR =  armA * s,  armL = -armA * s;                       // arms swing opposite to the legs
    float elbowR = -(10.0f + 18.0f * amt * std::max(0.0f, -s));
    float elbowL = -(10.0f + 18.0f * amt * std::max(0.0f,  s));
    float bob = 0.025f * amt * fabsf(s);                             // body rises and falls with each step

    glPushMatrix();
    glTranslatef(0, 0.01f + bob, 0);                                 // tiny lift so shoe soles never fight with the floor

    humanLeg(-1, hipL, kneeL);
    humanLeg(+1, hipR, kneeR);

    // ---- upper body (leans very slightly forward while walking) ----
    glPushMatrix();
    glTranslatef(0, 0.93f, 0); glRotatef(4.0f * amt, 1, 0, 0); glTranslatef(0, -0.93f, 0);

    mat(0.08f, 0.10f, 0.20f, 0.15f, 10.0f);                          // pelvis (trousers)
    ellipsoid(0, 0.93f, 0, 0.178f, 0.12f, 0.115f);
    mat(0.03f, 0.03f, 0.03f, 0.5f, 40.0f);                           // belt
    glPushMatrix(); glTranslatef(0, 0.945f, 0); glRotatef(-90, 1, 0, 0); glScalef(1.0f, 0.62f, 1.0f);
    gluCylinder(quad, 0.176f, 0.178f, 0.03f, 20, 1); glPopMatrix();
    mat(0.75f, 0.70f, 0.25f, 0.9f, 80.0f);                           // belt buckle
    glPushMatrix(); glTranslatef(0, 0.96f, 0.112f); drawBox(0.035f, 0.025f, 0.012f); glPopMatrix();

    mat(0.93f, 0.93f, 0.96f, 0.2f, 12.0f);                           // white shirt torso (elliptical cross-section)
    glPushMatrix(); glTranslatef(0, 0.95f, 0); glRotatef(-90, 1, 0, 0); glScalef(1.0f, 0.62f, 1.0f);
    gluCylinder(quad, 0.17f, 0.21f, 0.50f, 24, 4); glPopMatrix();
    ellipsoid(0, 1.45f, 0, 0.21f, 0.085f, 0.13f, 24, 10);            // chest / shoulder line
    mat(0.45f, 0.06f, 0.08f, 0.3f, 20.0f);                           // tie
    glPushMatrix(); glTranslatef(0, 1.33f, 0.126f); glRotatef(-4, 1, 0, 0); drawBox(0.034f, 0.20f, 0.012f); glPopMatrix();
    mat(0.10f, 0.10f, 0.14f, 0.4f, 30.0f);                           // epaulettes
    for (int sd = -1; sd <= 1; sd += 2) {
        glPushMatrix(); glTranslatef(sd * 0.215f, 1.47f, 0); glRotatef(sd * -18.0f, 0, 0, 1); drawBox(0.11f, 0.014f, 0.08f); glPopMatrix();
    }

    // ---- neck + head ----
    mat(0.88f, 0.69f, 0.55f, 0.2f, 12.0f);                           // skin
    glPushMatrix(); glTranslatef(0, 1.44f, 0); glRotatef(-90, 1, 0, 0); gluCylinder(quad, 0.058f, 0.052f, 0.15f, 14, 1); glPopMatrix();
    ellipsoid(0, 1.675f, 0.0f, 0.100f, 0.122f, 0.112f, 24, 18);      // head
    ellipsoid(0, 1.640f, 0.040f, 0.075f, 0.080f, 0.075f, 18, 12);    // cheeks / jaw
    ellipsoid(-0.101f, 1.670f, 0.0f, 0.016f, 0.028f, 0.020f, 10, 8); // ears
    ellipsoid( 0.101f, 1.670f, 0.0f, 0.016f, 0.028f, 0.020f, 10, 8);
    ellipsoid(0, 1.652f, 0.110f, 0.014f, 0.020f, 0.020f, 10, 8);     // nose
    mat(0.12f, 0.08f, 0.05f, 0.3f, 16.0f);                           // hair
    ellipsoid(0, 1.700f, -0.012f, 0.107f, 0.117f, 0.117f, 24, 18);
    mat(0.97f, 0.97f, 0.97f, 0.6f, 60.0f);                           // eyes (white)
    ellipsoid(-0.040f, 1.685f, 0.097f, 0.016f, 0.012f, 0.012f, 10, 8);
    ellipsoid( 0.040f, 1.685f, 0.097f, 0.016f, 0.012f, 0.012f, 10, 8);
    mat(0.02f, 0.02f, 0.03f, 0.8f, 80.0f);                           // pupils
    ellipsoid(-0.040f, 1.685f, 0.107f, 0.0075f, 0.0075f, 0.006f, 8, 6);
    ellipsoid( 0.040f, 1.685f, 0.107f, 0.0075f, 0.0075f, 0.006f, 8, 6);
    mat(0.15f, 0.08f, 0.05f, 0.2f, 10.0f);                           // eyebrows
    ellipsoid(-0.040f, 1.712f, 0.098f, 0.022f, 0.004f, 0.010f, 8, 6);
    ellipsoid( 0.040f, 1.712f, 0.098f, 0.022f, 0.004f, 0.010f, 8, 6);
    mat(0.60f, 0.25f, 0.25f, 0.3f, 20.0f);                           // mouth
    ellipsoid(0, 1.622f, 0.101f, 0.024f, 0.006f, 0.010f, 10, 6);

    humanArm(-1, armL, elbowL);
    humanArm(+1, armR, elbowR);
    glPopMatrix();   // upper body
    glPopMatrix();   // bob
}

// Draw the controllable human at its current floor position + facing, with a soft blob shadow.
static void drawHuman()
{
    noTex();
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT);
    glDisable(GL_LIGHTING);                                          // blob shadow on the tiles
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-4.0f, -12.0f);
    glColor4f(0, 0, 0, 0.30f);
    glPushMatrix();
    glTranslatef(human.x, 0.004f, human.z);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(0, 0, 0);
    for (int i = 0; i <= 24; i++) { float t = 2 * PI * i / 24; glVertex3f(cosf(t) * 0.34f, 0, sinf(t) * 0.26f); }
    glEnd();
    glPopMatrix();
    glPopAttrib();

    glPushMatrix();
    glTranslatef(human.x, 0.0f, human.z);
    glRotatef(human.yaw, 0, 1, 0);
    if (humanAction == HA_SITTING || humanAction == HA_DRINKING) drawSittingHuman();
    else drawHumanModel(human.phase, human.amt);
    glPopMatrix();
}

// =============================================================================
//  RADAR ANTENNA (continuously rotating dish)
// =============================================================================
static void drawDish(float R, float d)      // paraboloid opening toward +Y
{
    const int rings = 8, segs = 32;
    float k = d / (R * R);
    for (int i = 0; i < rings; i++) {
        float r0 = R * i / rings, r1 = R * (i + 1) / rings;
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= segs; j++) {
            float a = 2 * PI * j / segs, ca = cosf(a), sa = sinf(a);
            for (int m = 0; m < 2; m++) {
                float r = (m == 0) ? r0 : r1, x = r * ca, z = r * sa, y = k * r * r;
                float nx = -2 * k * x, ny = 1.0f, nz = -2 * k * z;
                float l = sqrtf(nx * nx + ny * ny + nz * nz);
                glNormal3f(nx / l, ny / l, nz / l);
                glVertex3f(x, y, z);
            }
        }
        glEnd();
    }
}

static void drawRadarAntenna(float s)
{
    glPushMatrix();
    glScalef(s, s, s);
    mat(0.75f, 0.75f, 0.78f, 0.8f, 60.0f);
    cylY(0.5f, 1.2f);                                       // pedestal
    glTranslatef(0, 1.2f, 0);
    mat(0.55f, 0.57f, 0.6f, 0.7f, 50.0f);
    cylY(0.35f, 0.2f);
    glTranslatef(0, 0.2f, 0);

    glPushMatrix();
    glRotatef(90.0f - radarAngle, 0, 1, 0);                 // <<< CONTINUOUS 3D ROTATION (bearing = radarAngle)
    mat(0.8f, 0.8f, 0.82f, 0.8f, 60.0f);
    glPushMatrix(); glTranslatef(0, 0.6f, 0); drawBox(0.25f, 1.2f, 0.25f); glPopMatrix();     // yoke
    glPushMatrix(); glTranslatef(-0.5f, 0.3f, 0); drawBox(0.7f, 0.15f, 0.15f); glPopMatrix(); // counter-weight arm
    glPushMatrix(); glTranslatef(-0.9f, 0.3f, 0); drawBox(0.3f, 0.35f, 0.35f); glPopMatrix();
    glPushMatrix();
    glTranslatef(0.2f, 1.5f, 0);
    glRotatef(-82, 0, 0, 1);                                // dish axis: +X, elevated 8 degrees
    mat(0.9f, 0.9f, 0.92f, 0.9f, 80.0f);
    drawDish(2.0f, 0.5f);
    mat(0.3f, 0.3f, 0.32f, 0.6f, 40.0f);
    cylY(0.03f, 2.0f);                                      // feed-horn arm (focus = R^2/4d = 2.0)
    glTranslatef(0, 2.0f, 0); sphere(0.09f, 12, 8);
    glPopMatrix();
    glPopMatrix();

    // blinking red aircraft-warning beacon
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT);
    glDisable(GL_LIGHTING);
    float b = (sinf(tNow * 5.0f) > 0.0f) ? 1.0f : 0.15f;
    glColor3f(b, 0.0f, 0.0f);
    glPushMatrix(); glTranslatef(0, 0.15f, 0.3f); sphere(0.1f, 8, 6); glPopMatrix();
    glPopAttrib();
    glPopMatrix();
}

// =============================================================================
//  AIRPLANE MODEL  (nose toward +X, wings along Z, up = +Y)
// =============================================================================
static void wheel(float x, float y, float z, float r, float w)
{
    glPushMatrix();
    glTranslatef(x, y, z - w / 2);
    mat(0.05f, 0.05f, 0.05f, 0.2f, 10.0f);
    gluCylinder(quad, r, r, w, 16, 1);
    gluDisk(quad, 0, r, 16, 1);
    glTranslatef(0, 0, w); gluDisk(quad, 0, r, 16, 1);
    glPopMatrix();
}

static void drawAirplaneModel(bool gear)
{
    // fuselage: cylinder + rounded nose + tapered tail cone
    mat(0.92f, 0.93f, 0.96f, 0.85f, 80.0f);
    glPushMatrix(); glTranslatef(-3.5f, 0, 0); cylX(0.7f, 0.7f, 7.0f, 24); glPopMatrix();
    glPushMatrix(); glTranslatef(3.5f, 0, 0); glScalef(1.6f, 0.7f, 0.7f); sphere(1.0f, 24, 16); glPopMatrix();
    glPushMatrix(); glTranslatef(-3.5f, 0, 0); glRotatef(-90, 0, 1, 0); gluCylinder(quad, 0.7f, 0.2f, 3.6f, 24, 1); glPopMatrix();
    // airline stripe
    mat(0.1f, 0.25f, 0.7f, 0.5f, 40.0f);
    glPushMatrix(); glTranslatef(-3.0f, -0.12f, 0); cylX(0.712f, 0.712f, 5.5f, 24); glPopMatrix();

    // cockpit + cabin windows
    mat(0.03f, 0.05f, 0.09f, 1.0f, 120.0f);
    glPushMatrix(); glTranslatef(4.0f, 0.38f, 0); glScalef(0.7f, 0.28f, 0.5f); sphere(1.0f, 12, 8); glPopMatrix();
    for (int i = 0; i < 9; i++) for (int s = -1; s <= 1; s += 2) {
        glPushMatrix(); glTranslatef(-2.6f + i * 0.6f, 0.22f, s * 0.68f); glScalef(0.13f, 0.1f, 0.04f); sphere(1.0f, 8, 6); glPopMatrix();
    }

    // swept wings, engines, struts
    for (int s = -1; s <= 1; s += 2) {
        mat(0.75f, 0.77f, 0.82f, 0.7f, 50.0f);
        glPushMatrix(); glTranslatef(0.2f, -0.25f, s * 3.0f); glRotatef(-28.0f * s, 0, 1, 0); drawBox(2.6f, 0.12f, 5.6f); glPopMatrix();
        mat(0.55f, 0.57f, 0.6f, 0.8f, 60.0f);
        glPushMatrix(); glTranslatef(-0.4f, -0.85f, s * 2.2f); cylX(0.38f, 0.38f, 1.9f, 20); glPopMatrix();
        glPushMatrix(); glTranslatef(1.5f, -0.85f, s * 2.2f); glRotatef(90, 0, 1, 0); gluDisk(quad, 0, 0.38f, 20, 1); glPopMatrix();
        mat(0.75f, 0.77f, 0.82f, 0.7f, 50.0f);
        glPushMatrix(); glTranslatef(0.5f, -0.55f, s * 2.2f); drawBox(0.15f, 0.5f, 0.2f); glPopMatrix();
        // horizontal stabilizers
        glPushMatrix(); glTranslatef(-6.6f, 0.15f, s * 1.1f); glRotatef(-25.0f * s, 0, 1, 0); drawBox(1.1f, 0.07f, 2.4f); glPopMatrix();
    }
    // vertical fin (red)
    mat(0.75f, 0.1f, 0.1f, 0.6f, 50.0f);
    glPushMatrix(); glTranslatef(-6.3f, 1.0f, 0); glRotatef(20, 0, 0, 1); drawBox(1.4f, 1.8f, 0.1f); glPopMatrix();

    // landing gear (lowered while near the ground)
    if (gear) {
        mat(0.6f, 0.6f, 0.62f, 0.8f, 60.0f);
        glPushMatrix(); glTranslatef(3.0f, -0.65f, 0); cylY(0.05f, 0.35f, 10); glPopMatrix();
        wheel(3.0f, -1.0f, 0.0f, 0.3f, 0.2f);
        for (int s = -1; s <= 1; s += 2) {
            mat(0.6f, 0.6f, 0.62f, 0.8f, 60.0f);
            glPushMatrix(); glTranslatef(-0.4f, -0.65f, s * 1.1f); cylY(0.06f, 0.35f, 10); glPopMatrix();
            wheel(-0.4f, -1.0f, s * 1.1f, 0.32f, 0.28f);
        }
    }

    // navigation lights: red (left) / green (right) / white strobe on the tail
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT);
    glDisable(GL_LIGHTING);
    glColor3f(1, 0, 0);       glPushMatrix(); glTranslatef(-1.15f, -0.2f, -5.5f); sphere(0.12f, 8, 6); glPopMatrix();
    glColor3f(0, 1, 0);       glPushMatrix(); glTranslatef(-1.15f, -0.2f,  5.5f); sphere(0.12f, 8, 6); glPopMatrix();
    float st = (fmodf(tNow, 1.2f) < 0.1f) ? 1.0f : 0.1f;
    glColor3f(st, st, st);    glPushMatrix(); glTranslatef(-6.95f, 1.85f, 0); sphere(0.13f, 8, 6); glPopMatrix();
    glPopAttrib();
}

// Place an airplane in the world using object transformations (T * Ry * Rz * Rx * S)
static void drawAirplaneAt(float x, float z, float yaw, float pitch, float roll, float alt, float scale, bool shadow)
{
    float y = RW + 1.3f * scale + alt;
    if (shadow) {                                            // soft blob shadow on the runway
        glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT);
        glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-3.0f, -10.0f);
        float a = 0.35f / (1.0f + alt * 0.06f);
        glPushMatrix();
        glTranslatef(x, RW + 0.03f, z);
        glRotatef(yaw, 0, 1, 0);
        glColor4f(0, 0, 0, a);
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(0, 0, 0);
        for (int i = 0; i <= 24; i++) { float t = 2 * PI * i / 24; glVertex3f(cosf(t) * 8.0f * scale, 0, sinf(t) * 5.0f * scale); }
        glEnd();
        glPopMatrix();
        glPopAttrib();
    }
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(yaw, 0, 1, 0);
    glRotatef(pitch, 0, 0, 1);
    glRotatef(roll, 1, 0, 0);
    glScalef(scale, scale, scale);
    drawAirplaneModel(alt < 3.0f * scale);
    glPopMatrix();
}

// =============================================================================
//  OUTDOOR ENVIRONMENT
// =============================================================================
static void quadXZ(float x0, float x1, float z0, float z1, float y)
{
    glNormal3f(0, 1, 0);
    glBegin(GL_QUADS);
    glVertex3f(x0, y, z1); glVertex3f(x1, y, z1); glVertex3f(x1, y, z0); glVertex3f(x0, y, z0);
    glEnd();
}

static void lightBall(float x, float y, float z, float r, float g, float b, float rad)
{
    glColor3f(r, g, b);
    glPushMatrix(); glTranslatef(x, y, z); glutSolidSphere(rad, 6, 4); glPopMatrix();
}

static void drawRunwayLights()
{
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT);
    glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D);
    float y = G + 0.35f;
    float k = night ? 1.0f : 0.65f;
    for (float x = -480; x <= 480; x += 30) {                    // runway edge lights
        lightBall(x, y, -115.6f, k, k, 0.9f * k, 0.35f);
        lightBall(x, y, -74.4f,  k, k, 0.9f * k, 0.35f);
    }
    for (float z = -113; z <= -77; z += 4) {                     // threshold: green (west), red (east)
        lightBall(-500, y, z, 0.1f, 1.0f, 0.2f, 0.35f);
        lightBall( 500, y, z, 1.0f, 0.1f, 0.1f, 0.35f);
    }
    int active = (int)(tNow * 14.0f) % 16;                       // sequenced "rabbit" approach lights
    for (int i = 0; i < 15; i++) {
        bool on = (14 - i) == active;
        float v = on ? 1.0f : 0.25f;
        lightBall(-512.0f - i * 30.0f, y + 0.4f, -95.0f, v, v, v, on ? 0.9f : 0.5f);
    }
    for (float x = -300; x <= 300; x += 25) {                    // blue taxiway edge lights
        lightBall(x, y - 0.1f, -62.6f, 0.1f, 0.2f, 1.0f, 0.25f);
        lightBall(x, y - 0.1f, -49.4f, 0.1f, 0.2f, 1.0f, 0.25f);
    }
    glPopAttrib();
}

static void drawOutdoor()
{
    // grass
    mat(0.55f, 0.8f, 0.5f, 0.05f, 8.0f);
    useTex(texGrass);
    planeXZ(-1800, 1800, -1800, 400, G, 12.0f);
    // runway (asphalt), parallel taxiway, connectors
    mat(0.85f, 0.85f, 0.85f, 0.18f, 12.0f);
    useTex(texAsphalt);
    planeXZ(-500, 500, -115, -75, RW, 8.0f, 20.0f);
    planeXZ(-300, 300, -62, -50, RW, 8.0f, 20.0f);
    planeXZ(-156, -144, -75, -62, RW, 8.0f);
    planeXZ(94, 106, -75, -62, RW, 8.0f);
    // apron (concrete)
    mat(0.9f, 0.9f, 0.9f, 0.15f, 12.0f);
    useTex(texConcrete);
    planeXZ(-70, 70, -50, -20, RW, 6.0f, 10.0f);
    noTex();

    // ---- painted markings (decals) ----
    glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-2.0f, -8.0f);
    mat(0.95f, 0.95f, 0.95f, 0.1f, 8.0f);
    float ym = RW + 0.02f;
    for (float x = -450; x < 450; x += 40) quadXZ(x, x + 20, -95.6f, -94.4f, ym);           // centre dashes
    quadXZ(-500, 500, -113.8f, -113.2f, ym); quadXZ(-500, 500, -76.8f, -76.2f, ym);          // edge lines
    for (int s = -1; s <= 1; s += 2)
        for (float z = -111; z < -79; z += 4) {                                              // threshold bars
            if (s < 0) quadXZ(-490, -472, z, z + 2, ym); else quadXZ(472, 490, z, z + 2, ym);
        }
    for (int i = 0; i < 3; i++) for (int s = -1; s <= 1; s += 2) {                           // touchdown zone marks
        float x = s * (150.0f + i * 40.0f);
        quadXZ(x - 10, x + 10, -104, -102, ym); quadXZ(x - 10, x + 10, -88, -86, ym);
    }
    mat(0.95f, 0.8f, 0.1f, 0.1f, 8.0f);
    quadXZ(-300, 300, -56.2f, -55.8f, ym);                                                   // taxiway centre line
    quadXZ(-150.2f, -149.8f, -75, -62, ym); quadXZ(99.8f, 100.2f, -75, -62, ym);
    quadXZ(-156, -144, -74.2f, -73.6f, ym); quadXZ(94, 106, -74.2f, -73.6f, ym);             // hold-short bars
    for (int i = 0; i < 2; i++) quadXZ(-30 + i * 60 - 0.2f, -30 + i * 60 + 0.2f, -50, -33, ym); // apron parking lines
    glDisable(GL_POLYGON_OFFSET_FILL);

    // ---- airport buildings ----
    mat(0.85f, 0.85f, 0.85f, 0.2f, 16.0f);
    useTex(texConcrete);
    glPushMatrix(); glTranslatef(120, G + 7, -28);  drawBox(100, 14, 24, 6.0f); glPopMatrix();   // terminal
    glPushMatrix(); glTranslatef(-120, G + 6, -30); drawBox(40, 12, 30, 6.0f);  glPopMatrix();   // hangar
    glPushMatrix(); glTranslatef(-120, G + 12.6f, -30); drawBox(42, 1.2f, 32, 6.0f); glPopMatrix();
    noTex();
    mat(0.1f, 0.2f, 0.32f, 0.9f, 90.0f);                                                          // terminal glass band
    glPushMatrix(); glTranslatef(120, G + 6, -15.85f); drawBox(96, 4, 0.3f); glPopMatrix();
    mat(0.15f, 0.15f, 0.18f, 0.5f, 30.0f);                                                        // hangar door
    glPushMatrix(); glTranslatef(-120, G + 4.5f, -14.9f); drawBox(24, 9, 0.3f); glPopMatrix();

    // ---- radar mast outside the window (second rotating antenna) ----
    mat(0.7f, 0.7f, 0.72f, 0.5f, 30.0f);
    useTex(texConcrete);
    glPushMatrix(); glTranslatef(12, G + 0.4f, -20); drawBox(4, 0.8f, 4, 2.0f); glPopMatrix();
    noTex();
    mat(0.75f, 0.75f, 0.78f, 0.6f, 40.0f);
    glPushMatrix(); glTranslatef(12, G + 0.8f, -20); cylY(0.6f, 6.2f); glPopMatrix();
    glPushMatrix(); glTranslatef(12, -3.0f, -20); drawRadarAntenna(1.6f); glPopMatrix();

    // ---- parked planes on the apron ----
    drawAirplaneAt(-30, -36, 90, 0, 0, 0, 1.6f, true);
    drawAirplaneAt( 32, -36, 100, 0, 0, 0, 1.6f, true);
}

static void drawTowerExterior()
{
    mat(0.85f, 0.85f, 0.85f, 0.2f, 16.0f);
    useTex(texConcrete);
    glPushMatrix(); glTranslatef(0, G + 0.3f, 0);  drawBox(11, 0.6f, 11, 3.0f, 2.0f); glPopMatrix();   // plinth
    glPushMatrix(); glTranslatef(0, -0.22f, 0);    drawBox(17.6f, 0.4f, 13.6f, 3.0f, 2.0f); glPopMatrix(); // floor slab / balcony
    glPushMatrix(); glTranslatef(0, 4.15f, 0);     drawBox(18.4f, 0.3f, 14.4f, 3.0f, 2.0f); glPopMatrix(); // roof slab
    mat(0.85f, 0.8f, 0.78f, 0.15f, 12.0f);
    useTex(texBrick);
    // FLOOR FIX: the shaft used to end at exactly y = 0.0, the same plane as the cab floor tiles (Z-fighting ->
    // jagged red brick patches).  It now stops at y = -0.3, hidden inside the floor slab (slab spans -0.42..-0.02).
    glPushMatrix(); glTranslatef(0, -4.95f, 0);    drawBox(8, 9.3f, 8, 2.0f, 2.0f); glPopMatrix();      // brick tower shaft
    noTex();
    glPushMatrix(); glTranslatef(-4, 4.3f, 1); drawRadarAntenna(0.7f); glPopMatrix();                  // roof radar
}

// Night sky dome + stars (drawn first, centred on the camera)
static void drawSky()
{
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT | GL_POINT_BIT);
    glDisable(GL_LIGHTING); glDisable(GL_FOG); glDisable(GL_TEXTURE_2D);
    glDepthMask(GL_FALSE);
    glPushMatrix();
    glTranslatef(cam.x, cam.y, cam.z);
    float hor[3] = { night ? 0.06f : 0.78f, night ? 0.08f : 0.86f, night ? 0.16f : 0.96f };
    float zen[3] = { night ? 0.01f : 0.20f, night ? 0.02f : 0.42f, night ? 0.06f : 0.85f };
    const float R = 2000.0f;
    const int stacks = 14, slices = 36;
    for (int i = 0; i < stacks; i++) {
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= slices; j++) {
            float az = 2 * PI * j / slices;
            for (int k = 0; k < 2; k++) {
                float el = -15.0f + (i + k) * (105.0f / stacks);
                float t = powf(std::max(0.0f, el) / 90.0f, 0.6f);
                glColor3f(hor[0] + (zen[0] - hor[0]) * t, hor[1] + (zen[1] - hor[1]) * t, hor[2] + (zen[2] - hor[2]) * t);
                float e = el * DEG;
                glVertex3f(R * cosf(e) * sinf(az), R * sinf(e), -R * cosf(e) * cosf(az));
            }
        }
        glEnd();
    }
    if (night) {
        glPointSize(2.0f);
        glColor3f(1, 1, 1);
        glBegin(GL_POINTS);
        for (size_t i = 0; i + 2 < starDirs.size(); i += 3) glVertex3f(starDirs[i] * 1900, starDirs[i + 1] * 1900, starDirs[i + 2] * 1900);
        glEnd();
    }
    glPopMatrix();
    glPopAttrib();
}

// =============================================================================
//  LIGHTING  (GL_LIGHT0 = sun/moon directional, GL_LIGHT1 = ceiling point light)
// =============================================================================
static void setupLights()
{
    GLfloat gAmb[4] = { night ? 0.10f : 0.22f, night ? 0.10f : 0.22f, night ? 0.14f : 0.25f, 1 };
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, gAmb);

    GLfloat a0[4] = { night ? 0.06f : 0.30f, night ? 0.07f : 0.30f, night ? 0.12f : 0.32f, 1 };
    GLfloat d0[4] = { night ? 0.22f : 0.95f, night ? 0.26f : 0.92f, night ? 0.42f : 0.85f, 1 };
    GLfloat s0[4] = { night ? 0.30f : 1.00f, night ? 0.30f : 1.00f, night ? 0.40f : 0.95f, 1 };
    GLfloat p0[4] = { 0.45f, 0.80f, 0.50f, 0.0f };           // w = 0 -> directional (sun / moon)
    glLightfv(GL_LIGHT0, GL_AMBIENT, a0);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, d0);
    glLightfv(GL_LIGHT0, GL_SPECULAR, s0);
    glLightfv(GL_LIGHT0, GL_POSITION, p0);

    GLfloat a1[4] = { 0.05f, 0.05f, 0.04f, 1 };
    GLfloat d1[4] = { 1.0f, 0.92f, 0.75f, 1 };
    GLfloat s1[4] = { 1.0f, 1.0f, 0.9f, 1 };
    GLfloat p1[4] = { 0.0f, 3.7f, 0.5f, 1.0f };              // w = 1 -> positional point light in the cab
    glLightfv(GL_LIGHT1, GL_AMBIENT, a1);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, d1);
    glLightfv(GL_LIGHT1, GL_SPECULAR, s1);
    glLightfv(GL_LIGHT1, GL_POSITION, p1);
    glLightf(GL_LIGHT1, GL_CONSTANT_ATTENUATION, 1.0f);
    glLightf(GL_LIGHT1, GL_LINEAR_ATTENUATION, 0.06f);
    glLightf(GL_LIGHT1, GL_QUADRATIC_ATTENUATION, 0.012f);

    if (lightSun) glEnable(GL_LIGHT0); else glDisable(GL_LIGHT0);
    if (lightLamp) glEnable(GL_LIGHT1); else glDisable(GL_LIGHT1);
}

// =============================================================================
//  DISPLAY
// =============================================================================
static void applyProjection()
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(fov, (double)winW / (double)std::max(1, winH), 0.25, 3000.0);   // 3D perspective projection
    glMatrixMode(GL_MODELVIEW);
}

static void display()
{
    float sky[3] = { night ? 0.06f : 0.78f, night ? 0.08f : 0.86f, night ? 0.16f : 0.96f };
    glClearColor(sky[0], sky[1], sky[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    float yr = cam.yaw * DEG, pr = cam.pitch * DEG;
    float fx = sinf(yr) * cosf(pr), fy = sinf(pr), fz = -cosf(yr) * cosf(pr);
    gluLookAt(cam.x, cam.y, cam.z, cam.x + fx, cam.y + fy, cam.z + fz, 0, 1, 0);   // viewing transformation

    drawSky();

    GLfloat fogc[4] = { sky[0], sky[1], sky[2], 1 };
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogfv(GL_FOG_COLOR, fogc);
    glFogf(GL_FOG_START, 250.0f);
    glFogf(GL_FOG_END, 1500.0f);

    setupLights();                      // lights are placed AFTER the view transform (world-fixed)
    glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);

    drawOutdoor();
    drawTowerExterior();
    drawRoom();
    drawFurniture();
    drawHuman();
    drawRunwayLights();
    drawAirplaneAt(plane.x, plane.z, plane.yaw, plane.pitch, plane.roll, plane.alt, plane.scale, true);

    // transparent glass last
    glPushMatrix(); glTranslatef(0, 0, -6);                          drawWindowGlass(-8, 8);  glPopMatrix();
    glPushMatrix(); glTranslatef(-8, 0, 0); glRotatef(90, 0, 1, 0);  drawWindowGlass(-2, 6);  glPopMatrix();
    glPushMatrix(); glTranslatef(8, 0, 0);  glRotatef(-90, 0, 1, 0); drawWindowGlass(-6, 2);  glPopMatrix();

    glutSwapBuffers();
}

static void reshape(int w, int h)
{
    winW = w; winH = std::max(1, h);
    glViewport(0, 0, winW, winH);
    applyProjection();
}

// =============================================================================
//  UPDATE / ANIMATION  (called from glutTimerFunc every ~16 ms)
// =============================================================================
static void resetPlane()
{
    plane.x = -260; plane.z = -95; plane.yaw = 0; plane.pitch = 0; plane.roll = 0; plane.alt = 0; plane.speed = 3;
}

static void setView(int v)
{
    viewPreset = v % 3;
    switch (viewPreset) {
    case 0: cam.x = 0;   cam.y = 1.75f; cam.z = 1;    cam.yaw = 0;   cam.pitch = -4;   freeCam = false; break; // inside cab
    case 1: cam.x = 40;  cam.y = 6;     cam.z = 45;   cam.yaw = -36; cam.pitch = -7;   freeCam = true;  break; // outside overview
    case 2: cam.x = -60; cam.y = -6;    cam.z = -68;  cam.yaw = 90;  cam.pitch = 4;    freeCam = true;  break; // runway side
    }
}

static bool chairWorldInfo(int idx, float& cx, float& cz, float& yaw)
{
    if(idx==0) { cx=-5.9f-CHAIR_ROLL_IN; cz=3.8f; yaw=-90.0f; return true; }
    if(idx==1) { cx= 5.9f+CHAIR_ROLL_IN; cz=3.8f; yaw= 90.0f; return true; }
    return false;
}

static int nearestSideChair(float maxDist)
{
    int best=-1; float bd=maxDist*maxDist;
    for(int i=0;i<2;i++) {
        float cx,cz,yaw; chairWorldInfo(i,cx,cz,yaw);
        float dx=human.x-cx,dz=human.z-cz,d=dx*dx+dz*dz;
        if(d<bd) { bd=d; best=i; }
    }
    return best;
}

static void startSitting(int idx)
{
    float cx,cz,yaw; if(!chairWorldInfo(idx,cx,cz,yaw)) return;
    activeChair=idx; humanAction=HA_SITTING; sitBlend=1.0f; drinkClock=0.0f;
    human.vx=human.vz=0.0f; human.x=cx; human.z=cz; human.yaw=yaw;
}

static void startDrinking()
{
    if(humanAction==HA_SITTING) { humanAction=HA_DRINKING; drinkClock=0.0f; }
}

static void leaveChair()
{
    if(activeChair<0) return;
    float cx,cz,yaw; chairWorldInfo(activeChair,cx,cz,yaw);

    // Step clearly out of the chair footprint before enabling walking.
    // The old 0.75 m offset could leave the human inside the chair's
    // collision box, which made the walking cycle appear frozen.
    float rad=(yaw+180.0f)*DEG;
    const float offsets[] = {1.45f, 1.70f, 2.00f, 2.30f};
    bool placed=false;
    for(float d : offsets) {
        float nx=cx+sinf(rad)*d;
        float nz=cz+cosf(rad)*d;
        if(!humanBlocked(nx,nz)) {
            human.x=nx; human.z=nz; placed=true; break;
        }
    }
    if(!placed) {
        // Safe fallback inside the cab.
        human.x = std::max(-7.0f,std::min(7.0f,cx+sinf(rad)*2.0f));
        human.z = std::max(-5.0f,std::min(5.0f,cz+cosf(rad)*2.0f));
    }

    humanAction=HA_FREE;
    activeChair=-1;
    sitBlend=0.0f;
    drinkClock=0.0f;
    human.vx=human.vz=0.0f;
    human.amt=0.0f;
}

static void update(float dt)
{
    // ---- continuous animation ----
    radarAngle = fmodf(radarAngle + 90.0f * dt, 360.0f);          // radar dish rotates every frame

    // ---- camera (FPS style) ----
    float spd = (freeCam ? 25.0f : 3.5f) * dt;
    float sy = sinf(cam.yaw * DEG), cy = cosf(cam.yaw * DEG);
    if (keyState['w']) { cam.x += sy * spd; cam.z -= cy * spd; }
    if (keyState['s']) { cam.x -= sy * spd; cam.z += cy * spd; }
    if (keyState['d']) { cam.x += cy * spd; cam.z += sy * spd; }
    if (keyState['a']) { cam.x -= cy * spd; cam.z -= sy * spd; }
    if (keyState['e']) cam.y += spd * 0.6f;
    if (keyState['q']) cam.y -= spd * 0.6f;
    // camera look moved off the arrow keys (they now drive the human): Z/X = look left/right, T/B = look up/down
    if (keyState['z']) cam.yaw   -= 70.0f * dt;
    if (keyState['x']) cam.yaw   += 70.0f * dt;
    if (keyState['t']) cam.pitch += 45.0f * dt;
    if (keyState['b']) cam.pitch -= 45.0f * dt;
    cam.pitch = std::max(-89.0f, std::min(89.0f, cam.pitch));
    if (!freeCam) {                                                // stay inside the cab
        cam.x = std::max(-7.4f, std::min(7.4f, cam.x));
        cam.z = std::max(-5.4f, std::min(5.5f, cam.z));
        cam.y = std::max(0.6f, std::min(3.5f, cam.y));
    }

    // ---- human interaction / movement ----
    if (humanAction == HA_FREE) {
        float fwd = 0.0f, side = 0.0f;
        if (specState[GLUT_KEY_UP])    fwd  += 1.0f;
        if (specState[GLUT_KEY_DOWN])  fwd  -= 1.0f;
        if (specState[GLUT_KEY_RIGHT]) side += 1.0f;
        if (specState[GLUT_KEY_LEFT])  side -= 1.0f;
        float dx = sy * fwd + cy * side;
        float dz = -cy * fwd + sy * side;
        float len = sqrtf(dx * dx + dz * dz);
        bool input = len > 0.001f;
        if (input) { dx /= len; dz /= len; }
        float k = std::min(1.0f, 10.0f * dt);
        human.vx += ((input ? dx * HUMAN_SPEED : 0.0f) - human.vx) * k;
        human.vz += ((input ? dz * HUMAN_SPEED : 0.0f) - human.vz) * k;
        float nx = human.x + human.vx * dt, nz = human.z + human.vz * dt;
        if (!humanBlocked(nx, human.z)) human.x = nx; else human.vx = 0.0f;
        if (!humanBlocked(human.x, nz)) human.z = nz; else human.vz = 0.0f;
        if (input) {
            float target = atan2f(dx, dz) / DEG;
            float diff = fmodf(target - human.yaw + 540.0f, 360.0f) - 180.0f;
            float step = 600.0f * dt;
            human.yaw += std::max(-step, std::min(step, diff));
            human.yaw = fmodf(human.yaw + 360.0f, 360.0f);
        }
        float speedNow = sqrtf(human.vx * human.vx + human.vz * human.vz);

        // Drive the visible walk cycle from keyboard intent as well as actual
        // velocity.  Immediately after leaving a chair the velocity starts at
        // zero, so using velocity alone made the character look frozen for a
        // few frames and could leave the impression that he was only sliding.
        float targetAmt = input ? 1.0f : 0.0f;
        float animK = std::min(1.0f, 14.0f * dt);
        human.amt += (targetAmt - human.amt) * animK;
        if (!input && speedNow > 0.02f) human.amt = std::max(human.amt, std::min(1.0f, speedNow / HUMAN_SPEED));

        // Keep the existing walk cycle running only while an arrow key is held.
        // It alternates both legs and arms inside drawHumanModel().
        if (input) {
            human.phase = fmodf(human.phase + (2.0f * PI * (0.55f + 0.45f * human.amt) / 0.85f) * dt, 2.0f * PI);
        }
    } else if (humanAction == HA_SITTING) {
        float cx,cz,yaw; chairWorldInfo(activeChair,cx,cz,yaw);
        sitBlend=std::min(1.0f,sitBlend+dt/0.55f);
    } else if (humanAction == HA_DRINKING) {
        drinkClock += dt;
        if (drinkClock >= 8.6f) { humanAction=HA_SITTING; drinkClock=0.0f; }
    }

    // ---- airplane: interactive object transformations ----
    float mv = 35.0f * dt, rot = 60.0f * dt;
    bool manual = false;
    float hx = cosf(plane.yaw * DEG), hz = -sinf(plane.yaw * DEG);   // heading vector
    if (keyState['i']) { plane.x += hx * mv; plane.z += hz * mv; manual = true; }      // TRANSLATE forward
    if (keyState['k']) { plane.x -= hx * mv; plane.z -= hz * mv; manual = true; }      // TRANSLATE backward
    if (keyState['j']) { plane.yaw += rot; manual = true; }                              // ROTATE (yaw) left
    if (keyState['l']) { plane.yaw -= rot; manual = true; }                              // ROTATE (yaw) right
    if (keyState['u']) { plane.pitch = std::min(45.0f, plane.pitch + rot * 0.5f); manual = true; }  // nose up
    if (keyState['o']) { plane.pitch = std::max(-45.0f, plane.pitch - rot * 0.5f); manual = true; } // nose down
    if (keyState['f']) { plane.roll += rot; manual = true; }                             // roll left
    if (keyState['g']) { plane.roll -= rot; manual = true; }                             // roll right
    if (keyState['y']) { plane.alt += 12.0f * dt; manual = true; }                       // TRANSLATE up
    if (keyState['h']) { plane.alt = std::max(0.0f, plane.alt - 12.0f * dt); manual = true; } // down
    if (keyState['+'] || keyState['=']) { plane.scale = std::min(4.0f, plane.scale * (1.0f + 0.8f * dt)); manual = true; } // SCALE up
    if (keyState['-'] || keyState['_']) { plane.scale = std::max(0.4f, plane.scale * (1.0f - 0.8f * dt)); manual = true; } // SCALE down
    if (manual) autoMode = false;

    if (autoMode) {                                                // automatic take-off run
        plane.yaw = 0; plane.roll = 0; plane.z = -95;
        if (plane.speed < 85) plane.speed += 6.0f * dt;
        plane.x += plane.speed * dt;
        if (plane.speed >= 45) {                                   // rotate & climb
            plane.pitch = std::min(plane.pitch + 12.0f * dt, 14.0f);
            plane.alt += plane.speed * dt * sinf(plane.pitch * DEG);
        }
        if (plane.x > 520) resetPlane();
    }
}

static int frames = 0, lastFpsMs = 0, lastMs = 0;
static void timerFunc(int)
{
    int now = glutGet(GLUT_ELAPSED_TIME);
    float dt = (now - lastMs) / 1000.0f;
    lastMs = now;
    if (dt > 0.1f) dt = 0.1f;
    tNow = now / 1000.0f;
    update(dt);

    frames++;
    if (now - lastFpsMs > 500) {                                   // status shown in the title bar (no 2-D overlay needed)
        char t[256];
        const char* hs = humanAction==HA_FREE ? "WALK" : (humanAction==HA_SITTING ? "SITTING" : "DRINKING");
        snprintf(t, sizeof t, "3D ATC Tower | %s | %s | Human:%s | View %d%s | Plane x=%.0f alt=%.0f scale=%.1f %s | %d FPS",
                 night ? "NIGHT" : "DAY", freeCam ? "FREE-CAM" : "CAB", hs, viewPreset, wireframe ? " WIRE" : "",
                 plane.x, plane.alt, plane.scale, autoMode ? "[AUTO]" : "[MANUAL]", (int)(frames * 1000.0f / (now - lastFpsMs)));
        glutSetWindowTitle(t);
        frames = 0; lastFpsMs = now;
    }
    glutPostRedisplay();
    glutTimerFunc(16, timerFunc, 0);
}

// =============================================================================
//  INPUT
// =============================================================================
static void keyDown(unsigned char k, int, int)
{
    if (k == 27) exit(0);
    unsigned char c = (unsigned char)tolower(k);
    if (c == 't') {
        if (humanAction == HA_DRINKING) return;
        if (humanAction == HA_SITTING) { startDrinking(); return; }
        int chair = nearestSideChair(1.7f);
        if (chair >= 0) { startSitting(chair); return; }
    }
    keyState[c] = true;
    switch (c) {
    case '1': lightSun = !lightSun; break;
    case '2': lightLamp = !lightLamp; break;
    case 'n': night = !night; break;
    case 'v': setView(viewPreset + 1); break;
    case 'c': freeCam = !freeCam; break;
    case 'm': wireframe = !wireframe; break;
    case 'r': resetPlane(); break;
    case ' ': autoMode = !autoMode; if (autoMode) resetPlane(); break;
    }
}
static void keyUp(unsigned char k, int, int)          { keyState[(unsigned char)tolower(k)] = false; }
static void specialDown(int k, int, int)              { if (k < 256) { if ((k==GLUT_KEY_UP||k==GLUT_KEY_DOWN||k==GLUT_KEY_LEFT||k==GLUT_KEY_RIGHT) && humanAction!=HA_FREE) leaveChair(); specState[k] = true; } }
static void specialUp(int k, int, int)                { if (k < 256) specState[k] = false; }

static bool dragging = false;
static int lastX = 0, lastY = 0;
static void mouseButton(int b, int s, int x, int y)
{
    if (b == GLUT_LEFT_BUTTON) { dragging = (s == GLUT_DOWN); lastX = x; lastY = y; }
    else if (s == GLUT_DOWN && b == 3) { fov = std::max(25.0f, fov - 3.0f); applyProjection(); }   // wheel up: zoom in
    else if (s == GLUT_DOWN && b == 4) { fov = std::min(90.0f, fov + 3.0f); applyProjection(); }   // wheel down: zoom out
}
static void mouseMotion(int x, int y)
{
    if (!dragging) return;
    cam.yaw   += (x - lastX) * 0.15f;
    cam.pitch -= (y - lastY) * 0.15f;
    cam.pitch = std::max(-89.0f, std::min(89.0f, cam.pitch));
    lastX = x; lastY = y;
}

// =============================================================================
//  INITIALISATION
// =============================================================================
static void initGL()
{
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHT1);
    glEnable(GL_NORMALIZE);                                        // needed because of glScalef on lit objects
    glShadeModel(GL_SMOOTH);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
    glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);
    glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SEPARATE_SPECULAR_COLOR);   // specular highlights over textures
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    quad = gluNewQuadric();
    gluQuadricNormals(quad, GLU_SMOOTH);
    gluQuadricTexture(quad, GL_TRUE);

    texBrick    = makeTex("textures/brick.bmp",    genBrick);
    texAsphalt  = makeTex("textures/asphalt.bmp",  genAsphalt);
    texMetal    = makeTex("textures/metal.bmp",    genMetal);
    texConcrete = makeTex("textures/concrete.bmp", genConcrete);
    texTile     = makeTex("textures/floor.bmp",    genTile);
    texGrass    = makeTex("textures/grass.bmp",    genGrass);

    srand(7);
    for (int i = 0; i < 500; i++) {                                // random stars above the horizon
        float az = 2 * PI * (rand() / (float)RAND_MAX);
        float el = (5.0f + 85.0f * (rand() / (float)RAND_MAX)) * DEG;
        starDirs.push_back(cosf(el) * sinf(az));
        starDirs.push_back(sinf(el));
        starDirs.push_back(-cosf(el) * cosf(az));
    }
    setView(0);
}

static void printHelp()
{
    printf("=========== 3D ATC TOWER - CONTROLS ===========\n");
    printf(" HUMAN  : Arrow keys move | T near chair = sit | T while sitting = drink tea\n");
    printf(" CAMERA : W/A/S/D move, Q/E down/up, Z/X look left/right, B look down (T remains interaction-aware)\n");
    printf("          left-mouse drag look, mouse wheel zoom\n");
    printf("          V cycle views (cab / outside / runway), C toggle free-cam\n");
    printf(" AIRPLANE (object transformations):\n");
    printf("   TRANSLATE  I/K forward/back   Y/H up/down\n");
    printf("   ROTATE     J/L yaw   U/O pitch   F/G roll\n");
    printf("   SCALE      + / -\n");
    printf("   SPACE auto take-off on/off   R reset plane\n");
    printf(" LIGHTS : 1 sun/moon (LIGHT0)   2 tower lamp (LIGHT1)   N day/night\n");
    printf(" M wireframe   ESC quit\n");
    printf("===============================================\n");
}

int main(int argc, char** argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowSize(winW, winH);
    glutCreateWindow("3D ATC Tower");
    initGL();
    printHelp();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyDown);
    glutKeyboardUpFunc(keyUp);
    glutSpecialFunc(specialDown);
    glutSpecialUpFunc(specialUp);
    glutMouseFunc(mouseButton);
    glutMotionFunc(mouseMotion);
    glutIgnoreKeyRepeat(1);

    lastMs = lastFpsMs = glutGet(GLUT_ELAPSED_TIME);
    glutTimerFunc(16, timerFunc, 0);
    glutMainLoop();
    return 0;
}
