#include "Model.h"
#include "func.h"
#include "globals.h"
#include <vector>
#include <algorithm>
#include <random>      // для random_device, mt19937, uniform_real_distribution
#include <glm/glm.hpp> // для vec3 (или другой математической библиотеки)
using glm::vec3;
void Model::render(GLuint mode)
{
	glUseProgram(shader_programme);
	glBindVertexArray(vao);
	if (indices_count > 0)
		glDrawElements(mode, indices_count, GL_UNSIGNED_INT, 0);
	else
		glDrawArrays(mode, 0, verteces_count);
}
void Model::render1(glm::mat4 MVPMatrix, GLuint mode)
{
	//Более сложный вариант - с использованием  
	//матрицы преобразований 
	//Сначала всегда выбираем программу 
	glUseProgram(shader_programme);
	//Теперь надо поискать в ней матрицу 
	GLuint MVP = glGetUniformLocation(shader_programme, "MVP");
	//Как нашли - передаём в неё значения 
	glUniformMatrix4fv(MVP, 1, GL_FALSE, &MVPMatrix[0][0]);
	//А дальше всё так же, как и в простом случае
	glBindVertexArray(vao);
	if (indices_count > 0)
		glDrawElements(mode, indices_count, GL_UNSIGNED_INT, 0);
	else
		glDrawArrays(mode, 0, verteces_count);
}
void Model::load_coords(glm::vec3* verteces, size_t count)
{
	verteces_count = count;
	GLuint coords_vbo = 0;
	glGenBuffers(1, &coords_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, coords_vbo);
	glBufferData(GL_ARRAY_BUFFER, count * sizeof(glm::vec3), verteces, GL_STATIC_DRAW);
	glBindVertexArray(vao);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
	glEnableVertexAttribArray(0);
}
void Model::load_colors(glm::vec3* colors, size_t count)
{
	GLuint colors_vbo = 0;
	glGenBuffers(1, &colors_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, colors_vbo);
	glBufferData(GL_ARRAY_BUFFER, count * sizeof(glm::vec3), colors, GL_STATIC_DRAW);
	glBindVertexArray(vao);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, NULL);
	glEnableVertexAttribArray(1);
}
void Model::load_indices(GLuint* indices, size_t count)
{
	indices_count = count;
	glBindVertexArray(vao);
	GLuint elementbuffer;
	glGenBuffers(1, &elementbuffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elementbuffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(GLuint), indices,
		GL_STATIC_DRAW);
}
void Model::load_shaders(const char* vect, const char* frag)
{
	GLint result = GL_FALSE;
	int infoLogLength;
	shader_programme = glCreateProgram();
	string vstext = LoadShader(vect);
	const char* vertex_shader = vstext.c_str();
	string fstext = LoadShader(frag);
	const char* fragment_shader = fstext.c_str();
	GLuint vs = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vs, 1, &vertex_shader, NULL);
	glCompileShader(vs);
	glGetShaderiv(vs, GL_COMPILE_STATUS, &result);
	glGetShaderiv(vs, GL_INFO_LOG_LENGTH, &infoLogLength);
	if (infoLogLength > 0)
	{
		char* errorMessage = new char[infoLogLength + 1];
		glGetShaderInfoLog(vs, infoLogLength, NULL, errorMessage);
		std::cout << errorMessage;
		delete errorMessage;
	}
	GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fs, 1, &fragment_shader, NULL);
	glCompileShader(fs);
	glGetShaderiv(fs, GL_COMPILE_STATUS, &result);
	glGetShaderiv(fs, GL_INFO_LOG_LENGTH, &infoLogLength);
	if (infoLogLength > 0)
	{
		char* errorMessage = new char[infoLogLength + 1];
		glGetShaderInfoLog(fs, infoLogLength, NULL, errorMessage);
		std::cout << errorMessage;
		delete errorMessage;
	}
	glAttachShader(shader_programme, vs);
	glAttachShader(shader_programme, fs);
	glBindAttribLocation(shader_programme, 0, "vertex_position");
	glBindAttribLocation(shader_programme, 1, "vertex_color");
	glLinkProgram(shader_programme);
}
vec3 randomColor() {
	static random_device rd;
	static mt19937 gen(rd());
	static uniform_real_distribution<float> dis(0.0f, 1.0f);
	return vec3(dis(gen), dis(gen), dis(gen));
}