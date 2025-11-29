// 注意：必须先包含 glew.h，再包含 glfw3.h
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/packing.hpp> 

#include <vector>
#include <iostream>
#include <cmath>
#include <algorithm> // 包含 std::clamp

// ==========================================
// 设置与常量
// ==========================================
const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;

// Cloth resolution
const int CLOTH_W = 60;
const int CLOTH_H = 60;
const float DAMPING = 0.98f;
const float TIME_STEP = 0.01f;
const int CONSTRAINT_ITERATIONS = 5;

// Silk physical parameters
const float STRUCTURAL_STIFFNESS = 1.0f;
const float SHEAR_STIFFNESS = 0.8f;
const float BENDING_STIFFNESS = 0.05f;

// Render mode state
enum RenderMode { SHADED, WIREFRAME, POINTS };
RenderMode currentRenderMode = SHADED;
bool key_M_pressed = false;
float pointSize = 3.0f;

// ==========================================
// Global Camera and Mouse State
// ==========================================
// 修正：将相机初始位置调远，以便能看到整个布料
glm::vec3 cameraPos = glm::vec3(0.0f, 3.0f, 12.0f); // 初始Z值增大
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);

float deltaTime = 0.0f;
float lastFrame = 0.0f;
float windPower = 0.0f;

// Mouse Look Around state
bool firstMouse = true;
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
float yaw = -90.0f;
float pitch = 0.0f;

// Particle dragging state
int grabbedParticleIndex = -1;
float grabDistance = 0.0f;
bool isDraggingCamera = false;

// ==========================================
// Shader Source (Anisotropic Lighting)
// ==========================================
const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out vec3 Tangent;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    Tangent = mat3(model) * aTangent; 
    TexCoords = aTexCoords;
    
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in vec3 Tangent;

uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 objectColor;
uniform bool useTexture;
uniform sampler2D clothTexture;

void main()
{
    // -------------------------------------------
    // Silk Rendering - Anisotropic Specular
    // -------------------------------------------
    
    vec3 N = normalize(Normal);
    vec3 T = normalize(Tangent);
    vec3 V = normalize(viewPos - FragPos);
    vec3 L = normalize(lightPos - FragPos);
    
    // Calculate reflection vector R = reflect(-L, N)
    vec3 R = reflect(-L, N);
    
    // Half-way vector H
    vec3 H = normalize(L + V);

    // 1. Ambient
    float ambientStrength = 0.2;
    vec3 ambient = ambientStrength * vec3(1.0);
  
    // 2. Diffuse
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * vec3(1.0) * 0.8;
    
    // 3. Anisotropic Specular
    // We use the half-way vector H projected onto the tangent plane as the specular direction
    // For simpler calculation, use the sin(theta_TH) approach which gives better silk sheen
    float dotTH = dot(T, H);
    // sin(theta) = sqrt(1 - cos^2(theta)), where cos(theta)=dot(T, H)
    float sinTH = sqrt(1.0 - dotTH * dotTH);
    // Higher power for sharper specular highlight
    float spec = pow(max(sinTH, 0.0), 80.0); 
    
    vec3 specularColor = vec3(1.0, 0.95, 0.9);
    vec3 specular = 1.5 * spec * specularColor;

    vec3 baseColor = objectColor;
    
    vec3 result = (ambient + diffuse) * baseColor + specular;
    FragColor = vec4(result, 1.0);
}
)";

// ==========================================
// Physics Structure
// ==========================================
struct Particle {
    glm::vec3 position;
    glm::vec3 oldPosition;
    glm::vec3 acceleration;
    glm::vec2 uv;
    glm::vec3 normal;
    glm::vec3 tangent;
    bool isPinned;
    float mass;

    Particle(glm::vec3 pos, glm::vec2 tex) :
        position(pos), oldPosition(pos), acceleration(0.0f),
        uv(tex), normal(0.0f, 0.0f, 1.0f), tangent(1.0f, 0.0f, 0.0f),
        isPinned(false), mass(1.0f) {
    }

    void addForce(glm::vec3 f) {
        acceleration += f / mass;
    }

