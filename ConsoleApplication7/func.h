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
//�������� ��������
string LoadShader(const char* filename);
//�������� ����
GLFWwindow* InitAll(int w, int h, bool Fullscreen);
//���������� ������
void EndAll();