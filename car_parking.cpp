// parking_3d.cpp
// Full-featured 3-car parking simulator (portable; uses glRasterPos2f for center text)

#include <GL/glut.h>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>

////////////////////////////////////////////////////////////////////////////////
// --- Config / Globals -------------------------------------------------------
////////////////////////////////////////////////////////////////////////////////
struct AABB
{
    float cx, cy, cz, hx, hy, hz;
};

struct Car
{
    float x, y, z; // position (center)
    float angle;   // yaw degrees
    float speed;
    float scale; // uniform scale
    bool parked;
    int parkedSpot;   // -1 if none
    float brakeLight; // 0..1
    float flashTimer; // collision flash seconds
    std::string name;
};

std::vector<AABB> parkingAreas;
std::vector<AABB> buildings;
std::vector<AABB> bollards;
std::vector<AABB> parkedCars;
std::vector<Car> cars;

int activeCar = 0;
bool pendingUnpark = false;

// camera
float camYaw = 180.0f, camPitch = 10.0f, camDist = 15.0f;
float camTargetX = 0.0f, camTargetY = 0.9f, camTargetZ = 0.0f;
bool leftDrag = false, middleDrag = false;
int lastMouseX = 0, lastMouseY = 0;

// animated clouds offset (screen-space)
float cloudOffset = 0.0f;
// cloud animation/appearance parameters (user-adjustable)
float cloudSpeed = 0.28f; // advance per idle tick
float cloudScale = 1.0f;  // multiplier for cloud sizes

// controls
bool upPressed = false, downPressed = false, leftPressed = false, rightPressed = false;

// message HUD
std::string parkingMessage = "";
float messageAlpha = 0.0f;
int messageTimer = 0;
const int MSG_MAX = 300;

// physics params
const float BASE_MAX_SPEED = 0.05f;
float maxSpeed = BASE_MAX_SPEED;
const float ACCEL_RATE = 0.03f, FRICTION = 0.005f;

const float MAX_STEERING = 30.0f;
const float BRAKE_RISE = 0.06f, BRAKE_FALL = 0.03f, BRAKE_THRESHOLD = 0.03f;

////////////////////////////////////////////////////////////////////////////////
// --- Utilities ---------------------------------------------------------------
////////////////////////////////////////////////////////////////////////////////
float nowSeconds() { return glutGet(GLUT_ELAPSED_TIME) / 1000.0f; }

bool aabbIntersect(const AABB &A, const AABB &B)
{
    return (fabs(A.cx - B.cx) <= (A.hx + B.hx)) &&
           (fabs(A.cy - B.cy) <= (A.hy + B.hy)) &&
           (fabs(A.cz - B.cz) <= (A.hz + B.hz));
}

void setParkingMessage(const std::string &msg, int frames = 180)
{
    parkingMessage = msg;
    messageTimer = frames;
    messageAlpha = 1.0f;
    // ensure HUD updates immediately when message is set
    glutPostRedisplay();
}

////////////////////////////////////////////////////////////////////////////////
// --- Drawing helpers ---------------------------------------------------------
////////////////////////////////////////////////////////////////////////////////
void drawSkyGradient(int w, int h)
{
    // Draw a full-screen vertical gradient (pale blue -> white)
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    // deeper blue at top -> warmer near horizon
    glColor3f(0.38f, 0.64f, 0.94f);
    glVertex2f(0, 0);
    glColor3f(0.70f, 0.90f, 0.98f);
    glVertex2f(w, 0);
    glColor3f(0.95f, 0.97f, 0.99f);
    glVertex2f(w, h);
    glColor3f(0.58f, 0.80f, 0.98f);
    glVertex2f(0, h);
    glEnd();

    // soft, painterly clouds (screen-space ellipses)
    auto drawCloud = [&](float cx, float cy, float sx, float sy, float alpha)
    {
        glColor4f(1.0f, 1.0f, 1.0f, alpha);
        const int STEPS = 28;
        for (int k = 0; k < 3; ++k)
        {
            float ox = ((k - 1) * 0.35f) * sx;
            float oy = (-0.05f + 0.02f * k) * sy;
            glBegin(GL_POLYGON);
            for (int i = 0; i < STEPS; ++i)
            {
                float a = (float)i / STEPS * 2.0f * 3.1415926535f;
                float x = cx + (ox + cosf(a) * sx * (0.6f + 0.06f * k));
                float y = cy + (oy + sinf(a) * sy * (0.8f - 0.06f * k));
                glVertex2f(x, y);
            }
            glEnd();
        }
    };

    // three cloud groups, positions relative to window size
    // animate clouds slightly by shifting their X positions with cloudOffset
    float cx1 = fmodf((float)w * 0.22f + cloudOffset * 0.6f, (float)w);
    float cx2 = fmodf((float)w * 0.60f + cloudOffset * 0.9f, (float)w);
    float cx3 = fmodf((float)w * 0.82f + cloudOffset * 0.4f, (float)w);
    // draw using user-configurable size multiplier
    drawCloud(cx1, (float)h * 0.74f, (float)w * 0.14f * cloudScale, (float)h * 0.06f * cloudScale, 0.72f);
    drawCloud(cx2, (float)h * 0.80f, (float)w * 0.18f * cloudScale, (float)h * 0.07f * cloudScale, 0.68f);
    drawCloud(cx3, (float)h * 0.68f, (float)w * 0.12f * cloudScale, (float)h * 0.05f * cloudScale, 0.60f);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

void drawBuilding(float x, float z, float w, float d, float h)
{
    // base wall color varies slightly by side for variety
    if (x < 0)
        glColor3f(0.80f, 0.78f, 0.75f);
    else
        glColor3f(0.86f, 0.88f, 0.92f);
    glPushMatrix();
    glTranslatef(x, h / 2.0f, z);
    glScalef(w, h, d);
    glutSolidCube(1.0f);

    // add glazed windows as slightly transparent panes on front and side faces
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // window grid parameters (in unit-cube coordinate)
    int rows = std::max(2, (int)(h));
    int cols = std::max(2, (int)(w * 1.2f));
    float marginX = 0.08f, marginY = 0.08f;
    float paneW = (1.0f - 2.0f * marginX) / cols;
    float paneH = (1.0f - 2.0f * marginY) / rows;

    // front face (+Z in model space)
    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < cols; ++c)
        {
            float lx = -0.5f + marginX + c * paneW;
            float ly = -0.5f + marginY + r * paneH;
            float rx = lx + paneW * 0.86f;
            float ry = ly + paneH * 0.86f;
            glColor4f(0.12f, 0.22f, 0.32f, 0.46f); // glass tint
            glBegin(GL_QUADS);
            glVertex3f(lx, ry, 0.501f);
            glVertex3f(rx, ry, 0.501f);
            glVertex3f(rx, ly, 0.501f);
            glVertex3f(lx, ly, 0.501f);
            glEnd();
            // thin frame
            glColor4f(0.06f, 0.06f, 0.06f, 1.0f);
            glBegin(GL_LINE_LOOP);
            glVertex3f(lx, ry, 0.502f);
            glVertex3f(rx, ry, 0.502f);
            glVertex3f(rx, ly, 0.502f);
            glVertex3f(lx, ly, 0.502f);
            glEnd();
        }
    }

    // back face (-Z in model space) - mirror of front
    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < cols; ++c)
        {
            float lx = -0.5f + marginX + c * paneW;
            float ly = -0.5f + marginY + r * paneH;
            float rx = lx + paneW * 0.86f;
            float ry = ly + paneH * 0.86f;
            glColor4f(0.12f, 0.22f, 0.32f, 0.46f); // glass tint
            glBegin(GL_QUADS);
            glVertex3f(lx, ry, -0.501f);
            glVertex3f(rx, ry, -0.501f);
            glVertex3f(rx, ly, -0.501f);
            glVertex3f(lx, ly, -0.501f);
            glEnd();
            // thin frame
            glColor4f(0.06f, 0.06f, 0.06f, 1.0f);
            glBegin(GL_LINE_LOOP);
            glVertex3f(lx, ry, -0.502f);
            glVertex3f(rx, ry, -0.502f);
            glVertex3f(rx, ly, -0.502f);
            glVertex3f(lx, ly, -0.502f);
            glEnd();
        }
    }

    // side face (+X in model space) - slightly fewer columns
    int srows = std::max(2, (int)(h));
    int scols = std::max(1, (int)(d * 1.0f));
    float smarginY = 0.08f, smarginZ = 0.08f;
    float spaneH = (1.0f - 2.0f * smarginY) / srows;
    float spaneW = (1.0f - 2.0f * smarginZ) / scols;
    for (int r = 0; r < srows; ++r)
    {
        for (int c = 0; c < scols; ++c)
        {
            float lz = -0.5f + smarginZ + c * spaneW;
            float ly = -0.5f + smarginY + r * spaneH;
            float rz = lz + spaneW * 0.86f;
            float ry = ly + spaneH * 0.86f;
            glColor4f(0.12f, 0.22f, 0.32f, 0.42f);
            glBegin(GL_QUADS);
            glVertex3f(0.501f, ry, lz);
            glVertex3f(0.501f, ry, rz);
            glVertex3f(0.501f, ly, rz);
            glVertex3f(0.501f, ly, lz);
            glEnd();
            glColor4f(0.06f, 0.06f, 0.06f, 1.0f);
            glBegin(GL_LINE_LOOP);
            glVertex3f(0.502f, ry, lz);
            glVertex3f(0.502f, ry, rz);
            glVertex3f(0.502f, ly, rz);
            glVertex3f(0.502f, ly, lz);
            glEnd();
        }
    }

    glDisable(GL_BLEND);
    glPopMatrix();
    // small roof cap
    glColor3f(0.62f, 0.42f, 0.34f);
    glPushMatrix();
    glTranslatef(x, h + 0.3f, z);
    glScalef(w + 0.5f, 0.3f, d + 0.5f);
    glutSolidCube(1.0f);
    glPopMatrix();
}

