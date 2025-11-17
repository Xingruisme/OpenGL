#include "GLWidget.h"
#include "FabricSimulation.h"
#include <QOpenGLShaderProgram>
#include <QDebug>

GLWidget::GLWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
}

GLWidget::~GLWidget() = default;

void GLWidget::setMaterial(const QString &name)
{
    // create simulation instance based on name
    if (name == "Cotton")
        m_sim = std::make_unique<CottonSimulation>();
    else if (name == "Silk")
        m_sim = std::make_unique<SilkSimulation>();
    else if (name == "Denim")
        m_sim = std::make_unique<DenimSimulation>();

    if (m_sim)
        m_sim->initialize();

    update();
}

void GLWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);

    // default material
    setMaterial("Cotton");

    m_timerId = startTimer(16); // ~60fps
}

void GLWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void GLWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (m_sim)
        m_sim->render();
}

void GLWidget::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_timerId) {
        if (m_sim) {
            m_sim->step(0.016f);
        }
        update();
    }
}
