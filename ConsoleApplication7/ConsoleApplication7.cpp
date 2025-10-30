#include <iostream>
#include <vector>
#include <map>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "OBJ_Loader.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

std::map<std::string, unsigned int> loadedTextures;

// Шейдеры для теней
const char* shadowVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;

uniform mat4 lightSpaceMatrix;
uniform mat4 model;

void main() {
    gl_Position = lightSpaceMatrix * model * vec4(aPos, 1.0);
}
)";

const char* shadowFragmentShaderSource = R"(
#version 330 core
void main() {
}
)";

const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;
out vec4 FragPosLightSpace;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;

void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = aNormal;
    TexCoord = aTexCoord;
    FragPosLightSpace = lightSpaceMatrix * vec4(FragPos, 1.0);
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;
in vec4 FragPosLightSpace;

uniform vec3 material_Kd;
uniform vec3 material_Ka;
uniform vec3 material_Ks;
uniform float material_Ns;

uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 lightColor;

uniform sampler2D diffuseTexture;
uniform sampler2D shadowMap;
uniform bool useTexture;

float ShadowCalculation(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    // Перспективное деление
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    // Преобразование в диапазон [0,1]
    projCoords = projCoords * 0.5 + 0.5;
    
    // Проверка выхода за границы
    if(projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0)
        return 0.0;
        
    // Ближайшая глубина из shadow map
    float closestDepth = texture(shadowMap, projCoords.xy).r;
    // Текущая глубина от источника света
    float currentDepth = projCoords.z;
    
    // Смещение для борьбы с shadow acne
    float bias = 0.005;
    
    // Простая проверка тени
    float shadow = currentDepth - bias > closestDepth ? 1.0 : 0.0;
    
    return shadow;
}

void main() {
    // Базовый цвет - из текстуры или из материала
    vec3 baseColor;
    if (useTexture) {
        baseColor = texture(diffuseTexture, TexCoord).rgb;
    } else {
        baseColor = material_Kd;
    }
    
    // Ambient
    float ambientStrength = 0.3; // Увеличим ambient чтобы тени были заметнее
    vec3 ambient = material_Ka * lightColor * ambientStrength;
    
    // Diffuse 
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = baseColor * diff * lightColor;
    
    // Specular
    float specularStrength = 0.5;
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material_Ns);
    vec3 specular = material_Ks * spec * lightColor * specularStrength;
    
    // Shadow
    float shadow = ShadowCalculation(FragPosLightSpace, norm, lightDir);
    
    vec3 result = ambient + (1.0 - shadow) * (diffuse + specular);
    FragColor = vec4(result, 1.0);
}
)";

// Простой шейдер для отладки shadow map
const char* debugVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;

out vec2 TexCoords;

void main() {
    TexCoords = aTexCoords;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* debugFragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D depthMap;

void main() {
    float depthValue = texture(depthMap, TexCoords).r;
    FragColor = vec4(vec3(depthValue), 1.0);
}
)";

// Структура для FBO теней
struct ShadowMap {
    unsigned int FBO;
    unsigned int depthMap;
    unsigned int shaderProgram;
    int width, height;
};