void drawStreetLight(float x, float z)
{
    // pole
    glColor3f(0.12f, 0.12f, 0.12f);
    glPushMatrix();
    glTranslatef(x, 2.5f, z);
    glScalef(0.12f, 5.0f, 0.12f);
    glutSolidCube(1.0f);
    glPopMatrix();
    // lamp glass
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(1.0f, 0.94f, 0.65f, 0.95f);
    glPushMatrix();
    glTranslatef(x, 5.3f, z);
    glutSolidSphere(0.35f, 14, 14);
    glPopMatrix();
    // soft glow around lamp
    glColor4f(1.0f, 0.92f, 0.6f, 0.18f);
    glPushMatrix();
    glTranslatef(x, 5.3f, z);
    glutSolidSphere(0.9f, 12, 12);
    glPopMatrix();
    glDisable(GL_BLEND);
}

void drawTree(float x, float z)
{
    glColor3f(0.45f, 0.28f, 0.18f);
    glPushMatrix();
    glTranslatef(x, 1.0f, z);
    glScalef(0.25f, 2.0f, 0.25f);
    glutSolidCube(1.0f);
    glPopMatrix();
    // layered foliage for a more natural look
    glColor3f(0.16f, 0.65f, 0.24f);
    glPushMatrix();
    glTranslatef(x, 2.4f, z);
    glutSolidSphere(0.85f, 18, 18);
    glPopMatrix();
    glColor3f(0.10f, 0.52f, 0.18f);
    glPushMatrix();
    glTranslatef(x - 0.5f, 2.2f, z + 0.25f);
    glutSolidSphere(0.55f, 14, 14);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(x + 0.45f, 2.05f, z - 0.2f);
    glutSolidSphere(0.5f, 14, 14);
    glPopMatrix();
}

void drawGround()
{
    // base asphalt (darker, near-black road for realism)
    glColor3f(0.12f, 0.12f, 0.14f);
    glBegin(GL_QUADS);
    glVertex3f(-50, 0, -50);
    glVertex3f(50, 0, -50);
    glVertex3f(50, 0, 50);
    glVertex3f(-50, 0, 50);
    glEnd();

    // driving lane
    glColor3f(0.095f, 0.095f, 0.11f);
    glBegin(GL_QUADS);
    glVertex3f(-5, 0.01f, -50);
    glVertex3f(5, 0.01f, -50);
    glVertex3f(5, 0.01f, 50);
    glVertex3f(-5, 0.01f, 50);
    glEnd();

    // center dashed (yellow) with slightly warm tint
    glColor3f(1.0f, 0.86f, 0.08f);
    for (float z = -45.0f; z <= 45.0f; z += 8.0f)
    {
        glBegin(GL_QUADS);
        glVertex3f(-0.2f, 0.02f, z);
        glVertex3f(0.2f, 0.02f, z);
        glVertex3f(0.2f, 0.02f, z + 4.0f);
        glVertex3f(-0.2f, 0.02f, z + 4.0f);
        glEnd();
    }

    // subtle lane edge highlights (very faint reflective streaks)
    glColor4f(0.95f, 0.95f, 0.98f, 0.06f);
    glBegin(GL_QUADS);
    glVertex3f(-5.1f, 0.021f, -50);
    glVertex3f(-4.9f, 0.021f, -50);
    glVertex3f(-4.9f, 0.021f, 50);
    glVertex3f(-5.1f, 0.021f, 50);
    glVertex3f(4.9f, 0.021f, -50);
    glVertex3f(5.1f, 0.021f, -50);
    glVertex3f(5.1f, 0.021f, 50);
    glVertex3f(4.9f, 0.021f, 50);
    glEnd();

    // parking spots (vivid — like original)
    for (int i = 0; i < (int)parkingAreas.size(); ++i)
    {
        AABB &p = parkingAreas[i];
        if (i % 3 == 0)
            glColor3f(0.30f, 0.85f, 0.40f); // bright green
        else if (i % 3 == 1)
            glColor3f(0.40f, 0.70f, 0.95f); // sky blue
        else
            glColor3f(0.95f, 0.65f, 0.30f); // orange
        glBegin(GL_QUADS);
        glVertex3f(p.cx - p.hx, 0.03f, p.cz - p.hz);
        glVertex3f(p.cx + p.hx, 0.03f, p.cz - p.hz);
        glVertex3f(p.cx + p.hx, 0.03f, p.cz + p.hz);
        glVertex3f(p.cx - p.hx, 0.03f, p.cz + p.hz);
        glEnd();

        // border lines
        glColor3f(1.0f, 1.0f, 1.0f);
        glLineWidth(4.0f);
        glBegin(GL_LINE_LOOP);
        glVertex3f(p.cx - p.hx, 0.04f, p.cz - p.hz);
        glVertex3f(p.cx + p.hx, 0.04f, p.cz - p.hz);
        glVertex3f(p.cx + p.hx, 0.04f, p.cz + p.hz);
        glVertex3f(p.cx - p.hx, 0.04f, p.cz + p.hz);
        glEnd();
        glLineWidth(1.0f);

        // spot label
        glColor3f(1, 1, 1);
        glRasterPos3f(p.cx - 0.8f, 0.05f, p.cz);
        std::string spot = "P" + std::to_string(i + 1);
        for (char c : spot)
            glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, c);
    }
}

