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

// Шейдеры с поддержкой текстур
const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = aNormal;
    TexCoord = aTexCoord;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

uniform vec3 material_Kd;
uniform vec3 material_Ka;
uniform vec3 material_Ks;
uniform float material_Ns;

uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 lightColor;

uniform sampler2D diffuseTexture;
uniform bool useTexture;

void main() {
    // Базовый цвет - из текстуры или из материала
    vec3 baseColor;
    if (useTexture) {
        baseColor = texture(diffuseTexture, TexCoord).rgb;
    } else {
        baseColor = material_Kd;
    }
    
    // Ambient
    float ambientStrength = 0.1;
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
    
    vec3 result = ambient + diffuse + specular;
    FragColor = vec4(result, 1.0);
}
)";

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
    } else {
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
};

// Создание буферов для меша с учетом текстур
MeshData SetupMesh(const objl::Mesh& mesh) {
    MeshData meshData;
    meshData.material = mesh.MeshMaterial;
    meshData.name = mesh.MeshName;
    
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
    } else {
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
    } else {
        glUniform1i(glGetUniformLocation(shaderProgram, "useTexture"), 0);
    }
}

int main() {
    // Инициализация GLFW
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1920, 1080, "OBJ Loader with Textures", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) return -1;

    // ЗАГРУЗКА МОДЕЛИ
    objl::Loader loader;
    std::cout << "=== ЗАГРУЗКА МОДЕЛИ ===" << std::endl;
    if (!loader.LoadFile("obj/GTR.obj")) {
        std::cout << "Не удалось загрузить модель!" << std::endl;
        return -1;
    }

    objl::Loader loader1;
    std::cout << "=== ЗАГРУЗКА МОДЕЛИ ===" << std::endl;
    if (!loader1.LoadFile("obj/rv_lamp_post_4.obj")) {
        std::cout << "Не удалось загрузить модель!" << std::endl;
        return -1;
    }
    objl::Loader loader2;
    std::cout << "=== ЗАГРУЗКА МОДЕЛИ ===" << std::endl;
    if (!loader2.LoadFile("obj/rv_lamp_post_4.obj")) {
        std::cout << "Не удалось загрузить модель!" << std::endl;
        return -1;
    }

    for (int i = 0; i < loader.LoadedMeshes.size(); i++) {
        const auto& mesh = loader.LoadedMeshes[i];
    }
    for (int i = 0; i < loader1.LoadedMeshes.size(); i++) {
        const auto& mesh = loader1.LoadedMeshes[i];
    }
    for (int i = 0; i < loader2.LoadedMeshes.size(); i++) {
        const auto& mesh = loader2.LoadedMeshes[i];
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
    float objectPosY = -0.26f;
    float objectPosZ = 0.0f;
    float objectScale = 0.2f;
    float objectRotate = 0.0f;

    float object1PosX = -1.25f;
    float object1PosY = -0.45f;
    float object1PosZ = 0.0f;
    float object1Scale = 0.07f;
    float object1Rotate = 0.0f;

    float object2PosX = 1.25f;
    float object2PosY = -0.45f;
    float object2PosZ = 0.0f;
    float object2Scale = 0.07f;
    float object2Rotate = 180.0f;

    // Переменные для камеры
    glm::vec3 cameraPos = glm::vec3(3.0f, 2.0f, 3.0f);
    glm::vec3 cameraFront = glm::vec3(-0.6f, -0.4f, -0.6f);
    glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
    float cameraSpeed = 0.001f;
    // Основной цикл рендеринга
    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Матрицы преобразований
        float time = glfwGetTime();
        glm::mat4 modelMat = glm::mat4(1.0f);
        modelMat = glm::translate(modelMat, glm::vec3(objectPosX, objectPosY, objectPosZ));
        modelMat = glm::scale(modelMat, glm::vec3(objectScale));
        modelMat = glm::rotate(modelMat, glm::radians(objectRotate), glm::vec3(0.0f, 1.0f, 0.0f));

        glm::mat4 modelMat1 = glm::mat4(1.0f);
        modelMat1 = glm::translate(modelMat1, glm::vec3(object1PosX, object1PosY, object1PosZ));
        modelMat1 = glm::scale(modelMat1, glm::vec3(object1Scale));
        modelMat1 = glm::rotate(modelMat1, glm::radians(object1Rotate), glm::vec3(0.0f, 1.0f, 0.0f));

        glm::mat4 modelMat2 = glm::mat4(1.0f);
        modelMat2 = glm::translate(modelMat2, glm::vec3(object2PosX, object2PosY, object2PosZ));
        modelMat2 = glm::scale(modelMat2, glm::vec3(object2Scale));
        modelMat2 = glm::rotate(modelMat2, glm::radians(object2Rotate), glm::vec3(0.0f, 1.0f, 0.0f));

        glm::mat4 view = glm::lookAt(
            cameraPos,                    // позиция камеры
            cameraPos + cameraFront,      // направление взгляда
            cameraUp                      // вектор "вверх"
        );

        glm::mat4 projection = glm::perspective(glm::radians(45.0f), 1920.0f / 1080.0f, 0.1f, 100.0f);

        // === ПЕРВЫЙ ОБЪЕКТ ===
        glUseProgram(shaderProgram);

        // Устанавливаем uniform'ы для ПЕРВОГО шейдера (ПОСЛЕ glUseProgram)
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(modelMat));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3f(glGetUniformLocation(shaderProgram, "lightPos"), 2.0f, 5.0f, 2.0f);
        glUniform3f(glGetUniformLocation(shaderProgram, "viewPos"), 3.0f, 2.0f, 3.0f);
        glUniform3f(glGetUniformLocation(shaderProgram, "lightColor"), 1.0f, 1.0f, 1.0f);

        // Рендерим ПЕРВЫЙ объект
        for (int i = 0; i < meshes.size(); i++) {
            SetMaterial(shaderProgram, meshes[i]);
            glBindVertexArray(meshes[i].VAO);
            glDrawElements(GL_TRIANGLES, loader.LoadedMeshes[i].Indices.size(), GL_UNSIGNED_INT, 0);
        }

        // === ВТОРОЙ ОБЪЕКТ ===
        glUseProgram(shaderProgram1);

        // Устанавливаем uniform'ы для ВТОРОГО шейдера (ПОСЛЕ glUseProgram)
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram1, "model"), 1, GL_FALSE, glm::value_ptr(modelMat1));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram1, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram1, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3f(glGetUniformLocation(shaderProgram1, "lightPos"), 2.0f, 5.0f, 2.0f);
        glUniform3f(glGetUniformLocation(shaderProgram1, "viewPos"), 3.0f, 2.0f, 3.0f);
        glUniform3f(glGetUniformLocation(shaderProgram1, "lightColor"), 1.0f, 1.0f, 1.0f);
        // Рендерим ВТОРОЙ объект
        for (int i = 0; i < meshes1.size(); i++) {
            SetMaterial(shaderProgram1, meshes1[i]);
            glBindVertexArray(meshes1[i].VAO);
            glDrawElements(GL_TRIANGLES, loader1.LoadedMeshes[i].Indices.size(), GL_UNSIGNED_INT, 0);
        }

        // === ТРЕТИЙ ОБЪЕКТ ===
        glUseProgram(shaderProgram2);

        // Устанавливаем uniform'ы для ТРЕТЬЕГО шейдера (ПОСЛЕ glUseProgram)
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram2, "model"), 1, GL_FALSE, glm::value_ptr(modelMat2));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram2, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram2, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3f(glGetUniformLocation(shaderProgram2, "lightPos"), 2.0f, 5.0f, 2.0f);
        glUniform3f(glGetUniformLocation(shaderProgram2, "viewPos"), 3.0f, 2.0f, 3.0f);
        glUniform3f(glGetUniformLocation(shaderProgram2, "lightColor"), 1.0f, 1.0f, 1.0f);

        // Рендерим ТРЕТИЙ объект
        for (int i = 0; i < meshes2.size(); i++) {
            SetMaterial(shaderProgram2, meshes2[i]);
            glBindVertexArray(meshes2[i].VAO);
            glDrawElements(GL_TRIANGLES, loader2.LoadedMeshes[i].Indices.size(), GL_UNSIGNED_INT, 0);
        }

        // Управление
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
            objectScale -= 0.001f;
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
            objectScale += 0.001f;
        //управление
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            objectPosZ += 0.0005f;
            objectRotate += 90.0f;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            objectPosZ -= 0.0005f;
            objectRotate -= 90.0f;  
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            objectPosX += 0.0005f;
            objectRotate += 90.0f;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            objectPosX -= 0.0005f;
            objectRotate -= 90.0f;
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
            cameraPos += cameraSpeed * cameraFront;  // приближение
        }
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
            cameraPos -= cameraSpeed * cameraFront;  // отдаление
        }
        // Дополнительное управление камерой (опционально)
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
            cameraPos += cameraSpeed * glm::normalize(glm::cross(cameraFront, cameraUp));  // вправо
        }
        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
            cameraPos -= cameraSpeed * glm::normalize(glm::cross(cameraFront, cameraUp));  // влево
        }
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    return 0;
}