    void update(float dt) {
        if (isPinned) return;

        glm::vec3 velocity = position - oldPosition;
        oldPosition = position;
        // 修正：使用 std::clamp 限制速度，防止粒子飞走，提高稳定性
        float velocityMag = glm::length(velocity);
        if (velocityMag > 10.0f) { // 限制最大速度
            velocity = glm::normalize(velocity) * 10.0f;
        }

        position += velocity * DAMPING + acceleration * dt * dt;
        acceleration = glm::vec3(0.0f);
    }
};

struct Constraint {
    Particle* p1;
    Particle* p2;
    float restDistance;
    float stiffness;

    Constraint(Particle* pi, Particle* pj, float stiff) : p1(pi), p2(pj), stiffness(stiff) {
        restDistance = glm::distance(p1->position, p2->position);
    }

    void solve() {
        glm::vec3 delta = p2->position - p1->position;
        float currentDist = glm::length(delta);
        if (currentDist == 0.0f) return;

        // PBD 约束求解
        float correctionAmount = (currentDist - restDistance) / currentDist;
        glm::vec3 correction = delta * correctionAmount * 0.5f * stiffness;

        if (!p1->isPinned) p1->position += correction;
        if (!p2->isPinned) p2->position -= correction;
    }
};

// ==========================================
// Cloth Class
// ==========================================
class Cloth {
public:
    int width, height;
    std::vector<Particle> particles;
    std::vector<Constraint> constraints;
    std::vector<unsigned int> indices;

    unsigned int VAO, VBO, EBO;