////////////////////////////////////////////////////////////////////////////////
// --- Scalable car model (keeps proportions when scale changes) -------------
////////////////////////////////////////////////////////////////////////////////
void drawCarModel(const Car &car, float r, float g, float b)
{
    float s = car.scale;
    auto S = [&](float v) -> float
    { return v * s; };

    float flash = std::max(0.0f, car.flashTimer);
    float flashBlend = std::min(1.0f, flash * 5.0f);
    float br = r + (1.0f - r) * flashBlend;
    float bg = g + (1.0f - g) * flashBlend;
    float bb = b + (1.0f - b) * flashBlend;

    glPushMatrix();
    glTranslatef(car.x, car.y, car.z);
    glRotatef(car.angle, 0, 1, 0);

    // body
    glColor3f(br, bg, bb);
    glPushMatrix();
    glScalef(S(1.3f), S(0.7f), S(2.2f));
    glutSolidCube(1.0f);
    glPopMatrix();

    // roof / cabin
    glColor3f(std::max(0.0f, br - 0.12f), std::max(0.0f, bg - 0.04f), std::max(0.0f, bb - 0.04f));
    glPushMatrix();
    glTranslatef(0, S(0.35f), S(-0.2f));
    glScalef(S(1.1f), S(0.4f), S(1.2f));
    glutSolidCube(1.0f);
    glPopMatrix();

    // windows
    glColor3f(0.12f, 0.22f, 0.26f);
    glPushMatrix();
    glTranslatef(0, S(0.45f), 0);
    glScalef(S(0.72f), S(0.28f), S(0.88f));
    glutSolidCube(1.0f);
    glPopMatrix();

    // headlights
    glColor3f(1.0f, 0.95f, 0.85f);
    glPushMatrix();
    glTranslatef(S(0.5f), S(0.0f), S(1.0f));
    glutSolidSphere(S(0.12f), 10, 10);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(S(-0.5f), S(0.0f), S(1.0f));
    glutSolidSphere(S(0.12f), 10, 10);
    glPopMatrix();

    // bumpers
    glColor3f(0.06f, 0.06f, 0.06f);
    glPushMatrix();
    glTranslatef(0, S(-0.12f), S(1.12f));
    glScalef(S(1.4f), S(0.18f), S(0.14f));
    glutSolidCube(1.0f);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(0, S(-0.12f), S(-1.12f));
    glScalef(S(1.4f), S(0.18f), S(0.14f));
    glutSolidCube(1.0f);
    glPopMatrix();

    // taillights react to brake
    {
        float baseR = 0.9f, baseG = 0.1f, baseB = 0.1f;
        float rcol = baseR * (0.4f + 0.6f * car.brakeLight);
        float gcol = baseG * (0.4f + 0.6f * car.brakeLight);
        float bcol = baseB * (0.4f + 0.6f * car.brakeLight);
        glColor3f(rcol, gcol, bcol);
        glPushMatrix();
        glTranslatef(S(0.5f), S(0.0f), S(-1.0f));
        glutSolidSphere(S(0.12f + 0.02f * car.brakeLight), 8, 8);
        glPopMatrix();
        glPushMatrix();
        glTranslatef(S(-0.5f), S(0.0f), S(-1.0f));
        glutSolidSphere(S(0.12f + 0.02f * car.brakeLight), 8, 8);
        glPopMatrix();
    }

    // doors / handles
    glColor3f(std::max(0.0f, br - 0.12f), std::max(0.0f, bg - 0.04f), std::max(0.0f, bb - 0.04f));
    glPushMatrix();
    glTranslatef(-S(0.52f), S(0.05f), 0);
    glScalef(S(0.09f), S(0.48f), S(1.02f));
    glutSolidCube(1.0f);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(S(0.52f), S(0.05f), 0);
    glScalef(S(0.09f), S(0.48f), S(1.02f));
    glutSolidCube(1.0f);
    glPopMatrix();

    // wheels
    glColor3f(0.12f, 0.12f, 0.12f);
    float wx = S(0.65f), wz = S(0.9f), wr = S(0.25f), wt = S(0.10f);
    for (float px : {-wx, wx})
        for (float pz : {-wz, wz})
        {
            glPushMatrix();
            glTranslatef(px, S(-0.25f), pz);
            glRotatef(90, 0, 1, 0);
            glutSolidTorus(wt, wr, 12, 16);
            glPopMatrix();
        }

    glPopMatrix();
}

////////////////////////////////////////////////////////////////////////////////
// Park light (pulsing neon cyan)
////////////////////////////////////////////////////////////////////////////////
void drawParkLight(const Car &car)
{
    if (!car.parked)
        return;
    float t = nowSeconds();
    float pulse = 0.10f * car.scale * (1.0f + 0.18f * sinf(t * 6.0f));
    glPushMatrix();
    glTranslatef(car.x, car.y + 1.1f * car.scale + pulse * 0.6f, car.z);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.0f, 0.95f, 0.95f, 0.20f);
    glutSolidSphere((0.20f + pulse) * car.scale, 12, 12);
    glColor4f(0.6f, 1.0f, 1.0f, 1.0f);
    glutSolidSphere((0.09f + pulse * 0.6f) * car.scale, 12, 12);
    glDisable(GL_BLEND);
    glPopMatrix();
}

////////////////////////////////////////////////////////////////////////////////
// Static collisions
////////////////////////////////////////////////////////////////////////////////
bool collidesWithStatic(const AABB &box)
{
    for (auto &b : buildings)
        if (aabbIntersect(box, b))
            return true;
    for (auto &b : bollards)
        if (aabbIntersect(box, b))
            return true;
    // world bounds
    if (box.cx - box.hx < -48 || box.cx + box.hx > 48 || box.cz - box.hz < -48 || box.cz + box.hz > 48)
        return true;
    return false;
}

////////////////////////////////////////////////////////////////////////////////
// Movement, collision, parking detection (detailed)
////////////////////////////////////////////////////////////////////////////////
void triggerFlash(Car &c) { c.flashTimer = 0.18f; }

