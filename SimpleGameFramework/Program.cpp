#include "Program.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include "Common.h"

sgf::SimpleProgram::SimpleProgram()
{

}

sgf::SimpleProgram::~SimpleProgram()
{

}

void sgf::SimpleProgram::LoadFromFile(const char* vertexShader, const char* fragmentShader)
{
	unsigned int vertexShaderUnit = LoadShader(vertexShader,SHADER_VERTEX);
	unsigned int fragmentShaderUnit = LoadShader(fragmentShader,SHADER_FRAGMENT);
	Link(vertexShaderUnit, fragmentShaderUnit);
}

void sgf::SimpleProgram::LoadFromSource(const char* vertexShader, const char* fragmentShader)
{
	unsigned int vertexShaderUnit = LoadShaderSource(vertexShader, SHADER_VERTEX, "embedded vertex shader");
	unsigned int fragmentShaderUnit = LoadShaderSource(fragmentShader, SHADER_FRAGMENT, "embedded fragment shader");
	Link(vertexShaderUnit, fragmentShaderUnit);
}

void sgf::SimpleProgram::Link(unsigned int vertexShaderUnit, unsigned int fragmentShaderUnit)
{
	mProgram = glCreateProgram();
	GL_CALL(glAttachShader(mProgram, vertexShaderUnit));
	GL_CALL(glAttachShader(mProgram, fragmentShaderUnit));

	GL_CALL(glBindAttribLocation(mProgram, 0, "aPosition"));
	GL_CALL(glBindAttribLocation(mProgram, 1, "aColor"));
	GL_CALL(glBindAttribLocation(mProgram, 2, "aTexCoord"));
	GL_CALL(glBindAttribLocation(mProgram, 3, "aTextureIndex"));
	GL_CALL(glBindAttribLocation(mProgram, 4, "aMatrixIndex"));

	GLint success = 0;
	GLchar errorLog[1024] = { 0 };
	GL_CALL(glLinkProgram(mProgram));

	glGetProgramiv(mProgram, GL_LINK_STATUS, &success);
	if (success == 0) {
		glGetProgramInfoLog(mProgram, sizeof(errorLog), NULL, errorLog);
		fprintf(stderr, "Error linking shader program: '%s'\n", errorLog);
		exit(1);
	}

	glDeleteShader(vertexShaderUnit);
	glDeleteShader(fragmentShaderUnit);
}

unsigned int sgf::SimpleProgram::LoadShader(const char* path,ShaderType type)
{
	std::ifstream shaderFile(path, std::ios::in | std::ios::binary);
	if (!shaderFile) {
		fprintf(stderr, "Unable to open shader file: '%s'\n", path);
		exit(1);
	}

	std::ostringstream shaderBuffer;
	shaderBuffer << shaderFile.rdbuf();
	const std::string shaderSource = shaderBuffer.str();
	return LoadShaderSource(shaderSource.c_str(), type, path);
}


unsigned int sgf::SimpleProgram::LoadShaderSource(const char* source, ShaderType type, const char* sourceName)
{
	unsigned int shaderType;
	if(type == SHADER_VERTEX)
		shaderType = GL_VERTEX_SHADER;
	else
		shaderType = GL_FRAGMENT_SHADER;

	unsigned int shaderUnit = glCreateShader(shaderType);

	GL_CALL(glShaderSource(shaderUnit, 1, &source, NULL));

	GL_CALL(glCompileShader(shaderUnit));
	int success = 0;
	glGetShaderiv(shaderUnit, GL_COMPILE_STATUS, &success);

	if (!success) {
		GLchar InfoLog[1024] = { 0 };
		glGetShaderInfoLog(shaderUnit, 1024, NULL, InfoLog);
		fprintf(stderr, "Error compiling %s: '%s'\n", sourceName, InfoLog);
		glDeleteShader(shaderUnit);
		exit(1);
	}

	return shaderUnit;
}

void sgf::SimpleProgram::Use()
{
	glUseProgram(mProgram);
}


void sgf::Vertex::InformationInit()
{
	glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),0);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(7 * sizeof(float)));
	glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(9 * sizeof(float)));
	glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(10 * sizeof(float)));
	
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);
	glEnableVertexAttribArray(3);
	glEnableVertexAttribArray(4);
}
