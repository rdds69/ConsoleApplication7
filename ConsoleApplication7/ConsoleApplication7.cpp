#include <iostream>
#include <vector>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "OBJ_Loader.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Шейдеры
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

uniform vec3 material_Kd;  // Diffuse color from MTL
uniform vec3 material_Ka;  // Ambient color from MTL
uniform vec3 material_Ks;  // Specular color from MTL
uniform float material_Ns; // Specular exponent from MTL

uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 lightColor;

void main() {
    // Ambient
    float ambientStrength = 0.1;
    vec3 ambient = material_Ka * lightColor * ambientStrength;
    
    // Diffuse 
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = material_Kd * diff * lightColor;
    
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

// Создание буферов для меша
void SetupMesh(unsigned int& VAO, unsigned int& VBO, unsigned int& EBO, const objl::Mesh& mesh) {
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

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
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
}

// Установка материала из MTL в шейдер
void SetMaterial(unsigned int shaderProgram, const objl::Material& material) {
    // Используем значения из MTL, если они есть, иначе значения по умолчанию
    glUniform3f(glGetUniformLocation(shaderProgram, "material_Kd"),
        material.Kd.X, material.Kd.Y, material.Kd.Z);
    glUniform3f(glGetUniformLocation(shaderProgram, "material_Ka"),
        material.Ka.X, material.Ka.Y, material.Ka.Z);
    glUniform3f(glGetUniformLocation(shaderProgram, "material_Ks"),
        material.Ks.X, material.Ks.Y, material.Ks.Z);
    glUniform1f(glGetUniformLocation(shaderProgram, "material_Ns"),
        material.Ns > 0 ? material.Ns : 32.0f);
}

int main() {
    // Инициализация GLFW
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1200, 800, "OBJ Loader with MTL", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) return -1;

    // ЗАГРУЗКА МОДЕЛИ С ИСПОЛЬЗОВАНИЕМ ГОТОВОГО LOADFILE
    objl::Loader loader;

    std::cout << "=== ЗАГРУЗКА МОДЕЛИ ===" << std::endl;
    if (!loader.LoadFile("OBJ_truck.obj")) { // Замени на свой путь
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
            << " (Kd: " << mesh.MeshMaterial.Kd.X << ", "
            << mesh.MeshMaterial.Kd.Y << ", "
            << mesh.MeshMaterial.Kd.Z << ")" << std::endl;
    }

    // Создаем шейдерную программу
    unsigned int shaderProgram = CreateShaderProgram();

    // Создаем VAO для каждого меша
    std::vector<unsigned int> VAOs, VBOs, EBOs;
    for (const auto& mesh : loader.LoadedMeshes) {
        unsigned int VAO, VBO, EBO;
        SetupMesh(VAO, VBO, EBO, mesh);
        VAOs.push_back(VAO);
        VBOs.push_back(VBO);
        EBOs.push_back(EBO);
    }

    glEnable(GL_DEPTH_TEST);

    // Основной цикл рендеринга
    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shaderProgram);

        // Матрицы преобразований
        float time = glfwGetTime();
        glm::mat4 modelMat = glm::mat4(1.0f);
        modelMat = glm::rotate(modelMat, time * glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        glm::mat4 view = glm::lookAt(
            glm::vec3(3.0f, 2.0f, 3.0f),
            glm::vec3(0.0f, 0.5f, 0.0f),
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

        // Рендерим каждый меш с его материалом из MTL
        for (int i = 0; i < loader.LoadedMeshes.size(); i++) {
            const auto& mesh = loader.LoadedMeshes[i];

            // Устанавливаем материал из MTL
            SetMaterial(shaderProgram, mesh.MeshMaterial);

            // Рендерим меш
            glBindVertexArray(VAOs[i]);
            glDrawElements(GL_TRIANGLES, mesh.Indices.size(), GL_UNSIGNED_INT, 0);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Очистка
    for (int i = 0; i < VAOs.size(); i++) {
        glDeleteVertexArrays(1, &VAOs[i]);
        glDeleteBuffers(1, &VBOs[i]);
        glDeleteBuffers(1, &EBOs[i]);
    }
    glDeleteProgram(shaderProgram);
    glfwTerminate();

    return 0;
}