#pragma once
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
using namespace std;
//Загрузка шейдеров
string LoadShader(const char* filename);
//Создание окна
GLFWwindow* InitAll(int w, int h, bool Fullscreen);
//Завершение работы
void EndAll();