    Cloth(int w, int h) : width(w), height(h) {
        particles.reserve(w * h);
        float spacing = 0.1f;

        // 初始化布料网格，并稍微抬高，使其处于视野中心
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                // 初始位置设置在 Y=3.0f 左右
                glm::vec3 pos((x - w / 2.0f) * spacing, 3.0f + (y - h / 2.0f) * spacing, 0.0f);
                glm::vec2 uv((float)x / (w - 1), (float)y / (h - 1));
                Particle p(pos, uv);

                // 挂住顶部边缘的粒子（每隔5个固定一个）
                if (y == h - 1 && (x % 5 == 0)) {
                    p.isPinned = true;
                }
                particles.push_back(p);
            }
        }

        auto addConstraint = [&](int x1, int y1, int x2, int y2, float k) {
            if (x1 >= 0 && x1 < w && y1 >= 0 && y1 < h &&
                x2 >= 0 && x2 < w && y2 >= 0 && y2 < h) {
                constraints.emplace_back(&particles[y1 * w + x1], &particles[y2 * w + x2], k);
            }
            };

        // 创建约束
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                // Structural (结构约束)
                addConstraint(x, y, x + 1, y, STRUCTURAL_STIFFNESS);
                addConstraint(x, y, x, y + 1, STRUCTURAL_STIFFNESS);

                // Shear (剪切约束)
                addConstraint(x, y, x + 1, y + 1, SHEAR_STIFFNESS);
                addConstraint(x, y, x - 1, y + 1, SHEAR_STIFFNESS);

                // Bending (弯曲约束) - 保持布料形状
                addConstraint(x, y, x + 2, y, BENDING_STIFFNESS);
                addConstraint(x, y, x, y + 2, BENDING_STIFFNESS);
            }
        }

        // 创建三角形索引
        for (int y = 0; y < h - 1; y++) {
            for (int x = 0; x < w - 1; x++) {
                int topLeft = y * w + x;
                int topRight = topLeft + 1;
                int bottomLeft = (y + 1) * w + x;
                int bottomRight = bottomLeft + 1;

                // Triangle 1
                indices.push_back(topLeft);
                indices.push_back(bottomLeft);
                indices.push_back(topRight);

                // Triangle 2
                indices.push_back(topRight);
                indices.push_back(bottomLeft);
                indices.push_back(bottomRight);
            }
        }

        setupMesh();
    }

    void update(float dt, glm::vec3 wind) {
        // A. Apply forces (Gravity + Wind)
        for (auto& p : particles) {
            if (!p.isPinned) {
                p.addForce(glm::vec3(0.0f, -9.8f, 0.0f)); // 重力
            }

            // 风力
            glm::vec3 windForce = wind * (glm::dot(p.normal, glm::normalize(wind)) * 0.8f + 0.2f);
            p.addForce(windForce);
        }

        // B. Integrate positions
        for (auto& p : particles) {
            p.update(dt);
        }

        // C. Satisfy constraints (PBD)
        for (int i = 0; i < CONSTRAINT_ITERATIONS; i++) {
            for (auto& c : constraints) {
                c.solve();
            }
        }

        // D. Recalculate Normals and Tangents
        recalculateNormals();
    }

    void recalculateNormals() {
        for (auto& p : particles) {
            p.normal = glm::vec3(0.0f);
            p.tangent = glm::vec3(0.0f);
        }

        for (size_t i = 0; i < indices.size(); i += 3) {
            Particle& p1 = particles[indices[i]];
            Particle& p2 = particles[indices[i + 1]];
            Particle& p3 = particles[indices[i + 2]];

            glm::vec3 edge1 = p2.position - p1.position;
            glm::vec3 edge2 = p3.position - p1.position;
            glm::vec3 normal = glm::cross(edge1, edge2);

            glm::vec3 tangent = glm::normalize(edge1); // 使用边1作为切线向量

            p1.normal += normal; p2.normal += normal; p3.normal += normal;
            p1.tangent += tangent; p2.tangent += tangent; p3.tangent += tangent;
        }

        for (auto& p : particles) {
            p.normal = glm::normalize(p.normal);
            p.tangent = glm::normalize(p.tangent);
        }
    }

    void setupMesh() {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        // Each vertex: Pos(3) + Norm(3) + Tex(2) + Tan(3) = 11 floats
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(particles.size() * 11 * sizeof(float)), NULL, GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)), indices.data(), GL_STATIC_DRAW);

        size_t stride = 11 * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride), (void*)(8 * sizeof(float)));

        glBindVertexArray(0);
    }

    void draw(unsigned int shaderProgram, RenderMode mode) {
        // Update VBO data
        std::vector<float> data;
        data.reserve(particles.size() * 11);
        for (const auto& p : particles) {
            data.push_back(p.position.x); data.push_back(p.position.y); data.push_back(p.position.z);
            data.push_back(p.normal.x);   data.push_back(p.normal.y);   data.push_back(p.normal.z);
            data.push_back(p.uv.x);       data.push_back(p.uv.y);
            data.push_back(p.tangent.x);  data.push_back(p.tangent.y);  data.push_back(p.tangent.z);
        }

        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(data.size() * sizeof(float)), data.data());

        // Draw based on mode
        if (mode == POINTS) {
            glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(particles.size()));
        }
        else {
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, 0);
        }
        glBindVertexArray(0);
    }
};

// ==========================================
// Utility Functions and Callbacks
// ==========================================

// State struct to pass data to static callbacks
struct AppState {
    Cloth* cloth;
    glm::mat4 projection;
    glm::mat4 view;
    // 窗口尺寸用于 unProject
    int width = SCR_WIDTH;
    int height = SCR_HEIGHT;
};

// Particle picking function
int getParticleIndexUnderCursor(double xpos, double ypos, const Cloth& cloth, const glm::mat4& view, const glm::mat4& projection, int width, int height) {
    float closestDistSq = 1000000.0f;
    int closestIndex = -1;
    glm::vec4 viewport = glm::vec4(0, 0, width, height);
    float scaledY = height - (float)ypos; // 修正：将GLFW的Y坐标（左上角为0）转换为OpenGL的Y坐标（左下角为0）

    for (int i = 0; i < cloth.particles.size(); ++i) {
        const Particle& p = cloth.particles[i];

        // Project world position to screen position
        glm::vec3 screenPos = glm::project(p.position, view, projection, viewport);

        // Z 深度检查
        if (screenPos.z < 0.0f || screenPos.z > 1.0f) continue;

        // Calculate 2D distance squared 
        float dx = screenPos.x - (float)xpos;
        float dy = screenPos.y - scaledY;
        float distSq = dx * dx + dy * dy;

        const float GRAB_THRESHOLD_SQ = 50.0f * 50.0f; // 50 pixel threshold
        if (distSq < GRAB_THRESHOLD_SQ && distSq < closestDistSq) {
            closestDistSq = distSq;
            closestIndex = i;
        }
    }
    return closestIndex;
}

