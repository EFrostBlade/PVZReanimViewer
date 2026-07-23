#include "GameApp.h"
#include "Constant.h"
#include "Graphics.h"
#include "ReanimViewer.h"

#include "MainScreen.h"

#undef main

ViewerApp* gApp;

sgf::Graphics* gGraphics;

int main(int argc, char* argv[]) {
	gApp = new ViewerApp();
	gGraphics = gApp->LoadGraphics();

	if (argc > 1) {
		std::vector<sgf::String> extraImageDirs;
		for (int i = 2; i < argc; ++i) {
			extraImageDirs.push_back(argv[i]);
		}
		if (!gApp->LoadReanim(argv[1], extraImageDirs)) {
			std::cerr << gApp->mLoadStatus << std::endl;
			delete gApp;
			gApp = nullptr;
			gGraphics = nullptr;
			return EXIT_FAILURE;
		}
	}

	gApp->EnterMainLoop();
	
	delete gApp;
	return EXIT_SUCCESS;
}