// Функция создания shadow map
ShadowMap CreateShadowMap(int width = 2048, int height = 2048) {
    ShadowMap shadowMap;
    shadowMap.width = width;
    shadowMap.height = height;

    // Создаем FBO для теней
    glGenFramebuffers(1, &shadowMap.FBO);

    // Создаем текстуру глубины
    glGenTextures(1, &shadowMap.depthMap);
    glBindTexture(GL_TEXTURE_2D, shadowMap.depthMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

    // Прикрепляем текстуру глубины к FBO
    glBindFramebuffer(GL_FRAMEBUFFER, shadowMap.FBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMap.depthMap, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    // Проверяем готовность FBO
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cout << "ОШИБКА: Framebuffer не complete!" << std::endl;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Компилируем шейдер для теней
    unsigned int shadowVertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(shadowVertexShader, 1, &shadowVertexShaderSource, NULL);
    glCompileShader(shadowVertexShader);

    // Проверка компиляции шейдера
    int success;
    char infoLog[512];
    glGetShaderiv(shadowVertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shadowVertexShader, 512, NULL, infoLog);
        std::cout << "ОШИБКА компиляции shadow vertex shader:\n" << infoLog << std::endl;
    }

    unsigned int shadowFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(shadowFragmentShader, 1, &shadowFragmentShaderSource, NULL);
    glCompileShader(shadowFragmentShader);

    glGetShaderiv(shadowFragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shadowFragmentShader, 512, NULL, infoLog);
        std::cout << "ОШИБКА компиляции shadow fragment shader:\n" << infoLog << std::endl;
    }

    shadowMap.shaderProgram = glCreateProgram();
    glAttachShader(shadowMap.shaderProgram, shadowVertexShader);
    glAttachShader(shadowMap.shaderProgram, shadowFragmentShader);
    glLinkProgram(shadowMap.shaderProgram);

    glGetProgramiv(shadowMap.shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shadowMap.shaderProgram, 512, NULL, infoLog);
        std::cout << "ОШИБКА линковки shadow shader program:\n" << infoLog << std::endl;
    }

    glDeleteShader(shadowVertexShader);
    glDeleteShader(shadowFragmentShader);

    return shadowMap;
}

// Создание отладочного квадрата для shadow map
struct DebugQuad {
    unsigned int VAO, VBO;
    unsigned int shaderProgram;
};

DebugQuad CreateDebugQuad() {
    DebugQuad debugQuad;

    // Вершины для отладочного квадрата
    float quadVertices[] = {
        // positions   // texCoords
        -0.5f,  0.5f,  0.0f, 1.0f,
        -0.5f, -0.5f,  0.0f, 0.0f,
         0.5f, -0.5f,  1.0f, 0.0f,

        -0.5f,  0.5f,  0.0f, 1.0f,
         0.5f, -0.5f,  1.0f, 0.0f,
         0.5f,  0.5f,  1.0f, 1.0f
    };

    // Настройка VAO
    glGenVertexArrays(1, &debugQuad.VAO);
    glGenBuffers(1, &debugQuad.VBO);
    glBindVertexArray(debugQuad.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, debugQuad.VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    // Компилируем отладочный шейдер
    unsigned int debugVertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(debugVertexShader, 1, &debugVertexShaderSource, NULL);
    glCompileShader(debugVertexShader);

    unsigned int debugFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(debugFragmentShader, 1, &debugFragmentShaderSource, NULL);
    glCompileShader(debugFragmentShader);

    debugQuad.shaderProgram = glCreateProgram();
    glAttachShader(debugQuad.shaderProgram, debugVertexShader);
    glAttachShader(debugQuad.shaderProgram, debugFragmentShader);
    glLinkProgram(debugQuad.shaderProgram);

    glDeleteShader(debugVertexShader);
    glDeleteShader(debugFragmentShader);

    return debugQuad;
}

// Функция загрузки текстуры
unsigned int LoadTexture(const std::string& filename) {
    if (loadedTextures.find(filename) != loadedTextures.end()) {
        return loadedTextures[filename];
    }

    unsigned int textureID;
    glGenTextures(1, &textureID);

    int width, height, nrComponents;

    // Пробуем разные пути к файлу
    std::vector<std::string> possiblePaths = {
        filename,
        "textures/" + filename,
        "Textures/" + filename,
        "./" + filename,
        "../textures/" + filename
    };

    unsigned char* data = nullptr;
    std::string foundPath;

    for (const auto& path : possiblePaths) {
        data = stbi_load(path.c_str(), &width, &height, &nrComponents, 0);
        if (data) {
            foundPath = path;
            std::cout << "Текстура загружена: " << foundPath << " (" << width << "x" << height << ")" << std::endl;
            break;
        }
    }

    if (data) {
        GLenum format;
        if (nrComponents == 1)
            format = GL_RED;
        else if (nrComponents == 3)
            format = GL_RGB;
        else if (nrComponents == 4)
            format = GL_RGBA;

        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    else {
        std::cout << "ОШИБКА: Не удалось загрузить текстуру: " << filename << std::endl;
        std::cout << "Проверь что файл существует в одной из папок:" << std::endl;
        for (const auto& path : possiblePaths) {
            std::cout << "  - " << path << std::endl;
        }
        textureID = 0;
    }

    stbi_image_free(data);
    loadedTextures[filename] = textureID;
    return textureID;
}

unsigned int CompileShader(unsigned int type, const char* source) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cout << "Ошибка компиляции шейдера: " << infoLog << std::endl;
    }
    return shader;
}

