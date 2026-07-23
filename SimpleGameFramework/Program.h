#pragma once
#ifndef __SIMPLE_PROGRAM__
#define __SIMPLE_PROGRAM__
#include "Common.h"

namespace sgf {
	class SimpleProgram;

	struct Vertex {
		float x, y, z;
		float r, g, b, a;
		float u, v;
		float texIndex;
		float matrixIndex;
		static void InformationInit();
	};

	class SimpleProgram {
	public:
		enum ShaderType {
			SHADER_VERTEX,
			SHADER_FRAGMENT,
		};
	public:
		unsigned int mProgram = 0;

	public:
		SimpleProgram();
		~SimpleProgram();

		void LoadFromFile(const char* vertexShader,const char* fragmentShader);
		void LoadFromSource(const char* vertexShader, const char* fragmentShader);

		unsigned int LoadShader(const char* path, ShaderType type);
		unsigned int LoadShaderSource(const char* source, ShaderType type, const char* sourceName);
		void Use();

	private:
		void Link(unsigned int vertexShaderUnit, unsigned int fragmentShaderUnit);
	};
}

#endif // !__SIMPLE_PROGRAM__
