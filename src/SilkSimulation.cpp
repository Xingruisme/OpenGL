
#include "SilkSimulation.h"
#include <GL/glut.h>
#include <vector>
#include <cmath>

SilkSimulation::SilkSimulation(int width, int height)
    : m_width(width), m_height(height)
{
}

SilkSimulation::~SilkSimulation() = default;

static inline int idx(int x, int y, int w) { return y * w + x; }

void SilkSimulation::initialize()
{
    m_particles.clear();
    m_particles.resize(m_width * m_height);

    // layout in [-0.5,0.5] x [0.5,-0.5] (top row y=0 pinned)
    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            float fx = (float)x / (m_width - 1) - 0.5f;
            float fy = 0.5f - (float)y / (m_height - 1); // top -> bottom
            Particle &p = m_particles[idx(x, y, m_width)];
            p.pos = { fx, fy };
            p.prev = p.pos;
            p.pinned = (y == 0); // pin top row
        }
    }
}

void SilkSimulation::step(float dt)
{
    if (dt <= 0.0f) return;
    const Vec2 gravity{ 0.0f, -1.5f };
    const float damping = 0.9995f;
    const float dt2 = dt * dt;

    // Verlet integrate
    for (auto &p : m_particles) {
        if (p.pinned) continue;
        Vec2 temp = p.pos;
        Vec2 vel{ (p.pos.x - p.prev.x) * damping, (p.pos.y - p.prev.y) * damping };
        p.pos.x += vel.x + gravity.x * dt2;
        p.pos.y += vel.y + gravity.y * dt2;
        p.prev = temp;
    }

    // constraints: structural (neighbors)
    const int iterations = 6;
    const float restX = 1.0f / (m_width - 1);
    const float restY = 1.0f / (m_height - 1);

    for (int it = 0; it < iterations; ++it) {
        // horizontal constraints
        for (int y = 0; y < m_height; ++y) {
            for (int x = 0; x < m_width - 1; ++x) {
                int a = idx(x, y, m_width);
                int b = idx(x + 1, y, m_width);
                Particle &pa = m_particles[a];
                Particle &pb = m_particles[b];
                float dx = pb.pos.x - pa.pos.x;
                float dy = pb.pos.y - pa.pos.y;
                float dist = std::sqrt(dx*dx + dy*dy);
                if (dist <= 1e-6f) continue;
                float target = restX;
                float diff = (dist - target) / dist * 0.5f;
                if (!pa.pinned) { pa.pos.x += dx * diff; pa.pos.y += dy * diff; }
                if (!pb.pinned) { pb.pos.x -= dx * diff; pb.pos.y -= dy * diff; }
            }
        }
        // vertical constraints
        for (int y = 0; y < m_height - 1; ++y) {
            for (int x = 0; x < m_width; ++x) {
                int a = idx(x, y, m_width);
                int b = idx(x, y + 1, m_width);
                Particle &pa = m_particles[a];
                Particle &pb = m_particles[b];
                float dx = pb.pos.x - pa.pos.x;
                float dy = pb.pos.y - pa.pos.y;
                float dist = std::sqrt(dx*dx + dy*dy);
                if (dist <= 1e-6f) continue;
                float target = restY;
                float diff = (dist - target) / dist * 0.5f;
                if (!pa.pinned) { pa.pos.x += dx * diff; pa.pos.y += dy * diff; }
                if (!pb.pinned) { pb.pos.x -= dx * diff; pb.pos.y -= dy * diff; }
            }
        }
    }
}

void SilkSimulation::render()
{
    // draw structural lines
    glColor3f(0.9f, 0.9f, 0.8f);
    glBegin(GL_LINES);
    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            const auto &p = m_particles[idx(x, y, m_width)];
            // horizontal
            if (x < m_width - 1) {
                const auto &q = m_particles[idx(x+1, y, m_width)];
                glVertex3f(p.pos.x, p.pos.y, 0.0f);
                glVertex3f(q.pos.x, q.pos.y, 0.0f);
            }
            // vertical
            if (y < m_height - 1) {
                const auto &q = m_particles[idx(x, y+1, m_width)];
                glVertex3f(p.pos.x, p.pos.y, 0.0f);
                glVertex3f(q.pos.x, q.pos.y, 0.0f);
            }
        }
    }
    glEnd();

    // draw particles (optional)
    glPointSize(3.0f);
    glBegin(GL_POINTS);
    glColor3f(1.0f, 0.3f, 0.3f);
    for (const auto &p : m_particles) {
        glVertex3f(p.pos.x, p.pos.y, 0.0f);
    }
    glEnd();
}