unsigned int CreateShaderProgram() {
    unsigned int vertexShader = CompileShader(GL_VERTEX_SHADER, vertexShaderSource);
    unsigned int fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    int success;
    char infoLog[512];
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cout << "ОШИБКА линковки шейдерной программы:\n" << infoLog << std::endl;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return shaderProgram;
}

struct MeshData {
    unsigned int VAO, VBO, EBO;
    unsigned int textureID;
    objl::Material material;
    std::string name;
    bool hasTexture;
    int indexCount;
};

// Создание буферов для меша с учетом текстур
MeshData SetupMesh(const objl::Mesh& mesh) {
    MeshData meshData;
    meshData.material = mesh.MeshMaterial;
    meshData.name = mesh.MeshName;
    meshData.indexCount = mesh.Indices.size();

    std::vector<float> vertices;

    // Преобразуем вершины в плоский массив
    for (const auto& vertex : mesh.Vertices) {
        vertices.push_back(vertex.Position.X);
        vertices.push_back(vertex.Position.Y);
        vertices.push_back(vertex.Position.Z);
        vertices.push_back(vertex.Normal.X);
        vertices.push_back(vertex.Normal.Y);
        vertices.push_back(vertex.Normal.Z);
        vertices.push_back(vertex.TextureCoordinate.X);
        vertices.push_back(vertex.TextureCoordinate.Y);
    }

    glGenVertexArrays(1, &meshData.VAO);
    glGenBuffers(1, &meshData.VBO);
    glGenBuffers(1, &meshData.EBO);

    glBindVertexArray(meshData.VAO);

    glBindBuffer(GL_ARRAY_BUFFER, meshData.VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, meshData.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        mesh.Indices.size() * sizeof(unsigned int),
        mesh.Indices.data(),
        GL_STATIC_DRAW);

    // Атрибуты вершин
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    // Загружаем текстуру если есть
    meshData.hasTexture = !mesh.MeshMaterial.map_Kd.empty();
    if (meshData.hasTexture) {
        std::cout << "Пытаемся загрузить текстуру: " << mesh.MeshMaterial.map_Kd << std::endl;
        meshData.textureID = LoadTexture(mesh.MeshMaterial.map_Kd);
    }
    else {
        meshData.textureID = 0;
    }

    return meshData;
}

// Установка материала и текстуры
void SetMaterial(unsigned int shaderProgram, const MeshData& meshData) {
    const auto& material = meshData.material;

    glUniform3f(glGetUniformLocation(shaderProgram, "material_Kd"),
        material.Kd.X, material.Kd.Y, material.Kd.Z);
    glUniform3f(glGetUniformLocation(shaderProgram, "material_Ka"),
        material.Ka.X, material.Ka.Y, material.Ka.Z);
    glUniform3f(glGetUniformLocation(shaderProgram, "material_Ks"),
        material.Ks.X, material.Ks.Y, material.Ks.Z);
    glUniform1f(glGetUniformLocation(shaderProgram, "material_Ns"),
        material.Ns > 0 ? material.Ns : 32.0f);

    // Устанавливаем текстуру
    if (meshData.hasTexture && meshData.textureID != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, meshData.textureID);
        glUniform1i(glGetUniformLocation(shaderProgram, "diffuseTexture"), 0);
        glUniform1i(glGetUniformLocation(shaderProgram, "useTexture"), 1);
    }
    else {
        glUniform1i(glGetUniformLocation(shaderProgram, "useTexture"), 0);
    }
}

