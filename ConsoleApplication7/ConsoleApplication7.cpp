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

    GLFWwindow* window = glfwCreateWindow(1200, 800, "OBJ Loader with Textures", nullptr, nullptr);
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

    // Отладочная информация
    std::cout << "Загружено мешей: " << loader.LoadedMeshes.size() << std::endl;
    std::cout << "Загружено материалов: " << loader.LoadedMaterials.size() << std::endl;

    for (int i = 0; i < loader.LoadedMeshes.size(); i++) {
        const auto& mesh = loader.LoadedMeshes[i];
        std::cout << "Меш " << i << ": '" << mesh.MeshName << "'"
            << " -> Материал: '" << mesh.MeshMaterial.name << "'"
            << " -> Текстура: '" << mesh.MeshMaterial.map_Kd << "'" << std::endl;
    }

    // Создаем шейдерную программу
    unsigned int shaderProgram = CreateShaderProgram();

    // Создаем меши с текстурами
    std::vector<MeshData> meshes;
    for (const auto& mesh : loader.LoadedMeshes) {
        meshes.push_back(SetupMesh(mesh));
    }

    glEnable(GL_DEPTH_TEST);

    // Переменные для управления
    float objectPosX = 0.0f;
    float objectPosY = 0.0f;
    float objectPosZ = 0.0f;
    float objectScale = 0.001f;

    // Основной цикл рендеринга
    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shaderProgram);

        // Матрицы преобразований
        float time = glfwGetTime();
        glm::mat4 modelMat = glm::mat4(1.0f);
        modelMat = glm::translate(modelMat, glm::vec3(objectPosX, objectPosY, objectPosZ));
        modelMat = glm::scale(modelMat, glm::vec3(0.5));
        modelMat = glm::rotate(modelMat, time * glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        glm::mat4 view = glm::lookAt(
            glm::vec3(3.0f, 2.0f, 3.0f),
            glm::vec3(0.0f, objectPosY + 0.5f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f)
        );

        glm::mat4 projection = glm::perspective(glm::radians(45.0f), 1200.0f / 800.0f, 0.1f, 100.0f);

        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(modelMat));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

        // Освещение
        glUniform3f(glGetUniformLocation(shaderProgram, "lightPos"), 2.0f, 5.0f, 2.0f);
        glUniform3f(glGetUniformLocation(shaderProgram, "viewPos"), 3.0f, 2.0f, 3.0f);
        glUniform3f(glGetUniformLocation(shaderProgram, "lightColor"), 1.0f, 1.0f, 1.0f);

        // Рендерим каждый меш
        for (int i = 0; i < meshes.size(); i++) {
            SetMaterial(shaderProgram, meshes[i]);
            glBindVertexArray(meshes[i].VAO);
            glDrawElements(GL_TRIANGLES, loader.LoadedMeshes[i].Indices.size(), GL_UNSIGNED_INT, 0);
        }

        // Управление
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
            objectPosY -= 0.05f;
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
            objectPosY += 0.05f;
        if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS)
            objectScale -= 0.05f;
        if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS)
            objectScale += 0.05f;
        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
            objectPosY = -1.0f;
            objectScale = 0.3f;
        }

        // Ограничиваем масштаб
        if (objectScale < 0.1f) objectScale = 0.1f;
        if (objectScale > 3.0f) objectScale = 3.0f;

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Очистка
    for (auto& mesh : meshes) {
        glDeleteVertexArrays(1, &mesh.VAO);
        glDeleteBuffers(1, &mesh.VBO);
        glDeleteBuffers(1, &mesh.EBO);
        if (mesh.textureID != 0) {
            glDeleteTextures(1, &mesh.textureID);
        }
    }
    glDeleteProgram(shaderProgram);
    glfwTerminate();

    return 0;
}