// Mouse button callback (for dragging particles and camera)
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    AppState* state = (AppState*)glfwGetWindowUserPointer(window);
    Cloth& cloth = *state->cloth;
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            // 尝试拾取粒子
            grabbedParticleIndex = getParticleIndexUnderCursor(xpos, ypos, cloth, state->view, state->projection, state->width, state->height);

            if (grabbedParticleIndex != -1) {
                // 拾取成功，禁用鼠标用于相机旋转，确保拖拽流畅
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                isDraggingCamera = false;

                cloth.particles[grabbedParticleIndex].isPinned = true;

                // 计算抓取深度
                glm::vec4 p_view = state->view * glm::vec4(cloth.particles[grabbedParticleIndex].position, 1.0f);
                grabDistance = -p_view.z;
            }
        }
        else if (action == GLFW_RELEASE) {
            if (grabbedParticleIndex != -1) {
                // 释放粒子
                cloth.particles[grabbedParticleIndex].isPinned = false;
            }
            grabbedParticleIndex = -1;
            grabDistance = 0.0f;
        }
    }
    else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        // 右键控制相机旋转
        if (action == GLFW_PRESS) {
            // 修正：进入相机拖拽模式时，隐藏并锁定鼠标
            isDraggingCamera = true;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            firstMouse = true; // 重置首次鼠标标志
        }
        else if (action == GLFW_RELEASE) {
            // 修正：退出相机拖拽模式时，恢复正常鼠标
            isDraggingCamera = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    }
}

// Mouse position callback (for dragging particles and rotating camera)
void cursor_position_callback(GLFWwindow* window, double xposIn, double yposIn) {
    AppState* state = (AppState*)glfwGetWindowUserPointer(window);
    Cloth& cloth = *state->cloth;

    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    if (isDraggingCamera) {
        // ========== View Rotation Logic ==========

        if (firstMouse) {
            lastX = xpos;
            lastY = ypos;
            firstMouse = false;
        }

        float xoffset = xpos - lastX;
        float yoffset = lastY - ypos; // 注意：Y轴是反的
        lastX = xpos;
        lastY = ypos;

        float sensitivity = 0.1f;
        xoffset *= sensitivity;
        yoffset *= sensitivity;

        yaw += xoffset;
        pitch = std::clamp(pitch + yoffset, -89.0f, 89.0f); // 使用 std::clamp 限制俯仰角

        // Calculate new cameraFront vector
        glm::vec3 front;
        front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        front.y = sin(glm::radians(pitch));
        front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        cameraFront = glm::normalize(front);

    }
    else if (grabbedParticleIndex != -1) {
        // ========== Particle Dragging Logic ==========

        // 1. Ray start (camera position)
        glm::vec3 rayStart = cameraPos;

        // 2. Ray end (unproject mouse to far plane using screen coordinates)
        glm::vec4 viewport = glm::vec4(0, 0, state->width, state->height);

        // 修正：在 unProject 中使用 OpenGL 坐标系的 Y 轴 (height - ypos)
        glm::vec3 rayEnd = glm::unProject(
            glm::vec3(xpos, state->height - ypos, 0.0f),
            state->view, state->projection, viewport
        );

        // 3. Ray direction
        glm::vec3 rayDir = glm::normalize(rayEnd - rayStart);

        // 4. Target position is along the ray at grabDistance from cameraPos
        glm::vec3 newWorldPos = rayStart + rayDir * grabDistance;

        // 直接将粒子的旧位置和新位置都设置为目标位置，以保持粒子在拖拽时的稳定性
        cloth.particles[grabbedParticleIndex].position = newWorldPos;
        cloth.particles[grabbedParticleIndex].oldPosition = newWorldPos;
    }
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
    // 修正：更新 AppState 中的窗口尺寸，供 unProject 使用
    AppState* state = (AppState*)glfwGetWindowUserPointer(window);
    if (state) {
        state->width = width;
        state->height = height;
    }
}

void processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    // 只有在没有拖拽粒子的相机控制模式下才允许移动
    if (!isDraggingCamera && grabbedParticleIndex == -1) {
        // Camera movement (WASD)
        float cameraSpeed = 5.0f * deltaTime;
        glm::vec3 right = glm::normalize(glm::cross(cameraFront, cameraUp));

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            cameraPos += cameraSpeed * cameraFront;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            cameraPos -= cameraSpeed * cameraFront;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            cameraPos -= right * cameraSpeed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            cameraPos += right * cameraSpeed;
    }


    // Wind Control
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
        windPower = 5.0f;
    else
        windPower = 0.0f;

    // M key to switch render mode
    if (glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS && !key_M_pressed) {
        key_M_pressed = true;
        currentRenderMode = (RenderMode)((currentRenderMode + 1) % 3);

        switch (currentRenderMode) {
        case SHADED:    std::cout << "Render Mode: Shaded (填充)" << std::endl; break;
        case WIREFRAME: std::cout << "Render Mode: Wireframe (线框)" << std::endl; break;
        case POINTS:    std::cout << "Render Mode: Points (质点)" << std::endl; break;
        }
    }
    else if (glfwGetKey(window, GLFW_KEY_M) == GLFW_RELEASE) {
        key_M_pressed = false;
    }
}


int main()
{
    // 1. Initialize GLFW
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Silk Simulation - OpenGL", NULL, NULL);
    if (window == NULL) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    // 2. Initialize GLEW
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cout << "Failed to initialize GLEW" << std::endl;
        return -1;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glDisable(GL_CULL_FACE);

    // 3. Compile Shader 
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // 4. Initialize Cloth
    Cloth cloth(CLOTH_W, CLOTH_H);

    // 5. Setup GLFW User Pointer and Callbacks
    AppState appState;
    appState.cloth = &cloth;
    glfwSetWindowUserPointer(window, &appState);

    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);

    // 修正：确保鼠标一开始就是可见的，直到右键被按下
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    // 6. Render Loop
    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput(window);

        // Physics Update
        float time = currentFrame;
        // 修正：将风力方向朝向布料前方（-Z轴）略微偏移
        glm::vec3 wind(sin(time * 3.0f) * (2.0f + windPower), 0.5f * sin(time) + windPower, -cos(time * 2.0f) * (2.0f + windPower));
        if (windPower > 0.1f) wind.z -= windPower * 10.0f; // 按下空格时，风力主要吹向 -Z 轴

        float physicsStep = 0.01f;
        float accumulator = deltaTime;
        if (accumulator > 0.05f) accumulator = 0.05f;
        while (accumulator >= physicsStep) {
            cloth.update(physicsStep, wind);
            accumulator -= physicsStep;
        }

        // Render
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shaderProgram);

        // Recalculate and store View/Projection matrices
        // 修正：使用 AppState 里的动态宽高
        appState.projection = glm::perspective(glm::radians(45.0f), (float)appState.width / (float)appState.height, 0.1f, 100.0f);
        appState.view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
        glm::mat4 model = glm::mat4(1.0f);

        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, &appState.projection[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, &appState.view[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &model[0][0]);

        glUniform3f(glGetUniformLocation(shaderProgram, "viewPos"), cameraPos.x, cameraPos.y, cameraPos.z);
        glUniform3f(glGetUniformLocation(shaderProgram, "lightPos"), 5.0f, 5.0f, 10.0f);

        glUniform3f(glGetUniformLocation(shaderProgram, "objectColor"), 0.6f, 0.1f, 0.2f);
        glUniform1i(glGetUniformLocation(shaderProgram, "useTexture"), false);

        // Set Polygon Mode and Point Size based on render mode
        switch (currentRenderMode) {
        case SHADED:
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            break;
        case WIREFRAME:
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glLineWidth(1.5f); // 增加线宽以便在画布上更容易看到线框
            break;
        case POINTS:
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glPointSize(pointSize);
            break;
        }

        cloth.draw(shaderProgram, currentRenderMode);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}