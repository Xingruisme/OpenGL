#pragma once

#include <vector>
#include <QVector3D>

// Simple abstract base for fabric simulation
class FabricSimulation
{
public:
    virtual ~FabricSimulation() = default;
    virtual void initialize() = 0;
    virtual void step(float dt) = 0;
    virtual void render() = 0;
};

// Placeholder material implementations - fill with real physics later
class CottonSimulation : public FabricSimulation
{
public:
    void initialize() override;
    void step(float dt) override;
    void render() override;

private:
    // ... data members for particles, springs, VAO/VBO, shader
};

class SilkSimulation : public FabricSimulation
{
public:
    void initialize() override;
    void step(float dt) override;
    void render() override;
};

class DenimSimulation : public FabricSimulation
{
public:
    void initialize() override;
    void step(float dt) override;
    void render() override;
};
