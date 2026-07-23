#include "GameApp.h"
#include "Constant.h"
#include "Graphics.h"
#include "ReanimViewer.h"

#include "MainScreen.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#undef main

ViewerApp* gApp;

sgf::Graphics* gGraphics;

int main(int argc, char* argv[]) {
	gApp = new ViewerApp();
	gGraphics = gApp->LoadGraphics();

	if (argc > 1) {
		std::vector<sgf::String> extraImageDirs;
		sgf::String exportPath;
		int exportWidth = 512;
		int exportHeight = 512;
		int exportFPS = 0;
		float exportScale = 1.0f;
		bool exportTransparent = true;
		auto readOptionValue = [&](int& index, const char* option) -> const char* {
			if (index + 1 >= argc) {
				std::cerr << "Missing value for " << option << std::endl;
				return nullptr;
			}
			return argv[++index];
		};
		for (int i = 2; i < argc; ++i) {
			if (std::strcmp(argv[i], "--export") == 0) {
				const char* value = readOptionValue(i, "--export");
				if (!value) {
					delete gApp;
					return EXIT_FAILURE;
				}
				exportPath = value;
			}
			else if (std::strcmp(argv[i], "--width") == 0) {
				const char* value = readOptionValue(i, "--width");
				if (!value) {
					delete gApp;
					return EXIT_FAILURE;
				}
				exportWidth = std::atoi(value);
			}
			else if (std::strcmp(argv[i], "--height") == 0) {
				const char* value = readOptionValue(i, "--height");
				if (!value) {
					delete gApp;
					return EXIT_FAILURE;
				}
				exportHeight = std::atoi(value);
			}
			else if (std::strcmp(argv[i], "--fps") == 0) {
				const char* value = readOptionValue(i, "--fps");
				if (!value) {
					delete gApp;
					return EXIT_FAILURE;
				}
				exportFPS = std::atoi(value);
			}
			else if (std::strcmp(argv[i], "--scale") == 0) {
				const char* value = readOptionValue(i, "--scale");
				if (!value) {
					delete gApp;
					return EXIT_FAILURE;
				}
				exportScale = std::strtof(value, nullptr);
			}
			else if (std::strcmp(argv[i], "--opaque") == 0) {
				exportTransparent = false;
			}
			else if (std::strncmp(argv[i], "--", 2) == 0) {
				std::cerr << "Unknown option: " << argv[i] << std::endl;
				delete gApp;
				return EXIT_FAILURE;
			}
			else {
				extraImageDirs.push_back(argv[i]);
			}
		}
		if (!gApp->LoadReanim(argv[1], extraImageDirs)) {
			std::cerr << gApp->mLoadStatus << std::endl;
			delete gApp;
			gApp = nullptr;
			gGraphics = nullptr;
			return EXIT_FAILURE;
		}
		if (!exportPath.empty() && !gApp->StartExport(
			exportPath, exportWidth, exportHeight, exportScale,
			exportFPS, exportTransparent, true)) {
			std::cerr << gApp->mExportStatus << std::endl;
			delete gApp;
			gApp = nullptr;
			gGraphics = nullptr;
			return EXIT_FAILURE;
		}
	}

	gApp->EnterMainLoop();
	const bool autoExportFailed = gApp->mAutoExportFailed;
	delete gApp;
	return autoExportFailed ? EXIT_FAILURE : EXIT_SUCCESS;
}

