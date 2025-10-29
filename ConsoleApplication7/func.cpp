#include "func.h"
#include "Globals.h"
string LoadShader(const char* filename)
{
	string res;
	ifstream file(filename, ios::in);
	if (file.is_open())
	{
		std::stringstream sstr;
		sstr << file.rdbuf();
		res = sstr.str();
		file.close();
	}
	return res;
}
GLFWwindow* InitAll(int w, int h, bool Fullscreen)
{
	int WinWidth = w;
	int WinHeight = h;
	GLFWwindow* window = nullptr;
	if (!glfwInit())
	{
		cerr << "ERROR: could not start GLFW3\n";
		exit(-1);
	}
	if (Fullscreen)
	{
		GLFWmonitor* mon = glfwGetPrimaryMonitor();
		const GLFWvidmode* vmode = glfwGetVideoMode(mon);
		WinWidth = vmode->width;
		WinHeight = vmode->height;
		window = glfwCreateWindow(WinWidth, WinHeight, "Capybara OpenGL", mon, NULL);
	}
	else
		window = glfwCreateWindow(WinWidth, WinHeight, "Capybara OpenGL", NULL, NULL);
	glfwMakeContextCurrent(window);
	glewExperimental = GL_TRUE;
	if (glewInit() != GLEW_OK)
	{
		cerr << "ERROR: could not start GLEW\n";
		return nullptr;
	}
	return window;
}
void EndAll()
{
	glfwTerminate();
}