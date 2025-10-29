#pragma once
#pragma once
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <random>      // для random_device, mt19937, uniform_real_distribution
#include <glm/glm.hpp> // для vec3 (или другой математической библиотеки)
using glm::vec3;
using namespace std;
class Model
{
public:
	Model(GLFWwindow* w) {
		glGenVertexArrays(1, &vao);
		window = w;
	};
	~Model() {};
	void render(GLuint mode = GL_TRIANGLES);
	void load_coords(glm::vec3* verteces, size_t count);
	void load_colors(glm::vec3* colors, size_t count);
	void load_indices(GLuint* indices, size_t count);
	void load_shaders(const char* vect, const char* frag);
	void render1(glm::mat4 MVPMatrix, GLuint mode);
private:
	GLuint vao = -1;
	size_t verteces_count = 0;
	size_t indices_count = 0;
	GLuint shader_programme = -1;
	GLFWwindow* window;
};
vec3 randomColor();