void updateActiveCar(Car &car)
{
    // if parked, remain static (brake light on)
    if (car.parked)
    {
        car.speed = 0.0f;
        car.brakeLight = 1.0f;
        return;
    }

    // intended speed
    float intended = 0.0f;
    if (upPressed && !downPressed)
        intended = maxSpeed;
    else if (downPressed && !upPressed)
        intended = -maxSpeed;
    if (upPressed && downPressed)
        intended = 0.0f;

    // accel/brake smoothing
    if (car.speed < intended)
        car.speed = std::min(intended, car.speed + ACCEL_RATE);
    else if (car.speed > intended)
        car.speed = std::max(intended, car.speed - ACCEL_RATE);

    if (!upPressed && !downPressed)
    {
        if (car.speed > 0)
            car.speed = std::max(0.0f, car.speed - FRICTION);
        else if (car.speed < 0)
            car.speed = std::min(0.0f, car.speed + FRICTION);
    }

    // steering input
    float steeringAngle = 0.0f;
    if (leftPressed && !rightPressed)
        steeringAngle = MAX_STEERING;
    else if (rightPressed && !leftPressed)
        steeringAngle = -MAX_STEERING;

    // propose movement
    float rad = car.angle * (3.1415926535f / 180.0f);
    float dx = sinf(rad) * car.speed;
    float dz = cosf(rad) * car.speed;
    float newX = car.x + dx, newZ = car.z + dz;

    // car box extents (approx)
    float halfX = 1.6f * car.scale, halfY = 0.6f * car.scale, halfZ = 2.0f * car.scale;
    AABB carBox = {newX, car.y, newZ, halfX, halfY, halfZ};

    // check static collisions
    bool collision = collidesWithStatic(carBox);

    // car-car collisions
    if (!collision)
    {
        for (int i = 0; i < (int)cars.size(); ++i)
        {
            if (&cars[i] == &car)
                continue;
            AABB other = {cars[i].x, cars[i].y, cars[i].z, 1.6f * cars[i].scale, 0.6f * cars[i].scale, 2.0f * cars[i].scale};
            if (aabbIntersect(carBox, other))
            {
                collision = true;
                break;
            }
        }
    }

    if (!collision)
    {
        car.x = newX;
        car.z = newZ;
    }
    else
    {
        if (fabs(car.speed) > 0.0001f)
            triggerFlash(car);
        car.speed = 0.0f;
    }

    // apply turning when moving
    if (fabs(car.speed) > 0.001f)
    {
        car.angle += (car.speed / 2.5f) * tanf(steeringAngle * 3.1415926535f / 180.0f) * 180.0f / 3.1415926535f;
    }

    // brake light smoothing
    float targetBrake = (fabs(car.speed) < BRAKE_THRESHOLD) ? 1.0f : 0.0f;
    if (targetBrake > car.brakeLight)
        car.brakeLight = std::min(1.0f, car.brakeLight + BRAKE_RISE);
    else if (targetBrake < car.brakeLight)
        car.brakeLight = std::max(0.0f, car.brakeLight - BRAKE_FALL);

    // Parking detection (detailed)
    car.parked = false;
    car.parkedSpot = -1;
    for (int i = 0; i < (int)parkingAreas.size(); ++i)
    {
        AABB &p = parkingAreas[i];
        bool insideX = (car.x - halfX >= p.cx - p.hx) && (car.x + halfX <= p.cx + p.hx);
        bool insideZ = (car.z - halfZ >= p.cz - p.hz) && (car.z + halfZ <= p.cz + p.hz);
        bool stopped = (fabs(car.speed) < 0.03f);
        if (insideX && insideZ && stopped)
        {
            // check occupancy
            bool occupied = false;
            for (int j = 0; j < (int)cars.size(); ++j)
            {
                if (&cars[j] == &car)
                    continue;
                if (cars[j].parked && cars[j].parkedSpot == i)
                {
                    occupied = true;
                    break;
                }
            }
            if (occupied)
            {
                setParkingMessage("PARKING FAILED! Spot occupied", 180);
                break;
            }
            // ensure margins not touching lines (min 0.05m clearance)
            float minLeft = (car.x - halfX) - (p.cx - p.hx);
            float minRight = (p.cx + p.hx) - (car.x + halfX);
            float minFront = (car.z - halfZ) - (p.cz - p.hz);
            float minBack = (p.cz + p.hz) - (car.z + halfZ);
            float minDist = std::min(std::min(minLeft, minRight), std::min(minFront, minBack));
            if (minDist >= 0.05f)
            {
                // snap car orientation to match parking spot orientation
                if (p.hx > p.hz)
                {
                    // horizontal spot (wider in X) -> face +/- X
                    if (car.x < p.cx)
                        car.angle = 90.0f;
                    else
                        car.angle = -90.0f;
                }
                else
                {
                    // vertical spot (deeper in Z) -> face +/- Z
                    if (car.z < p.cz)
                        car.angle = 0.0f;
                    else
                        car.angle = 180.0f;
                }
                car.speed = 0.0f; // ensure fully stopped when parked
                car.parked = true;
                car.parkedSpot = i;
                setParkingMessage("PARKED SUCCESSFULLY! Spot P" + std::to_string(i + 1), MSG_MAX);
            }
            else
            {
                setParkingMessage("PARKING FAILED! Too close to boundary line", 180);
            }
            break;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// --- HUD & 2D text (uses orthographic + glRasterPos2f for portability) ------
////////////////////////////////////////////////////////////////////////////////
void drawTextShadowed(const std::string &text, float x, float y, void *font, float r, float g, float b, float a = 1.0f)
{
    // shadow
    glColor4f(0.0f, 0.0f, 0.0f, a);
    glRasterPos2f(x + 1.5f, y - 1.5f);
    for (char c : text)
        glutBitmapCharacter(font, c);
    // main
    glColor4f(r, g, b, a);
    glRasterPos2f(x, y);
    for (char c : text)
        glutBitmapCharacter(font, c);
}

void drawHUD()
{
    int winW = glutGet(GLUT_WINDOW_WIDTH), winH = glutGet(GLUT_WINDOW_HEIGHT);

    // switch to 2D ortho
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, winW, 0, winH);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    // Light translucent HUD box (top-left)
    float panelW = 380.0f, panelH = 120.0f;
    float px = 12.0f, py = winH - panelH - 12.0f;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // Removed white background box

    // header and rows
    drawTextShadowed("Controls: 1/2/3/4/5 select | Arrows drive | R reset | U then 1/2/3/4/5 = unpark",
                     px + 10, py + panelH - 26, GLUT_BITMAP_HELVETICA_12, 0.08f, 0.08f, 0.08f);
    drawTextShadowed("Clouds: '[' slow | ']' fast  , smaller | . larger",
                     px + 10, py + panelH - 44, GLUT_BITMAP_HELVETICA_12, 0.08f, 0.08f, 0.08f);
    char buf[128];
    std::sprintf(buf, "Active: Car %d    Parked: %s    Max speed: %.3f", activeCar + 1, cars[activeCar].parked ? "YES" : "NO", maxSpeed);
    drawTextShadowed(buf, px + 10, py + panelH - 46, GLUT_BITMAP_HELVETICA_12, 0.08f, 0.08f, 0.08f);

    for (int i = 0; i < (int)cars.size(); ++i)
    {
        char b2[120];
        std::sprintf(b2, "Car %d  x=%.2f z=%.2f ang=%.0f %s", i + 1, cars[i].x, cars[i].z, cars[i].angle, (i == activeCar ? "<--" : ""));
        drawTextShadowed(b2, px + 12, py + panelH - 68 - i * 18, GLUT_BITMAP_HELVETICA_12, 0.08f, 0.08f, 0.08f);
    }

    // parking occupancy mini-map
    float mapX = px + panelW - 128, mapY = py + 12;
    drawTextShadowed("Spots:", mapX, mapY + 78, GLUT_BITMAP_HELVETICA_10, 0.08f, 0.08f, 0.08f);
    for (int i = 0; i < (int)parkingAreas.size(); ++i)
    {
        float dotX = mapX + (i % 3) * 34.0f;
        float dotY = mapY + 54.0f - (i / 3) * 18.0f;
        bool occupied = false;
        for (auto &c : cars)
            if (c.parked && c.parkedSpot == i)
            {
                occupied = true;
                break;
            }
        if (occupied)
            glColor3f(0.0f, 0.96f, 0.96f);
        else
            glColor3f(0.62f, 0.66f, 0.70f);
        glBegin(GL_QUADS);
        glVertex2f(dotX - 6, dotY - 6);
        glVertex2f(dotX + 6, dotY - 6);
        glVertex2f(dotX + 6, dotY + 6);
        glVertex2f(dotX - 6, dotY + 6);
        glEnd();
        char lbl[6];
        std::sprintf(lbl, "P%d", i + 1);
        drawTextShadowed(lbl, dotX + 12, dotY - 5, GLUT_BITMAP_HELVETICA_10, 0.08f, 0.08f, 0.08f);
    }

    // Center large message (shadowed text only, no background box)
    if (!parkingMessage.empty() && messageAlpha > 0.01f)
    {
        float mx = winW * 0.25f, mw = winW * 0.5f;
        float my = winH * 0.45f, mh = 86.0f;
        // text
        if (parkingMessage.rfind("PARKED SUCCESSFULLY", 0) == 0)
        {
            drawTextShadowed(parkingMessage, mx + 20.0f, my + mh * 0.62f, GLUT_BITMAP_TIMES_ROMAN_24, 0.0f, 0.65f, 0.65f, messageAlpha);
            drawTextShadowed("Press R to continue", mx + 20.0f, my + mh * 0.32f, GLUT_BITMAP_HELVETICA_18, 0.08f, 0.08f, 0.08f, messageAlpha);
        }
        else
        {
            drawTextShadowed(parkingMessage, mx + 20.0f, my + mh * 0.50f, GLUT_BITMAP_TIMES_ROMAN_24, 0.9f, 0.25f, 0.12f, messageAlpha);
        }
    }

    glDisable(GL_BLEND);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

////////////////////////////////////////////////////////////////////////////////
// --- Scene draw --------------------------------------------------------------
////////////////////////////////////////////////////////////////////////////////
void drawScene()
{
    drawGround();

    // buildings, bollards, trees
    drawBuilding(-35, 0, 6, 40, 8);
    drawBuilding(35, 0, 6, 40, 8);
    drawBuilding(0, 40, 30, 6, 5);

    glColor3f(0.98f, 0.86f, 0.20f);
    for (float x : {-28.0f, -24.0f, -20.0f, 20.0f, 24.0f, 28.0f})
    {
        for (float z : {20.0f, -20.0f})
        {
            // kiosk base
            glPushMatrix();
            glTranslatef(x, 0.6f, z);
            glScalef(0.35f, 1.2f, 0.35f);
            glutSolidCube(1.0f);
            glPopMatrix();
            // kiosk roof / sign
            glColor3f(1.0f, 0.45f, 0.12f);
            glPushMatrix();
            glTranslatef(x, 0.7f, z);
            glScalef(0.4f, 0.2f, 0.4f);
            glutSolidCube(1.0f);
            // draw a multi-pane window on the front (+Z)
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glPushMatrix();
            // in local cube coords, front face is at +0.5 in Z
            glTranslatef(0.0f, 0.0f, 0.501f);
            // window grid: cols x rows
            int wcols = 3, wrows = 2;
            float marginX = 0.08f, marginY = 0.06f;
            float paneW = (1.0f - 2.0f * marginX) / wcols;
            float paneH = (1.0f - 2.0f * marginY) / wrows;
            // glass panes
            glColor4f(0.12f, 0.22f, 0.32f, 0.48f);
            for (int ry = 0; ry < wrows; ++ry)
            {
                for (int cx = 0; cx < wcols; ++cx)
                {
                    float lx = -0.5f + marginX + cx * paneW;
                    float ly = -0.5f + marginY + ry * paneH;
                    float rx = lx + paneW * 0.86f;
                    float ry2 = ly + paneH * 0.86f;
                    glBegin(GL_QUADS);
                    glVertex3f(lx, ry2, 0.0f);
                    glVertex3f(rx, ry2, 0.0f);
                    glVertex3f(rx, ly, 0.0f);
                    glVertex3f(lx, ly, 0.0f);
                    glEnd();
                }
            }
            // frames (thin lines)
            glColor3f(0.06f, 0.06f, 0.06f);
            glLineWidth(2.0f);
            // vertical frame lines
            for (int i = 0; i <= wcols; ++i)
            {
                float fx = -0.5f + marginX + i * paneW;
                glBegin(GL_LINES);
                glVertex3f(fx, -0.5f + marginY, 0.001f);
                glVertex3f(fx, 0.5f - marginY, 0.001f);
                glEnd();
            }
            // horizontal frame lines
            for (int i = 0; i <= wrows; ++i)
            {
                float fy = -0.5f + marginY + i * paneH;
                glBegin(GL_LINES);
                glVertex3f(-0.5f + marginX, fy, 0.001f);
                glVertex3f(0.5f - marginX, fy, 0.001f);
                glEnd();
            }
            glLineWidth(1.0f);
            glPopMatrix();
            glDisable(GL_BLEND);
            glPopMatrix();
            glColor3f(0.98f, 0.86f, 0.20f);
        }
    }
    drawStreetLight(-25, 20);
    drawStreetLight(25, 20);
    // rows of street trees for a realistic avenue
    for (float tx = -44.0f; tx <= -16.0f; tx += 6.0f)
        drawTree(tx, 25.0f);
    for (float tx = 16.0f; tx <= 44.0f; tx += 6.0f)
        drawTree(tx, 25.0f);
    // additional small green islands near parking areas
    for (float zx : {-18.0f, -6.0f, 6.0f, 18.0f})
        drawTree(zx, -28.0f);

    // cars
    float palette[3][3] = {{0.9f, 0.15f, 0.15f}, {0.2f, 0.7f, 0.2f}, {0.2f, 0.3f, 0.9f}};
    for (int i = 0; i < (int)cars.size(); ++i)
    {
        if (i == activeCar)
        {
            glPushMatrix();
            glTranslatef(cars[i].x, 0.01f, cars[i].z);
            glRotatef(-90, 1, 0, 0);
            glColor4f(0.0f, 0.9f, 0.95f, 0.14f);
            glutSolidTorus(0.01f * cars[i].scale, 1.06f * cars[i].scale, 6, 40);
            glPopMatrix();
        }
        drawCarModel(cars[i], palette[i % 3][0], palette[i % 3][1], palette[i % 3][2]);
        drawParkLight(cars[i]);
    }
}

////////////////////////////////////////////////////////////////////////////////
// --- Display callback -------------------------------------------------------
////////////////////////////////////////////////////////////////////////////////
void display()
{
    int w = glutGet(GLUT_WINDOW_WIDTH), h = glutGet(GLUT_WINDOW_HEIGHT);
    drawSkyGradient(w, h);

    glClear(GL_DEPTH_BUFFER_BIT);

    // camera
    glLoadIdentity();
    float deg2rad = 3.1415926535f / 180.0f;
    float yaw = camYaw * deg2rad, pitch = camPitch * deg2rad;
    float cx = camTargetX + camDist * cosf(pitch) * sinf(yaw);
    float cy = camTargetY + camDist * sinf(pitch);
    float cz = camTargetZ + camDist * cosf(pitch) * cosf(yaw);
    gluLookAt(cx, cy, cz, camTargetX, camTargetY, camTargetZ, 0, 1, 0);

    // lighting (warm golden tint)
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    GLfloat lightpos[] = {40.0f, 80.0f, 30.0f, 1.0f};
    GLfloat ambient[] = {0.55f, 0.55f, 0.52f, 1.0f};
    GLfloat diffuse[] = {1.0f, 0.96f, 0.88f, 1.0f};
    GLfloat spec[] = {0.75f, 0.7f, 0.65f, 1.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, lightpos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    glLightfv(GL_LIGHT0, GL_SPECULAR, spec);

    // Update camera target to follow active car (kept lower so environment doesn't appear lifted)
    camTargetX = cars[activeCar].x;
    camTargetY = cars[activeCar].y + 0.4f;
    camTargetZ = cars[activeCar].z;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawScene();

    // HUD overlay
    glDisable(GL_LIGHTING);
    drawHUD();
    glEnable(GL_LIGHTING);

    glutSwapBuffers();
}

////////////////////////////////////////////////////////////////////////////////
// --- Input handling ---------------------------------------------------------
////////////////////////////////////////////////////////////////////////////////
void specialDown(int key, int, int)
{
    if (key == GLUT_KEY_UP)
        upPressed = true;
    if (key == GLUT_KEY_DOWN)
        downPressed = true;
    if (key == GLUT_KEY_LEFT)
        leftPressed = true;
    if (key == GLUT_KEY_RIGHT)
        rightPressed = true;
}
void specialUp(int key, int, int)
{
    if (key == GLUT_KEY_UP)
        upPressed = false;
    if (key == GLUT_KEY_DOWN)
        downPressed = false;
    if (key == GLUT_KEY_LEFT)
        leftPressed = false;
    if (key == GLUT_KEY_RIGHT)
        rightPressed = false;
}

void keyboard(unsigned char key, int, int)
{
    if (pendingUnpark)
    {
        if (key >= '1' && key <= '5')
        {
            int idx = key - '1';
            if (idx >= 0 && idx < (int)cars.size())
            {
                cars[idx].parked = false;
                cars[idx].parkedSpot = -1;
                setParkingMessage("Car " + std::to_string(idx + 1) + " UNPARKED", 120);
            }
        }
        pendingUnpark = false;
        return;
    }

    switch (key)
    {
    case 27:
        exit(0);
        break;
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
        activeCar = key - '1';
        break;
    case 'R':
    {
        // reset
        cars[0].x = 0;
        cars[0].z = -10;
        cars[0].angle = 0;
        cars[0].speed = 0;
        cars[0].parked = false;
        cars[0].parkedSpot = -1;
        cars[1].x = -6;
        cars[1].z = -12;
        cars[1].angle = 10;
        cars[1].speed = 0;
        cars[1].parked = false;
        cars[1].parkedSpot = -1;
        cars[2].x = 6;
        cars[2].z = -12;
        cars[2].angle = -10;
        cars[2].speed = 0;
        cars[2].parked = false;
        cars[2].parkedSpot = -1;
        cars[3].x = parkingAreas[1].cx;
        cars[3].z = parkingAreas[1].cz;
        cars[3].angle = 90;
        cars[3].speed = 0;
        cars[3].parked = false;
        cars[3].parkedSpot = -1;
        cars[4].x = parkingAreas[3].cx;
        cars[4].z = parkingAreas[3].cz;
        cars[4].angle = 90;
        cars[4].speed = 0;
        cars[4].parked = false;
        cars[4].parkedSpot = -1;
        parkingMessage = "";
        messageTimer = 0;
        messageAlpha = 0.0f;
        // reset camera to front view
        camYaw = 180.0f;
        camPitch = 10.0f;
        camDist = 15.0f;
        camTargetX = 0.0f;
        camTargetY = 0.9f;
        camTargetZ = 0.0f;
        break;
    }
    case 'u':
    case 'U':
        pendingUnpark = true;
        setParkingMessage("UNPARK MODE: Press 1/2/3/4/5 to unpark", 120);
        break;
    case '+':
    case '=':
        maxSpeed = std::min(maxSpeed + 0.01f, 0.5f);
        break;
    case '-':
    case '_':
        maxSpeed = std::max(maxSpeed - 0.01f, 0.01f);
        break;
    case '0':
        maxSpeed = BASE_MAX_SPEED;
        break;
    case '[':
        cloudSpeed = std::max(0.0f, cloudSpeed - 0.05f);
        setParkingMessage("Cloud speed: " + std::to_string(cloudSpeed), 60);
        break;
    case ']':
        cloudSpeed = std::min(5.0f, cloudSpeed + 0.05f);
        setParkingMessage("Cloud speed: " + std::to_string(cloudSpeed), 60);
        break;
    case ',':
        cloudScale = std::max(0.5f, cloudScale - 0.08f);
        setParkingMessage("Cloud size: " + std::to_string(cloudScale), 60);
        break;
    case '.':
        cloudScale = std::min(3.0f, cloudScale + 0.08f);
        setParkingMessage("Cloud size: " + std::to_string(cloudScale), 60);
        break;
    // quick camera presets: top, bottom (low), left, right
    case 't':
    case 'T':
        camYaw = 180.0f;
        camPitch = 89.0f; // near straight down
        camDist = 45.0f;
        break;
    case 'b':
        // Back camera relative to the active car's heading
        camYaw = cars[activeCar].angle + 180.0f;
        camPitch = -12.0f; // low rear/ground view
        camDist = 9.0f;
        break;
    case 'f':
    case 'F':
        // Front camera relative to the active car's heading
        camYaw = cars[activeCar].angle;
        camPitch = 8.0f; // slight top angle
        camDist = 9.0f;
        break;
    case 'l':
    case 'L':
        camYaw = 270.0f;
        camPitch = 8.0f;
        camDist = 18.0f;
        break;
    case 'r':
        camYaw = 90.0f;
        camPitch = 8.0f;
        camDist = 18.0f;
        break;
    }
}

void mouseButton(int button, int state, int x, int y)
{
    if (button == GLUT_LEFT_BUTTON)
    {
        leftDrag = (state == GLUT_DOWN);
        if (leftDrag)
        {
            lastMouseX = x;
            lastMouseY = y;
        }
    }
    else if (button == GLUT_MIDDLE_BUTTON)
    {
        middleDrag = (state == GLUT_DOWN);
        if (middleDrag)
        {
            lastMouseX = x;
            lastMouseY = y;
        }
    }
    else if (button == 3 && state == GLUT_DOWN)
    { // wheel up
        camDist = std::max(4.0f, camDist - 1.4f);
    }
    else if (button == 4 && state == GLUT_DOWN)
    { // wheel down
        camDist = std::min(200.0f, camDist + 1.4f);
    }
}

void mouseMotion(int x, int y)
{
    int dx = x - lastMouseX, dy = y - lastMouseY;
    lastMouseX = x;
    lastMouseY = y;
    if (leftDrag)
    {
        camYaw += dx * 0.35f;
        camPitch -= dy * 0.35f;
        if (camPitch > 89.0f)
            camPitch = 89.0f;
        if (camPitch < -89.0f)
            camPitch = -89.0f;
    }
    else if (middleDrag)
    {
        // pan in camera plane
        float yaw = camYaw * 3.1415926535f / 180.0f, pitch = camPitch * 3.1415926535f / 180.0f;
        float rx = cosf(yaw), rz = -sinf(yaw);
        float ux = -sinf(pitch) * sinf(yaw), uz = -sinf(pitch) * cosf(yaw);
        float panSpeed = camDist * 0.0025f;
        camTargetX += (-dx * rx + dy * ux) * panSpeed;
        camTargetZ += (-dx * rz + dy * uz) * panSpeed;
        camTargetY += dy * 0.004f;
    }
    glutPostRedisplay();
}

void reshape(int w, int h)
{
    if (h == 0)
        h = 1;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(60.0, (double)w / h, 0.1, 300.0);
    glMatrixMode(GL_MODELVIEW);
}

////////////////////////////////////////////////////////////////////////////////
// --- Idle update -------------------------------------------------------------
////////////////////////////////////////////////////////////////////////////////
void idle()
{
    // unpark logic: if active car is parked and driver presses drive keys -> unpark
    Car &ac = cars[activeCar];
    if (ac.parked && (upPressed || downPressed || leftPressed || rightPressed))
    {
        ac.parked = false;
        ac.parkedSpot = -1;
    }

    // update active car physics & parking detection
    updateActiveCar(ac);

    // decrement flash timers
    for (auto &c : cars)
    {
        if (c.flashTimer > 0.0f)
        {
            c.flashTimer -= 0.016f;
            if (c.flashTimer < 0.0f)
                c.flashTimer = 0.0f;
        }
    }

    // message fade management
    if (messageTimer > 0)
    {
        --messageTimer;
        if (messageTimer < 60)
            messageAlpha = std::max(0.0f, messageAlpha - 0.016f);
        else
            messageAlpha = std::min(1.0f, messageAlpha + 0.04f);
    }
    else
    {
        messageAlpha = std::max(0.0f, messageAlpha - 0.02f);
        if (messageAlpha < 0.01f)
            parkingMessage.clear();
    }

    // advance cloud animation (wrap to avoid large numbers)
    cloudOffset += cloudSpeed;
    if (cloudOffset > 10000.0f)
        cloudOffset = fmodf(cloudOffset, 10000.0f);

    glutPostRedisplay();
}

// Draw a car at an arbitrary position/rotation with a given base color
void drawSimpleCar(float x, float y, float z, float yawDeg, float r, float g, float b)
{
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(yawDeg, 0, 1, 0);

    // Main body - base color
    glColor3f(r, g, b);
    glPushMatrix();
    glScalef(1.3f, 0.7f, 2.2f);
    glutSolidCube(1.0);
    glPopMatrix();

    // Roof/cabin - slightly darker
    glColor3f(std::max(0.0f, r - 0.2f), std::max(0.0f, g - 0.05f), std::max(0.0f, b - 0.05f));
    glPushMatrix();
    glTranslatef(0, 0.35f, -0.2f);
    glScalef(1.1f, 0.4f, 1.2f);
    glutSolidCube(1.0);
    glPopMatrix();

    // Windows - darker glass tint to increase contrast with body
    glColor3f(0.15f, 0.45f, 0.6f);
    glPushMatrix();
    glTranslatef(0, 0.4f, 0);
    glScalef(0.78f, 0.28f, 0.94f);
    glutSolidCube(1.0);
    glPopMatrix();
    // Window frames - thin dark strips
    glColor3f(0.02f, 0.02f, 0.02f);
    glPushMatrix();
    glTranslatef(0, 0.56f, 0);
    glScalef(0.78f, 0.04f, 0.96f);
    glutSolidCube(1.0);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(0, 0.28f, 0);
    glScalef(0.78f, 0.03f, 0.96f);
    glutSolidCube(1.0);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(-0.38f, 0.42f, 0);
    glScalef(0.06f, 0.3f, 0.96f);
    glutSolidCube(1.0);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(0.38f, 0.42f, 0);
    glScalef(0.06f, 0.3f, 0.96f);
    glutSolidCube(1.0);
    glPopMatrix();

    // Headlights - bright white/yellow
    glColor3f(1.0f, 0.95f, 0.7f);
    glPushMatrix();
    glTranslatef(0.5f, 0, 1.0f);
    glutSolidSphere(0.12f, 8, 8);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(-0.5f, 0, 1.0f);
    glutSolidSphere(0.12f, 8, 8);
    glPopMatrix();

    // Front grille - thin horizontal bars for a simple grill look (simple car)
    glColor3f(0.05f, 0.05f, 0.05f);
    for (float yy = -0.02f; yy <= 0.04f; yy += 0.02f)
    {
        glBegin(GL_QUADS);
        glVertex3f(-0.42f, yy, 1.06f);
        glVertex3f(0.42f, yy, 1.06f);
        glVertex3f(0.42f, yy + 0.008f, 1.06f);
        glVertex3f(-0.42f, yy + 0.008f, 1.06f);
        glEnd();
    }

    // Front bumper for simple car - black
    glColor3f(0.02f, 0.02f, 0.02f);
    glPushMatrix();
    glTranslatef(0, -0.12f, 1.12f);
    glScalef(1.32f, 0.16f, 0.12f);
    glutSolidCube(1.0);
    glPopMatrix();

    // Taillights - red
    glColor3f(0.4f, 0.04f, 0.04f); // dim red for parked
    glPushMatrix();
    glTranslatef(0.5f, 0, -1.0f);
    glutSolidSphere(0.1f, 8, 8);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(-0.5f, 0, -1.0f);
    glutSolidSphere(0.1f, 8, 8);
    glPopMatrix();

    // Rear bumper for simple car - black
    glColor3f(0.02f, 0.02f, 0.02f);
    glPushMatrix();
    glTranslatef(0, -0.12f, -1.12f);
    glScalef(1.32f, 0.16f, 0.12f);
    glutSolidCube(1.0);
    glPopMatrix();

    // Door panels and handles for parked cars (match moving car style)
    glColor3f(std::max(0.0f, r - 0.25f), std::max(0.0f, g - 0.05f), std::max(0.0f, b - 0.05f));
    // left panel (narrower depth so it doesn't cover windows)
    glPushMatrix();
    glTranslatef(-0.52f, 0.05f, 0.0f);
    glScalef(0.09f, 0.48f, 1.02f);
    glutSolidCube(1.0f);
    glPopMatrix();
    // right panel
    glPushMatrix();
    glTranslatef(0.52f, 0.05f, 0.0f);
    glScalef(0.09f, 0.48f, 1.02f);
    glutSolidCube(1.0f);
    glPopMatrix();
    // Door seam
    glColor3f(0.02f, 0.02f, 0.02f);
    glPushMatrix();
    glTranslatef(0.0f, 0.05f, 0.0f);
    glScalef(0.02f, 0.5f, 1.05f);
    glutSolidCube(1.0);
    glPopMatrix();
    // handles (metallic)
    glColor3f(0.78f, 0.78f, 0.78f);
    glPushMatrix();
    glTranslatef(-0.44f, 0.0f, 0.25f);
    glScalef(0.08f, 0.03f, 0.18f);
    glutSolidCube(1.0f);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(0.44f, 0.0f, 0.25f);
    glScalef(0.08f, 0.03f, 0.18f);
    glutSolidCube(1.0f);
    glPopMatrix();

    // Wheels - black torus similar to the player car
    glColor3f(0.15f, 0.15f, 0.15f);
    for (float wx : {-0.65f, 0.65f})
        for (float wz : {-0.9f, 0.9f})
        {
            glPushMatrix();
            // lift wheels slightly to avoid clipping into the ground
            glTranslatef(wx, -0.25f, wz);
            glRotatef(90, 0, 1, 0);
            glutSolidTorus(0.1, 0.25, 12, 16);
            glPopMatrix();
        }

    glPopMatrix();
}

void drawParkedCars()
{
    float palette[6][3] = {
        {0.2f, 0.2f, 0.7f}, {0.2f, 0.7f, 0.2f}, {0.9f, 0.2f, 0.2f}, {0.9f, 0.6f, 0.1f}, {0.5f, 0.3f, 0.8f}, {0.2f, 0.8f, 0.8f}};
    for (size_t i = 0; i < parkedCars.size(); ++i)
    {
        auto &c = parkedCars[i];
        float yaw = (i % 2 == 0) ? 0.0f : 180.0f; // alternate facing
        float *col = palette[i % 6];
        // place car so its bottom sits on ground (c.cy is around 0.25)
        drawSimpleCar(c.cx, c.cy + c.hy, c.cz, yaw, col[0], col[1], col[2]);
    }
}

////////////////////////////////////////////////////////////////////////////////
// --- Main -------------------------------------------------------------------
////////////////////////////////////////////////////////////////////////////////
int main(int argc, char **argv)
{
    // build parking areas
    parkingAreas = {
        // P1..P6: horizontal (wider along X)
        {-20, 0.1f, 10, 4.5f, 0.1f, 3.5f}, {-20, 0.1f, 0, 4.5f, 0.1f, 3.5f}, {-20, 0.1f, -10, 4.5f, 0.1f, 3.5f},
        {20, 0.1f, 10, 4.5f, 0.1f, 3.5f}, {20, 0.1f, 0, 4.5f, 0.1f, 3.5f}, {20, 0.1f, -10, 4.5f, 0.1f, 3.5f},
        // P7..P9: vertical (as a row at top)
        {-10, 0.1f, 30, 3.5f, 0.1f, 4.5f}, {0, 0.1f, 30, 3.5f, 0.1f, 4.5f}, {10, 0.1f, 30, 3.5f, 0.1f, 4.5f}};

    // buildings
    buildings.push_back({-35, 4, 0, 6, 8, 40});
    buildings.push_back({35, 4, 0, 6, 8, 40});
    buildings.push_back({0, 2.5f, 40, 30, 5, 6});

    // bollards (as AABB entries for collision)
    float xs[] = {-28.0f, -24.0f, -20.0f, 20.0f, 24.0f, 28.0f};
    float zs[] = {20.0f, -20.0f};
    for (float x : xs)
        for (float z : zs)
            bollards.push_back({x, 0.6f, z, 0.35f, 1.2f, 0.35f});

    // cars (identical scale)
    cars.clear();
    Car c1;
    c1.x = 0;
    c1.y = 0.6f;
    c1.z = -10;
    c1.angle = 0;
    c1.speed = 0;
    c1.scale = 1.0f;
    c1.parked = false;
    c1.parkedSpot = -1;
    c1.brakeLight = 0;
    c1.flashTimer = 0;
    c1.name = "Car1";
    Car c2;
    c2.x = -6;
    c2.y = 0.6f;
    c2.z = -12;
    c2.angle = 10;
    c2.speed = 0;
    c2.scale = 1.0f;
    c2.parked = false;
    c2.parkedSpot = -1;
    c2.brakeLight = 0;
    c2.flashTimer = 0;
    c2.name = "Car2";
    Car c3;
    c3.x = 6;
    c3.y = 0.6f;
    c3.z = -12;
    c3.angle = -10;
    c3.speed = 0;
    c3.scale = 1.0f;
    c3.parked = false;
    c3.parkedSpot = -1;
    c3.brakeLight = 0;
    c3.flashTimer = 0;
    c3.name = "Car3";
    cars.push_back(c1);
    cars.push_back(c2);
    cars.push_back(c3);

    // Add two more movable cars starting in parking spots
    Car c4;
    c4.x = parkingAreas[1].cx;
    c4.y = 0.6f;
    c4.z = parkingAreas[1].cz;
    c4.angle = 90; // face along X to align with horizontal spot
    c4.speed = 0;
    c4.scale = 1.0f;
    c4.parked = false;
    c4.parkedSpot = -1;
    c4.brakeLight = 0;
    c4.flashTimer = 0;
    c4.name = "Car4";
    cars.push_back(c4);

    Car c5;
    c5.x = parkingAreas[3].cx;
    c5.y = 0.6f;
    c5.z = parkingAreas[3].cz;
    c5.angle = 90; // face along X to align with horizontal spot
    c5.speed = 0;
    c5.scale = 1.0f;
    c5.parked = false;
    c5.parkedSpot = -1;
    c5.brakeLight = 0;
    c5.flashTimer = 0;
    c5.name = "Car5";
    cars.push_back(c5);

    // GLUT init
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowSize(1280, 820);
    glutCreateWindow("Parking 3D - Bright Sunny Edition (Final)");
    glutDisplayFunc(display);
    glutIdleFunc(idle);
    glutReshapeFunc(reshape);
    glutSpecialFunc(specialDown);
    glutSpecialUpFunc(specialUp);
    glutKeyboardFunc(keyboard);
    glutMouseFunc(mouseButton);
    glutMotionFunc(mouseMotion);

    // GL init
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glShadeModel(GL_SMOOTH);
    glClearColor(0.58f, 0.80f, 0.98f, 1.0f);

    glutMainLoop();
    return 0;
}
