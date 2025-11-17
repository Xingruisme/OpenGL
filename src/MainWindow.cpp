#include "MainWindow.h"
#include "GLWidget.h"
#include <QToolBar>
#include <QAction>
#include <QComboBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_glWidget = new GLWidget(this);
    setCentralWidget(m_glWidget);

    QToolBar *tb = addToolBar("Tools");
    QComboBox *materialCombo = new QComboBox(tb);
    materialCombo->addItem("Cotton");
    materialCombo->addItem("Silk");
    materialCombo->addItem("Denim");
    tb->addWidget(materialCombo);

    connect(materialCombo, &QComboBox::currentTextChanged, m_glWidget, &GLWidget::setMaterial);
}

MainWindow::~MainWindow() = default;
