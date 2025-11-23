#pragma once
#include <vector>

class SilkSimulation {
public:
    SilkSimulation(int width = 48, int height = 32);
    ~SilkSimulation();

    void initialize();
    void step(float dt);
    void render();

private:
    struct Vec2 { float x, y; Vec2() : x(0), y(0) {} Vec2(float a, float b):x(a),y(b){} };
    struct Particle {
        Vec2 pos;
        Vec2 prev;
        bool pinned = false;
    };

    int m_width;
    int m_height;
    std::vector<Particle> m_particles;
};