int main() {
    // Инициализация GLFW
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1920, 1080, "OBJ Loader with Shadows", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) return -1;

    // Создаем shadow map
    std::cout << "Создание shadow map..." << std::endl;
    ShadowMap shadowMap = CreateShadowMap(2048, 2048);

    // Создаем отладочный квадрат
    DebugQuad debugQuad = CreateDebugQuad();

    // ЗАГРУЗКА МОДЕЛИ
    objl::Loader loader;
    std::cout << "=== ЗАГРУЗКА МОДЕЛИ ===" << std::endl;
    if (!loader.LoadFile("obj/GTR.obj")) {
        std::cout << "Не удалось загрузить модель!" << std::endl;
        return -1;
    }

    objl::Loader loader1;
    std::cout << "=== ЗАГРУЗКА МОДЕЛИ ===" << std::endl;
    if (!loader1.LoadFile("obj/GTR.obj")) {
        std::cout << "Не удалось загрузить модель!" << std::endl;
        return -1;
    }
    objl::Loader loader2;
    std::cout << "=== ЗАГРУЗКА МОДЕЛИ ===" << std::endl;
    if (!loader2.LoadFile("obj/table.obj")) {
        std::cout << "Не удалось загрузить модель!" << std::endl;
        return -1;
    }

    // Создаем шейдерную программу
    unsigned int shaderProgram = CreateShaderProgram();
    unsigned int shaderProgram1 = CreateShaderProgram();
    unsigned int shaderProgram2 = CreateShaderProgram();

    // Создаем меши с текстурами
    std::vector<MeshData> meshes;
    for (const auto& mesh : loader.LoadedMeshes) {
        meshes.push_back(SetupMesh(mesh));
    }
    std::vector<MeshData> meshes1;
    for (const auto& mesh : loader1.LoadedMeshes) {
        meshes1.push_back(SetupMesh(mesh));
    }
    std::vector<MeshData> meshes2;
    for (const auto& mesh : loader2.LoadedMeshes) {
        meshes2.push_back(SetupMesh(mesh));
    }

    glEnable(GL_DEPTH_TEST);

    // Переменные для управления
    float objectPosX = 0.0f;
    float objectPosY = 0.67f;
    float objectPosZ = 0.0f;
    float objectScale = 0.2f;
    float objectRotate = 0.0f;

    float object1PosX = 0.0f;
    float object1PosY = 0.0f;
    float object1PosZ = 0.0f;
    float object1Scale = 0.2f;
    float object1Rotate = 0.0f;

    float object2PosX = -1.0f;
    float object2PosY = -2.5f;
    float object2PosZ = 0.0f;
    float object2Scale = 1.0f;
    float object2Rotate = 180.0f;

    // ИСПРАВЛЕННЫЕ ПАРАМЕТРЫ СВЕТА - делаем свет ближе к объектам
    float globalLightPosX = 5.0f;  // Ближе к сцене
    float globalLightPosY = 8.0f;  // Ниже чем было
    float globalLightPosZ = 5.0f;  // Ближе к сцене

    // Переменные для камеры
    glm::vec3 cameraPos = glm::vec3(3.0f, 2.0f, 3.0f);
    glm::vec3 cameraFront = glm::vec3(-0.6f, -0.4f, -0.6f);
    glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
    float cameraSpeed = 0.001f;
    glm::vec3 carDirection = glm::vec3(0.0f, 0.0f, 1.0f);
    int currentDirection = 1;

    bool showDebugQuad = false; // Переключатель для отладочного отображения

    // Основной цикл рендеринга
    while (!glfwWindowShouldClose(window)) {
        // 1. РЕНДЕРИНГ В SHADOW MAP
        glViewport(0, 0, shadowMap.width, shadowMap.height);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowMap.FBO);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);

        // Настраиваем матрицу источника света - УВЕЛИЧИВАЕМ ОБЛАСТЬ ВИДИМОСТИ
        glm::mat4 lightProjection = glm::ortho(-15.0f, 15.0f, -15.0f, 15.0f, 1.0f, 30.0f);
        glm::mat4 lightView = glm::lookAt(
            glm::vec3(globalLightPosX, globalLightPosY, globalLightPosZ),
            glm::vec3(0.0f, 0.0f, 0.0f),  // Смотрим в центр сцены
            glm::vec3(0.0f, 1.0f, 0.0f)
        );
        glm::mat4 lightSpaceMatrix = lightProjection * lightView;

        // Матрицы для объектов
        glm::mat4 modelMat = glm::mat4(1.0f);
        modelMat = glm::translate(modelMat, glm::vec3(objectPosX, objectPosY, objectPosZ));
        modelMat = glm::scale(modelMat, glm::vec3(objectScale));
        modelMat = glm::rotate(modelMat, glm::radians(objectRotate + 180.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        glm::mat4 modelMat1 = glm::mat4(1.0f);
        modelMat1 = glm::translate(modelMat1, glm::vec3(object1PosX, object1PosY, object1PosZ));
        modelMat1 = glm::scale(modelMat1, glm::vec3(object1Scale));
        modelMat1 = glm::rotate(modelMat1, glm::radians(object1Rotate), glm::vec3(0.0f, 1.0f, 0.0f));

        glm::mat4 modelMat2 = glm::mat4(1.0f);
        modelMat2 = glm::translate(modelMat2, glm::vec3(object2PosX, object2PosY, object2PosZ));
        modelMat2 = glm::scale(modelMat2, glm::vec3(object2Scale));
        modelMat2 = glm::rotate(modelMat2, glm::radians(object2Rotate), glm::vec3(0.0f, 1.0f, 0.0f));

        // Рендерим все объекты в shadow map
        glUseProgram(shadowMap.shaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(shadowMap.shaderProgram, "lightSpaceMatrix"),
            1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));

        // Первый объект
        glUniformMatrix4fv(glGetUniformLocation(shadowMap.shaderProgram, "model"),
            1, GL_FALSE, glm::value_ptr(modelMat));
        for (int i = 0; i < meshes.size(); i++) {
            glBindVertexArray(meshes[i].VAO);
            glDrawElements(GL_TRIANGLES, meshes[i].indexCount, GL_UNSIGNED_INT, 0);
        }

        // Второй объект
        glUniformMatrix4fv(glGetUniformLocation(shadowMap.shaderProgram, "model"),
            1, GL_FALSE, glm::value_ptr(modelMat1));
        for (int i = 0; i < meshes1.size(); i++) {
            glBindVertexArray(meshes1[i].VAO);
            glDrawElements(GL_TRIANGLES, meshes1[i].indexCount, GL_UNSIGNED_INT, 0);
        }

        // Третий объект
        glUniformMatrix4fv(glGetUniformLocation(shadowMap.shaderProgram, "model"),
            1, GL_FALSE, glm::value_ptr(modelMat2));
        for (int i = 0; i < meshes2.size(); i++) {
            glBindVertexArray(meshes2[i].VAO);
            glDrawElements(GL_TRIANGLES, meshes2[i].indexCount, GL_UNSIGNED_INT, 0);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // 2. ОСНОВНОЙ РЕНДЕРИНГ
        glViewport(0, 0, 1920, 1080);
        glClearColor(16.0f / 255.0f, 122.0f / 255.0f, 176.0f / 255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Матрицы камеры
        glm::mat4 view = glm::lookAt(
            cameraPos,
            cameraPos + cameraFront,
            cameraUp
        );
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), 1920.0f / 1080.0f, 0.1f, 100.0f);

        if (!showDebugQuad) {
            // Функция для рендеринга объекта с тенями
            auto RenderObject = [&](unsigned int shaderProgram, const std::vector<MeshData>& meshes,
                const glm::mat4& modelMatrix) {
                    glUseProgram(shaderProgram);

                    // Устанавливаем uniform'ы
                    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(modelMatrix));
                    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
                    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "lightSpaceMatrix"), 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));

                    glUniform3f(glGetUniformLocation(shaderProgram, "lightPos"), globalLightPosX, globalLightPosY, globalLightPosZ);
                    glUniform3f(glGetUniformLocation(shaderProgram, "viewPos"), cameraPos.x, cameraPos.y, cameraPos.z);
                    glUniform3f(glGetUniformLocation(shaderProgram, "lightColor"), 1.0f, 1.0f, 1.0f);

                    // Привязываем shadow map
                    glActiveTexture(GL_TEXTURE1);
                    glBindTexture(GL_TEXTURE_2D, shadowMap.depthMap);
                    glUniform1i(glGetUniformLocation(shaderProgram, "shadowMap"), 1);

                    // Рендерим меши
                    for (int i = 0; i < meshes.size(); i++) {
                        SetMaterial(shaderProgram, meshes[i]);
                        glBindVertexArray(meshes[i].VAO);
                        glDrawElements(GL_TRIANGLES, meshes[i].indexCount, GL_UNSIGNED_INT, 0);
                    }
                };

            // === ПЕРВЫЙ ОБЪЕКТ ===
            RenderObject(shaderProgram, meshes, modelMat);

            // === ВТОРОЙ ОБЪЕКТ ===
            RenderObject(shaderProgram1, meshes1, modelMat1);

            // === ТРЕТИЙ ОБЪЕКТ ===
            RenderObject(shaderProgram2, meshes2, modelMat2);
        }
        else {
            // ОТЛАДОЧНОЕ ОТОБРАЖЕНИЕ SHADOW MAP
            glDisable(GL_DEPTH_TEST);
            glUseProgram(debugQuad.shaderProgram);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, shadowMap.depthMap);
            glBindVertexArray(debugQuad.VAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glEnable(GL_DEPTH_TEST);
        }

        // УПРАВЛЕНИЕ
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        {
            if (currentDirection != 1)
            {
                float targetRotation = 0.0f;
                objectRotate = targetRotation + 180.0f;
                carDirection = glm::vec3(0.0f, 0.0f, 1.0f);
            }
            currentDirection = 1;

            if (objectPosZ < 1.2535)
            {
                objectPosZ += 0.0005f;
            }
        }
        else if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        {
            if (currentDirection != 2)
            {
                float targetRotation = 180.0f;
                objectRotate = targetRotation + 180.0f;
                carDirection = glm::vec3(0.0f, 0.0f, -1.0f);
            }
            currentDirection = 2;
            if (objectPosZ > -1.1283)
            {
                objectPosZ -= 0.0005f;
            }
        }
        else if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        {
            if (currentDirection != 3)
            {
                float targetRotation = 90.0f;
                objectRotate = targetRotation;
                carDirection = glm::vec3(-1.0f, 0.0f, 0.0f);
            }
            currentDirection = 3;
            if (objectPosX > -3.21036f)
            {
                objectPosX -= 0.0005f;
            }
        }
        else if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        {
            if (currentDirection != 4)
            {
                float targetRotation = -90.0f;
                objectRotate = targetRotation;
                carDirection = glm::vec3(1.0f, 0.0f, 0.0f);
            }
            currentDirection = 4;
            if (objectPosX < 1.41899f)
            {
                objectPosX += 0.0005f;
            }
        }

        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
            cameraPos += cameraSpeed * cameraFront;
        }
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
            cameraPos -= cameraSpeed * cameraFront;
        }
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
            cameraPos += cameraSpeed * glm::normalize(glm::cross(cameraFront, cameraUp));
        }
        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
            cameraPos -= cameraSpeed * glm::normalize(glm::cross(cameraFront, cameraUp));
        }

        // Переключение отладочного режима
        if (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS) {
            showDebugQuad = !showDebugQuad;
            // Задержка чтобы избежать множественных переключений
            glfwWaitEventsTimeout(0.3);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Очистка ресурсов
    glDeleteFramebuffers(1, &shadowMap.FBO);
    glDeleteTextures(1, &shadowMap.depthMap);
    glDeleteProgram(shadowMap.shaderProgram);

    return 0;
}