#include "ReanimViewer.h"
#include "Constant.h"
#include "IconFonts.h"
#include "GamePacker/GamePacker.h"
#include <nfd.h>
#include <imgui_internal.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <new>
#include <sstream>
#include <vector>

namespace {
	char AsciiLower(char value)
	{
		if (value >= 'A' && value <= 'Z')
			return static_cast<char>(value - 'A' + 'a');
		return value;
	}

	bool EqualsNoCase(const sgf::String& left, const sgf::String& right)
	{
		if (left.size() != right.size())
			return false;
		for (size_t i = 0; i < left.size(); ++i) {
			if (AsciiLower(left[i]) != AsciiLower(right[i]))
				return false;
		}
		return true;
	}

	bool StartsWithNoCase(const sgf::String& value, const sgf::String& prefix)
	{
		if (value.size() < prefix.size())
			return false;
		return EqualsNoCase(value.substr(0, prefix.size()), prefix);
	}

	bool EndsWithNoCase(const sgf::String& value, const sgf::String& suffix)
	{
		if (value.size() < suffix.size())
			return false;
		return EqualsNoCase(value.substr(value.size() - suffix.size()), suffix);
	}

	sgf::String NormalizePath(sgf::String path)
	{
		if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
			path = path.substr(1, path.size() - 2);
		std::replace(path.begin(), path.end(), '\\', '/');
		while (path.size() > 1 && path.back() == '/' &&
			!(path.size() == 3 && path[1] == ':')) {
			path.pop_back();
		}
		return path;
	}

	sgf::String DirectoryName(const sgf::String& sourcePath)
	{
		const sgf::String path = NormalizePath(sourcePath);
		const size_t separator = path.find_last_of('/');
		if (separator == sgf::String::npos)
			return ".";
		if (separator == 0)
			return "/";
		if (separator == 2 && path.size() >= 3 && path[1] == ':')
			return path.substr(0, 3);
		return path.substr(0, separator);
	}

	sgf::String BaseName(const sgf::String& sourcePath)
	{
		const sgf::String path = NormalizePath(sourcePath);
		const size_t separator = path.find_last_of('/');
		return separator == sgf::String::npos ? path : path.substr(separator + 1);
	}

	sgf::String JoinPath(const sgf::String& directory, const sgf::String& name)
	{
		const sgf::String normalizedDirectory = NormalizePath(directory);
		const sgf::String normalizedName = NormalizePath(name);
		if (normalizedDirectory.empty())
			return normalizedName;
		if (normalizedDirectory.back() == '/')
			return normalizedDirectory + normalizedName;
		return normalizedDirectory + "/" + normalizedName;
	}

	void AddUniqueDirectory(std::vector<sgf::String>& directories, const sgf::String& directory)
	{
		const sgf::String normalized = NormalizePath(directory);
		if (normalized.empty())
			return;
		for (const auto& existing : directories) {
			if (EqualsNoCase(existing, normalized))
				return;
		}
		directories.push_back(normalized);
	}

	void AddEnvironmentDirectories(std::vector<sgf::String>& directories, const char* value)
	{
		if (!value || !*value)
			return;
		const sgf::String paths = value;
		size_t begin = 0;
		while (begin <= paths.size()) {
			const size_t end = paths.find(';', begin);
			AddUniqueDirectory(directories, paths.substr(begin, end - begin));
			if (end == sgf::String::npos)
				break;
			begin = end + 1;
		}
	}

	std::wstring Utf8ToWide(const sgf::String& value)
	{
		if (value.empty())
			return std::wstring();
		const int length = MultiByteToWideChar(
			CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, nullptr, 0);
		if (length <= 0)
			return std::wstring();
		std::vector<wchar_t> buffer(static_cast<size_t>(length));
		if (MultiByteToWideChar(
			CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1,
			buffer.data(), length) <= 0) {
			return std::wstring();
		}
		return std::wstring(buffer.data());
	}

	std::wstring JoinWidePath(const std::wstring& directory, const std::wstring& name)
	{
		if (directory.empty())
			return name;
		if (directory.back() == L'\\' || directory.back() == L'/')
			return directory + name;
		return directory + L"\\" + name;
	}

	bool IsRegularFile(const std::wstring& path)
	{
		const DWORD attributes = GetFileAttributesW(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES &&
			(attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
	}

	bool IsNonEmptyFile(const std::wstring& path)
	{
		WIN32_FILE_ATTRIBUTE_DATA data = {};
		if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) ||
			(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
			return false;
		}
		return data.nFileSizeHigh != 0 || data.nFileSizeLow != 0;
	}

	std::wstring GetExecutableDirectory()
	{
		std::vector<wchar_t> buffer(32768);
		const DWORD length = GetModuleFileNameW(
			nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0 || length >= buffer.size())
			return std::wstring();
		std::wstring path(buffer.data(), length);
		const size_t separator = path.find_last_of(L"\\/");
		return separator == std::wstring::npos ? std::wstring() : path.substr(0, separator);
	}

	std::wstring FindFfmpegExecutable()
	{
		const std::wstring bundled = JoinWidePath(GetExecutableDirectory(), L"ffmpeg.exe");
		if (IsRegularFile(bundled))
			return bundled;

		std::vector<wchar_t> buffer(32768);
		const DWORD length = SearchPathW(
			nullptr, L"ffmpeg.exe", nullptr,
			static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
		if (length == 0 || length >= buffer.size())
			return std::wstring();
		return std::wstring(buffer.data(), length);
	}

	std::wstring QuoteWindowsArgument(const std::wstring& argument)
	{
		std::wstring result = L"\"";
		size_t backslashCount = 0;
		for (wchar_t value : argument) {
			if (value == L'\\') {
				++backslashCount;
				continue;
			}
			if (value == L'\"') {
				result.append(backslashCount * 2 + 1, L'\\');
				result.push_back(value);
				backslashCount = 0;
				continue;
			}
			result.append(backslashCount, L'\\');
			backslashCount = 0;
			result.push_back(value);
		}
		result.append(backslashCount * 2, L'\\');
		result.push_back(L'\"');
		return result;
	}

	bool RunProcessAndWait(
		const std::wstring& executable,
		const std::vector<std::wstring>& arguments,
		DWORD& exitCode,
		DWORD& launchError)
	{
		std::wstring commandLine = QuoteWindowsArgument(executable);
		for (const auto& argument : arguments) {
			commandLine.push_back(L' ');
			commandLine += QuoteWindowsArgument(argument);
		}
		std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
		mutableCommand.push_back(L'\0');

		STARTUPINFOW startupInfo = {};
		startupInfo.cb = sizeof(startupInfo);
		PROCESS_INFORMATION processInfo = {};
		if (!CreateProcessW(
			executable.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
			CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo)) {
			launchError = GetLastError();
			return false;
		}

		const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, INFINITE);
		if (waitResult != WAIT_OBJECT_0) {
			launchError = GetLastError();
			CloseHandle(processInfo.hThread);
			CloseHandle(processInfo.hProcess);
			return false;
		}
		const bool gotExitCode = GetExitCodeProcess(processInfo.hProcess, &exitCode) != FALSE;
		if (!gotExitCode)
			launchError = GetLastError();
		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);
		return gotExitCode;
	}

	sgf::String RemoveAnimationExtension(sgf::String name)
	{
		const char* suffixes[] = {
			".reanim.compiled", ".compiled", ".reanim", ".xml"
		};
		for (const char* suffix : suffixes) {
			const sgf::String suffixString = suffix;
			if (EndsWithNoCase(name, suffixString)) {
				name.resize(name.size() - suffixString.size());
				break;
			}
		}
		return name.empty() ? "animation" : name;
	}
}

ViewerApp::ViewerApp()
	:GameApp(GAME_WINDOW_WIDTH, GAME_WINDOW_HEIGHT, "PVZ Reanim Viewer", true, true)
{
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.Fonts->AddFontDefault();

	float baseFontSize = 13.0f; // 13.0f is the size of the default font. Change to the font size you use.
	float iconFontSize = baseFontSize * 1.5f; // FontAwesome fonts need to have their sizes reduced by 2.0f/3.0f in order to align correctly

	static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
	ImFontConfig icons_config;
	icons_config.MergeMode = true;
	icons_config.PixelSnapH = true;
	icons_config.GlyphMinAdvanceX = iconFontSize;
	const sgf::String iconFontPath = sgf::String("fonts/") + FONT_ICON_FILE_NAME_FAS;
	if (sgf::FileManager::IsRealFileExist(iconFontPath)) {
		mHasIconFont = io.Fonts->AddFontFromFileTTF(
			iconFontPath.c_str(), iconFontSize, &icons_config, icons_ranges) != nullptr;
	}
	else {
		std::cout << "Font Awesome not found; using text controls." << std::endl;
	}

	mNfdInitialized = NFD_Init() == NFD_OKAY;
	if (!mNfdInitialized)
		mLoadStatus = "File dialog initialization failed; drag a file or use the CLI.";
	SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
}

ViewerApp::~ViewerApp()
{
	ReleaseExportResources(true);
	ClearLoadedAnimation();
	if (mNfdInitialized)
		NFD_Quit();
}

void ViewerApp::DrawImgui()
{
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();

	ImGuiWindowFlags window_flags =
		ImGuiWindowFlags_NoMove |       // 禁止移动
		ImGuiWindowFlags_NoResize |     // 禁止调整大小
		ImGuiWindowFlags_NoCollapse |   // 禁止折叠
		ImGuiWindowFlags_NoTitleBar |   // 移除标题栏
		ImGuiWindowFlags_MenuBar;       // 允许菜单栏

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);

	ImGui::Begin("Main DockSpace", nullptr, window_flags);

	// Use a versioned dockspace ID so installations with the old floating-window
	// layout receive the fixed defaults once while later user adjustments persist.
	ImGuiID dockspace_id = ImGui::GetID("MainDockSpaceV2");
	if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
		ImGui::DockBuilderRemoveNode(dockspace_id);
		ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

		ImGuiID viewerDockId = dockspace_id;
		ImGuiID sidebarDockId = ImGui::DockBuilderSplitNode(
			viewerDockId, ImGuiDir_Left, 0.22f, nullptr, &viewerDockId);
		ImGuiID resourcesDockId = ImGui::DockBuilderSplitNode(
			sidebarDockId, ImGuiDir_Down, 0.55f, nullptr, &sidebarDockId);
		ImGuiID controlsDockId = ImGui::DockBuilderSplitNode(
			viewerDockId, ImGuiDir_Down, 0.23f, nullptr, &viewerDockId);

		ImGui::DockBuilderDockWindow("Layer List", sidebarDockId);
		ImGui::DockBuilderDockWindow("Image Resource List", resourcesDockId);
		ImGui::DockBuilderDockWindow("Control Panel", controlsDockId);
		ImGui::DockBuilderDockWindow("Viewer", viewerDockId);
		ImGui::DockBuilderFinish(dockspace_id);
	}
	ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

	DisplayMenuBar();
	
	ImGui::End();

	DisplayBenchLayer();
	DisplayExportDialog();

	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void ViewerApp::Draw()
{
	mGraphics->Clear();
	DrawImgui();

	mGraphics->ActiveTextureShader();
	DrawTopLayer();
	mGraphics->Present();
	if (mExporting)
		CaptureExportFrame();
	
	SDL_GL_SwapWindow(mGameWindow);
}

void ViewerApp::Update()
{
	if (mAnimator && !mExporting)
		mAnimator->Update();

	if (mFocused)
	{
		static ImGuiIO& io = ImGui::GetIO();

		float wheel = GetMouseWheelY();

		mScale += wheel * 0.1f;

		if (mScale < 0.5f)
			mScale = 0.5f;

		if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			mDragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
			//std::cout << "Mouse dragging: (" << mDragDelta.x << ", " << mDragDelta.y << ")" << std::endl;
		}
		else {
			mMapPos.x += mDragDelta.x;
			mMapPos.y += mDragDelta.y;
			mDragDelta = { 0,0 };
		}
	}

	GameApp::Update();
}

void ViewerApp::CopeEvent(SDL_Event& theEvent)
{
	GameApp::CopeEvent(theEvent);

	switch (theEvent.type) {
	case SDL_EventType::SDL_WINDOWEVENT:
		switch (theEvent.window.event)
		{
		case SDL_WINDOWEVENT_SIZE_CHANGED:
			mWidth = theEvent.window.data1;
			mHeight = theEvent.window.data2;
		default:
			break;
		}
		//theEvent.window.event
		break;
	case SDL_EventType::SDL_MOUSEWHEEL: {
		break;
	}
	case SDL_EventType::SDL_DROPFILE: {
		if (theEvent.drop.file) {
			const sgf::String droppedPath = theEvent.drop.file;
			SDL_free(theEvent.drop.file);
			theEvent.drop.file = nullptr;
			if (mExporting)
				mExportStatus = "Finish or cancel the current export before loading another file.";
			else
				LoadReanim(droppedPath, mExtraImageDirs);
		}
		break;
	}
	}
}

static void ImGuiDrawGrid(
	ImDrawList* draw_list, ImVec2 window_pos, ImVec2 window_size, ImVec2 pos_offset, float grid_spacing, ImU32 grid_color, ImU32 grid_color_shallow)
{
	pos_offset.x -= int(pos_offset.x) / int(grid_spacing * 10) * grid_spacing * 10;
	pos_offset.y -= int(pos_offset.y) / int(grid_spacing * 10) * grid_spacing * 10;


	draw_list->AddRectFilled(window_pos, { window_pos.x + window_size.x ,window_pos.y + window_size.y }, IM_COL32(0x69, 0x62, 0x5d, 0xff));

	int num_lines_x = (int)(window_size.x / grid_spacing) + 1;
	int num_lines_y = (int)(window_size.y / grid_spacing) + 1;

	for (int i = 0; i < num_lines_x; ++i)
	{
		float x = window_pos.x + i * grid_spacing + pos_offset.x;
		if (i % 10)
			draw_list->AddLine(ImVec2(x, window_pos.y), ImVec2(x, window_pos.y + window_size.y), grid_color_shallow);
		else
			draw_list->AddLine(ImVec2(x, window_pos.y), ImVec2(x, window_pos.y + window_size.y), grid_color);

	}


	for (int i = 0; i < num_lines_y; ++i)
	{
		float y = window_pos.y + i * grid_spacing + pos_offset.y;
		if (i % 10)
			draw_list->AddLine(ImVec2(window_pos.x, y), ImVec2(window_pos.x + window_size.x, y), grid_color_shallow);
		else
			draw_list->AddLine(ImVec2(window_pos.x, y), ImVec2(window_pos.x + window_size.x, y), grid_color);
	}
}

void ViewerApp::DisplayBenchLayer()
{
	ImGui::Begin("Layer List");

	if (mReanimPtr && mAnimator)
	{
		for (size_t i = 0; i < mReanimPtr->mTracks->size(); i++)
		{
			sgf::String& trackName = mReanimPtr->mTracks->at(i).mTrackName;
			bool trackVisible = i < mTrackShowList.size() && mTrackShowList[i] != 0;
			if (ImGui::Checkbox((trackName + "##" + sgf::SString::StrParse(i)).c_str(), &trackVisible)) {
				mTrackShowList[i] = trackVisible ? 1 : 0;
				UpdateAnimatorState();
			}

			if (StartsWithNoCase(trackName, "anim_")) {
				ImGui::SameLine();
				if (ImGui::Button((sgf::String("Use Range##") + sgf::SString::StrParse(i)).c_str())) {
					mAnimator->SetFrameRangeByTrackName(trackName);
				};
			}
		}
	}
	else {
		ImGui::TextDisabled("No animation loaded.");
	}
	

	ImGui::End();

	ImGui::Begin("Image Resource List");
	if (mReanimPtr) {
		for (auto& x : *mReanimPtr->mImagesSet)
		{
			const bool missing = std::find(mMissingImages.begin(), mMissingImages.end(), x) != mMissingImages.end();
			if (missing)
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
			ImGui::TextUnformatted(x.c_str());
			if (missing) {
				ImGui::SameLine();
				ImGui::TextUnformatted("[transparent placeholder]");
				ImGui::PopStyleColor();
			}
		}
	}
	else {
		ImGui::TextDisabled("No image resources.");
	}

	ImGui::Separator();
	ImGui::TextWrapped("Status: %s", mLoadStatus.c_str());
	if (!mImageSearchDirs.empty()) {
		ImGui::TextUnformatted("Image search order:");
		for (const auto& directory : mImageSearchDirs)
			ImGui::BulletText("%s", directory.c_str());
	}
	if (!mMissingImages.empty()) {
		ImGui::Text("Missing images (%d):", static_cast<int>(mMissingImages.size()));
		for (const auto& imageId : mMissingImages)
			ImGui::BulletText("%s", imageId.c_str());
	}
	ImGui::End();

	ImGui::Begin("Viewer");

	mWindowSize = ImGui::GetWindowSize();
	mWindowPos = ImGui::GetWindowPos();
	mFocused = ImGui::IsWindowFocused();
	if (mCenterOnNextFrame && mWindowSize.x > 0.0f && mWindowSize.y > 20.0f) {
		mMapPos.x = mWindowSize.x * 0.5f;
		mMapPos.y = 20.0f + (mWindowSize.y - 20.0f) * 0.5f;
		mCenterOnNextFrame = false;
	}

	ImDrawList* draw_list = ImGui::GetWindowDrawList();

	ImGuiDrawGrid(draw_list, mWindowPos, mWindowSize,
		{ mMapPos.x + mDragDelta.x, mMapPos.y + mDragDelta.y },
		10.0f * mScale, IM_COL32(0xff, 0xff, 0xff, 0xA0), IM_COL32(0xff, 0xff, 0xff, 0x20));


	ImGui::End();

	ImGui::Begin("Control Panel");

	if (mAnimator) {
		const char* playLabel = mHasIconFont ? ICON_FA_PLAY " Play" : "Play";
		const char* pauseLabel = mHasIconFont ? ICON_FA_PAUSE " Pause" : "Pause";
		ImGui::PushStyleColor(ImGuiCol_Text, { 0,1,0,1 });
		if (ImGui::Button(playLabel, { 0,30 })) {
			mAnimator->Play();
		}
		ImGui::PopStyleColor();
		ImGui::SameLine();

		ImGui::PushStyleColor(ImGuiCol_Text, { 1,0,0,1 });
		if (ImGui::Button(pauseLabel, { 0,30 })) {
			mAnimator->Pause();
		}
		ImGui::PopStyleColor();


		if (ImGui::SliderFloat("Speed", &mReanimSpeed, 0.5f, 5.0f, "%.1f")) {
			mAnimator->mSpeed = mReanimSpeed;
		};

		ImGui::SameLine();
		if (ImGui::Button("Reset")) { mReanimSpeed = 1.0f; mAnimator->mSpeed = 1.0f; };

		ImGui::SliderFloat("Process", &mAnimator->mFrameIndexNow, mAnimator->mFrameIndexBegin, mAnimator->mFrameIndexEnd, "%.1f");
		ImGui::Text("RangeBegin: %f", mAnimator->mFrameIndexBegin);
		ImGui::SameLine();
		ImGui::Text("RangeEnd: %f", mAnimator->mFrameIndexEnd);
		ImGui::SameLine();
		if (ImGui::Button("Reset to default")) {
			mAnimator->SetFrameRangeToDefault();
		};
	}
	ImGui::End();
}

void ViewerApp::DisplayMenuBar()
{
	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("Files")) {
			if (ImGui::MenuItem("Open", nullptr, false, !mExporting)) {
				char* targetPath = nullptr;
				nfdu8filteritem_t item = { "Reanimation", "xml,reanim,compiled" };
				if (!mNfdInitialized) {
					mLoadStatus = "File dialog is unavailable; drop a file or use the CLI.";
				}
				else {
					const nfdresult_t result = NFD_OpenDialog(&targetPath, &item, 1, nullptr);
					if (result == NFD_OKAY && targetPath) {
						const sgf::String selectedPath = targetPath;
						NFD_FreePath(targetPath);
						LoadReanim(selectedPath, mExtraImageDirs);
					}
					else if (result == NFD_ERROR) {
						const char* error = NFD_GetError();
						mLoadStatus = sgf::String("Open failed: ") + (error ? error : "unknown error");
					}
				}
			}

			if (ImGui::MenuItem("Reload", nullptr, false,
				!mTargetPath.empty() && !mExporting)) {
				LoadReanim(mTargetPath, mExtraImageDirs);
			}

			ImGui::Separator();
			if (ImGui::MenuItem("Export...", nullptr, false,
				mAnimator != nullptr && !mExporting)) {
				mExportFrameBegin = static_cast<int>(std::floor(mAnimator->mFrameIndexBegin));
				mExportFrameEnd = static_cast<int>(std::floor(mAnimator->mFrameIndexEnd));
				mExportFPS = std::max(1, static_cast<int>(std::lround(mReanimPtr->mFPS)));
				mExportScale = mScale;
				mExportFinished = false;
				mExportSucceeded = false;
				mExportStatus = "Ready.";
				mExportDialogRequested = true;
			}
			ImGui::EndMenu();
		}

		ImGui::Text("FPS: %d", mFPS);
		ImGui::EndMenuBar();
	}
}

sgf::String ViewerApp::BuildDefaultExportName() const
{
	const sgf::String extension = mExportFormat == 0 ? ".gif" : ".mp4";
	return RemoveAnimationExtension(BaseName(mTargetPath)) + extension;
}

void ViewerApp::DisplayExportDialog()
{
	if (mExportDialogRequested) {
		ImGui::OpenPopup("Export Animation");
		mExportDialogRequested = false;
	}

	if (!ImGui::BeginPopupModal(
		"Export Animation", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	const char* formats[] = { "GIF animation", "MP4 video" };
	ImGui::BeginDisabled(mExporting);
	ImGui::Combo("Format", &mExportFormat, formats, IM_ARRAYSIZE(formats));
	ImGui::InputInt("Width", &mExportWidth, 16, 64);
	ImGui::InputInt("Height", &mExportHeight, 16, 64);
	ImGui::InputInt("FPS", &mExportFPS, 1, 5);
	ImGui::SliderFloat("Scale", &mExportScale, 0.1f, 5.0f, "%.1f");
	ImGui::InputInt("First frame", &mExportFrameBegin, 1, 10);
	ImGui::InputInt("Last frame", &mExportFrameEnd, 1, 10);

	if (mExportFormat == 0)
		ImGui::Checkbox("Transparent background", &mExportTransparent);
	else
		mExportTransparent = false;
	if (!mExportTransparent)
		ImGui::ColorEdit3("Background", &mExportBackground.x);
	ImGui::EndDisabled();

	mExportWidth = std::max(64, std::min(2048, mExportWidth));
	mExportHeight = std::max(64, std::min(2048, mExportHeight));
	mExportFPS = std::max(1, std::min(60, mExportFPS));
	mExportScale = std::max(0.1f, std::min(5.0f, mExportScale));
	if (mReanimPtr && mReanimPtr->mTracks && !mReanimPtr->mTracks->empty()) {
		const int lastAvailableFrame = static_cast<int>(
			mReanimPtr->mTracks->front().mFrames.size()) - 1;
		mExportFrameBegin = std::max(0, std::min(lastAvailableFrame, mExportFrameBegin));
		mExportFrameEnd = std::max(mExportFrameBegin,
			std::min(lastAvailableFrame, mExportFrameEnd));
	}

	if (mExporting) {
		const float progress = mExportFrameCount > 0
			? static_cast<float>(mExportFramesCaptured) / static_cast<float>(mExportFrameCount)
			: 0.0f;
		ImGui::ProgressBar(progress, ImVec2(360.0f, 0.0f));
		ImGui::TextUnformatted(mExportStatus.c_str());
		if (ImGui::Button("Cancel export"))
			FinishExport(false, "Export canceled.");
	}
	else {
		ImGui::TextWrapped("%s", mExportStatus.c_str());
		if (mExportFinished && mExportSucceeded) {
			if (ImGui::Button("Open file"))
				OpenExportResult(false);
			ImGui::SameLine();
			if (ImGui::Button("Show in folder"))
				OpenExportResult(true);
			ImGui::Separator();
		}

		if (ImGui::Button("Export")) {
			if (!mNfdInitialized) {
				mExportStatus = "File dialog is unavailable.";
			}
			else {
				char* targetPath = nullptr;
				const bool gif = mExportFormat == 0;
				const nfdu8filteritem_t filter = {
					gif ? "GIF animation" : "MP4 video",
					gif ? "gif" : "mp4"
				};
				const sgf::String defaultDirectory = DirectoryName(mTargetPath);
				const sgf::String defaultName = BuildDefaultExportName();
				const nfdresult_t result = NFD_SaveDialog(
					&targetPath, &filter, 1, defaultDirectory.c_str(), defaultName.c_str());
				if (result == NFD_OKAY && targetPath) {
					sgf::String selectedPath = targetPath;
					NFD_FreePath(targetPath);
					const sgf::String extension = gif ? ".gif" : ".mp4";
					if (!EndsWithNoCase(selectedPath, extension))
						selectedPath += extension;
					StartExport(
						selectedPath, mExportWidth, mExportHeight, mExportScale,
						mExportFPS, gif && mExportTransparent, false,
						mExportFrameBegin, mExportFrameEnd);
				}
				else if (result == NFD_ERROR) {
					const char* error = NFD_GetError();
					mExportStatus = sgf::String("Save dialog failed: ") +
						(error ? error : "unknown error");
				}
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Close"))
			ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

bool ViewerApp::StartExport(
	const sgf::String& outputPath,
	int width,
	int height,
	float scale,
	int fps,
	bool transparent,
	bool exitWhenDone,
	int frameBegin,
	int frameEnd)
{
	if (!mAnimator || !mReanimPtr || !mReanimPtr->mTracks ||
		mReanimPtr->mTracks->empty()) {
		mExportStatus = "Export failed: no animation is loaded.";
		return false;
	}
	if (mExporting) {
		mExportStatus = "Export failed: another export is already running.";
		return false;
	}

	if (EndsWithNoCase(outputPath, ".gif"))
		mExportFormat = 0;
	else if (EndsWithNoCase(outputPath, ".mp4"))
		mExportFormat = 1;
	else {
		mExportStatus = "Export failed: output must end in .gif or .mp4.";
		return false;
	}

	mExportWidth = std::max(64, std::min(2048, width));
	mExportHeight = std::max(64, std::min(2048, height));
	if (mExportFormat == 1) {
		mExportWidth += mExportWidth & 1;
		mExportHeight += mExportHeight & 1;
	}
	mExportScale = std::max(0.1f, std::min(5.0f, scale));
	mExportFPS = fps > 0
		? std::max(1, std::min(60, fps))
		: std::max(1, static_cast<int>(std::lround(mReanimPtr->mFPS)));
	mExportTransparent = mExportFormat == 0 && transparent;

	const int lastAvailableFrame = static_cast<int>(
		mReanimPtr->mTracks->front().mFrames.size()) - 1;
	if (frameBegin < 0)
		frameBegin = static_cast<int>(std::floor(mAnimator->mFrameIndexBegin));
	if (frameEnd < 0)
		frameEnd = static_cast<int>(std::floor(mAnimator->mFrameIndexEnd));
	mExportFrameBegin = std::max(0, std::min(lastAvailableFrame, frameBegin));
	mExportFrameEnd = std::max(mExportFrameBegin, std::min(lastAvailableFrame, frameEnd));

	return BeginExport(outputPath, exitWhenDone);
}

bool ViewerApp::BeginExport(const sgf::String& outputPath, bool exitWhenDone)
{
	mExportFfmpegPath = FindFfmpegExecutable();
	if (mExportFfmpegPath.empty()) {
		mExportStatus = "Export failed: ffmpeg.exe was not found beside PVZCapture.exe or on PATH.";
		return false;
	}

	mExportOutputPathWide = Utf8ToWide(outputPath);
	if (mExportOutputPathWide.empty()) {
		mExportStatus = "Export failed: output path is not valid UTF-8.";
		return false;
	}

	wchar_t temporaryDirectory[MAX_PATH + 1] = {};
	wchar_t temporaryFile[MAX_PATH + 1] = {};
	const DWORD temporaryDirectoryLength = GetTempPathW(MAX_PATH, temporaryDirectory);
	if (temporaryDirectoryLength == 0 || temporaryDirectoryLength >= MAX_PATH ||
		GetTempFileNameW(temporaryDirectory, L"PVR", 0, temporaryFile) == 0) {
		mExportStatus = "Export failed: could not create a temporary frame file.";
		return false;
	}
	mExportRawPath = temporaryFile;
	if (_wfopen_s(&mExportRawFile, mExportRawPath.c_str(), L"wb") != 0 ||
		!mExportRawFile) {
		DeleteFileW(mExportRawPath.c_str());
		mExportRawPath.clear();
		mExportStatus = "Export failed: could not open the temporary frame file.";
		return false;
	}

	GLint previousFramebuffer = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
	mGraphics->GenFrameBuffer(
		&mExportFramebuffer, &mExportTexture, mExportWidth, mExportHeight);
	const GLenum framebufferStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
	if (framebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
		ReleaseExportResources(true);
		mExportStatus = "Export failed: OpenGL could not create the export framebuffer.";
		return false;
	}

	try {
		mExportPixels.resize(
			static_cast<size_t>(mExportWidth) * static_cast<size_t>(mExportHeight) * 4);
	}
	catch (const std::bad_alloc&) {
		ReleaseExportResources(true);
		mExportStatus = "Export failed: not enough memory for the export framebuffer.";
		return false;
	}

	mExportOutputPath = outputPath;
	mExportFrameCurrent = mExportFrameBegin;
	mExportFramesCaptured = 0;
	mExportFrameCount = mExportFrameEnd - mExportFrameBegin + 1;
	mExportSavedFrame = mAnimator->mFrameIndexNow;
	mExportSavedRangeBegin = mAnimator->mFrameIndexBegin;
	mExportSavedRangeEnd = mAnimator->mFrameIndexEnd;
	mExportSavedPlaying = mAnimator->mIsPlaying;
	mExportSavedPlayState = mAnimator->mPlayingState;
	mAnimator->SetFrameRange(mExportFrameBegin, mExportFrameEnd);
	mAnimator->mFrameIndexNow = static_cast<float>(mExportFrameBegin);
	mAnimator->Pause();

	mExportExitWhenDone = exitWhenDone;
	mAutoExportFailed = false;
	mExportFinished = false;
	mExportSucceeded = false;
	mExporting = true;
	mExportStatus = "Capturing frames...";
	return true;
}

bool ViewerApp::CaptureExportFrame()
{
	if (!mExporting || !mExportRawFile || !mAnimator)
		return false;

	GLint previousFramebuffer = 0;
	GLint previousViewport[4] = {};
	GLint previousPackAlignment = 4;
	GLint previousBlendSourceRgb = GL_SRC_ALPHA;
	GLint previousBlendDestinationRgb = GL_ONE_MINUS_SRC_ALPHA;
	GLint previousBlendSourceAlpha = GL_SRC_ALPHA;
	GLint previousBlendDestinationAlpha = GL_ONE_MINUS_SRC_ALPHA;
	GLfloat previousClearColor[4] = {};
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
	glGetIntegerv(GL_VIEWPORT, previousViewport);
	glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
	glGetIntegerv(GL_BLEND_SRC_RGB, &previousBlendSourceRgb);
	glGetIntegerv(GL_BLEND_DST_RGB, &previousBlendDestinationRgb);
	glGetIntegerv(GL_BLEND_SRC_ALPHA, &previousBlendSourceAlpha);
	glGetIntegerv(GL_BLEND_DST_ALPHA, &previousBlendDestinationAlpha);
	glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);

	const sgf::Point previousTransform = mGraphics->mTransformPosition;
	const sgf::Point previousModelTransform = mGraphics->mModelTransformPosition;
	const sgf::Color previousColor = mGraphics->mCubeColor;

	while (glGetError() != GL_NO_ERROR) {
	}
	glBindFramebuffer(GL_FRAMEBUFFER, mExportFramebuffer);
	glViewport(0, 0, mExportWidth, mExportHeight);
	mGraphics->ProjectionResize(
		static_cast<float>(mExportWidth), static_cast<float>(mExportHeight));
	if (mExportTransparent)
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	else
		glClearColor(
			mExportBackground.x, mExportBackground.y, mExportBackground.z, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	glBlendFuncSeparate(
		GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
		GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	mGraphics->ActiveTextureShader();
	mGraphics->MoveTo(
		static_cast<float>(mExportWidth) * 0.5f,
		static_cast<float>(mExportHeight) * 0.5f);
	mGraphics->ModelMoveTo(0.0f, 0.0f);
	mGraphics->SetCubeColor({ 1, 1, 1, 1 });
	mAnimator->mFrameIndexNow = static_cast<float>(mExportFrameCurrent);
	mAnimator->PresentMatrix(
		mGraphics,
		glm::scale(
			glm::mat4x4(1.0f),
			glm::vec3(mExportScale, mExportScale, 1.0f)));
	mGraphics->Present();

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(
		0, 0, mExportWidth, mExportHeight,
		GL_RGBA, GL_UNSIGNED_BYTE, mExportPixels.data());
	const GLenum readError = glGetError();

	glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
	glViewport(
		previousViewport[0], previousViewport[1],
		previousViewport[2], previousViewport[3]);
	mGraphics->ProjectionResize(
		static_cast<float>(mWidth), static_cast<float>(mHeight));
	mGraphics->MoveTo(previousTransform.x, previousTransform.y);
	mGraphics->ModelMoveTo(previousModelTransform.x, previousModelTransform.y);
	mGraphics->SetCubeColor(previousColor);
	glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
	glBlendFuncSeparate(
		previousBlendSourceRgb, previousBlendDestinationRgb,
		previousBlendSourceAlpha, previousBlendDestinationAlpha);
	glClearColor(
		previousClearColor[0], previousClearColor[1],
		previousClearColor[2], previousClearColor[3]);

	if (readError != GL_NO_ERROR) {
		FinishExport(false, "Export failed while reading the OpenGL framebuffer.");
		return false;
	}
	if (mExportTransparent) {
		for (size_t pixel = 0; pixel < mExportPixels.size(); pixel += 4) {
			const unsigned int alpha = mExportPixels[pixel + 3];
			if (alpha == 0) {
				mExportPixels[pixel] = 0;
				mExportPixels[pixel + 1] = 0;
				mExportPixels[pixel + 2] = 0;
			}
			else if (alpha < 255) {
				for (size_t channel = 0; channel < 3; ++channel) {
					const unsigned int straight =
						(static_cast<unsigned int>(mExportPixels[pixel + channel]) * 255u +
							alpha / 2u) / alpha;
					mExportPixels[pixel + channel] = static_cast<unsigned char>(
						std::min(255u, straight));
				}
			}
		}
	}

	const size_t rowBytes = static_cast<size_t>(mExportWidth) * 4;
	for (int row = mExportHeight - 1; row >= 0; --row) {
		const unsigned char* rowPixels =
			mExportPixels.data() + static_cast<size_t>(row) * rowBytes;
		if (std::fwrite(rowPixels, rowBytes, 1, mExportRawFile) != 1) {
			FinishExport(false, "Export failed while writing temporary frames.");
			return false;
		}
	}

	++mExportFramesCaptured;
	++mExportFrameCurrent;
	std::ostringstream progress;
	progress << "Capturing frame " << mExportFramesCaptured
		<< "/" << mExportFrameCount << "...";
	mExportStatus = progress.str();

	if (mExportFramesCaptured >= mExportFrameCount) {
		std::fclose(mExportRawFile);
		mExportRawFile = nullptr;
		mExportStatus = "Encoding...";
		if (EncodeExport())
			FinishExport(true, "Export complete: " + mExportOutputPath);
		else
			FinishExport(false, mExportStatus);
	}
	return true;
}

bool ViewerApp::EncodeExport()
{
	std::vector<std::wstring> arguments = {
		L"-hide_banner", L"-loglevel", L"error", L"-y",
		L"-f", L"rawvideo",
		L"-pixel_format", L"rgba",
		L"-video_size", std::to_wstring(mExportWidth) + L"x" + std::to_wstring(mExportHeight),
		L"-framerate", std::to_wstring(mExportFPS),
		L"-i", mExportRawPath,
		L"-frames:v", std::to_wstring(mExportFrameCount)
	};

	if (mExportFormat == 0) {
		arguments.push_back(L"-filter_complex");
		arguments.push_back(
			L"[0:v]split[palette_source][gif_source];"
			L"[palette_source]palettegen=reserve_transparent=1:stats_mode=diff[palette];"
			L"[gif_source][palette]paletteuse=dither=sierra2_4a:alpha_threshold=128");
		arguments.push_back(L"-loop");
		arguments.push_back(L"0");
	}
	else {
		arguments.push_back(L"-an");
		arguments.push_back(L"-c:v");
		arguments.push_back(L"libx264");
		arguments.push_back(L"-preset");
		arguments.push_back(L"medium");
		arguments.push_back(L"-crf");
		arguments.push_back(L"18");
		arguments.push_back(L"-pix_fmt");
		arguments.push_back(L"yuv420p");
		arguments.push_back(L"-movflags");
		arguments.push_back(L"+faststart");
	}
	arguments.push_back(mExportOutputPathWide);

	DWORD exitCode = 0;
	DWORD launchError = 0;
	if (!RunProcessAndWait(mExportFfmpegPath, arguments, exitCode, launchError)) {
		std::ostringstream status;
		status << "Export failed: could not start FFmpeg (Windows error "
			<< launchError << ").";
		mExportStatus = status.str();
		return false;
	}
	if (exitCode != 0 || !IsNonEmptyFile(mExportOutputPathWide)) {
		std::ostringstream status;
		status << "Export failed: FFmpeg exited with code " << exitCode << ".";
		mExportStatus = status.str();
		return false;
	}
	return true;
}

void ViewerApp::RestoreAnimatorAfterExport()
{
	if (!mAnimator)
		return;
	mAnimator->SetFrameRange(
		static_cast<int>(mExportSavedRangeBegin),
		static_cast<int>(mExportSavedRangeEnd));
	mAnimator->mFrameIndexNow = mExportSavedFrame;
	if (mExportSavedPlaying)
		mAnimator->Play(mExportSavedPlayState);
	else
		mAnimator->Pause();
}

void ViewerApp::ReleaseExportResources(bool removeRawFile)
{
	if (mExportRawFile) {
		std::fclose(mExportRawFile);
		mExportRawFile = nullptr;
	}
	if (mGraphics && mExportFramebuffer) {
		mGraphics->ReleaseFrameBuffer(mExportFramebuffer);
		mExportFramebuffer = 0;
	}
	if (mGraphics && mExportTexture) {
		mGraphics->ReleaseTexture(mExportTexture);
		mExportTexture = 0;
	}
	if (removeRawFile && !mExportRawPath.empty()) {
		DeleteFileW(mExportRawPath.c_str());
		mExportRawPath.clear();
	}
	mExportPixels.clear();
}

void ViewerApp::FinishExport(bool succeeded, const sgf::String& status)
{
	const bool exitWhenDone = mExportExitWhenDone;
	RestoreAnimatorAfterExport();
	ReleaseExportResources(true);
	mExporting = false;
	mExportFinished = true;
	mExportSucceeded = succeeded;
	mExportStatus = status;
	if (succeeded)
		mLoadStatus = status;
	if (exitWhenDone) {
		mAutoExportFailed = !succeeded;
		mIsOpen = false;
	}
}

void ViewerApp::OpenExportResult(bool selectInFolder) const
{
	if (mExportOutputPathWide.empty())
		return;
	if (selectInFolder) {
		const std::wstring parameters = L"/select," +
			QuoteWindowsArgument(mExportOutputPathWide);
		ShellExecuteW(
			nullptr, L"open", L"explorer.exe", parameters.c_str(),
			nullptr, SW_SHOWNORMAL);
	}
	else {
		ShellExecuteW(
			nullptr, L"open", mExportOutputPathWide.c_str(), nullptr,
			nullptr, SW_SHOWNORMAL);
	}
}

void ViewerApp::DrawTopLayer()
{
	if (mAnimator) {
		PresentAnimator();
	}
}

void ViewerApp::ClearLoadedAnimation()
{
	delete mAnimator;
	mAnimator = nullptr;

	delete mReanimPtr;
	mReanimPtr = nullptr;

	for (auto& resource : mResourceManager.mResourcePool) {
		delete static_cast<sgf::SimpleImage*>(resource.second);
	}
	mResourceManager.mResourcePool.clear();

	mTargetPath.clear();
	mExtraImageDirs.clear();
	mImageSearchDirs.clear();
	mMissingImages.clear();
	mTrackShowList.clear();
	mCenterOnNextFrame = false;
}

std::vector<sgf::String> ViewerApp::BuildImageSearchDirs(
	const sgf::String& reanimPath,
	const std::vector<sgf::String>& extraImageDirs) const
{
	std::vector<sgf::String> result;
	const sgf::String animationDirectory = DirectoryName(reanimPath);
	AddUniqueDirectory(result, animationDirectory);
	for (const auto& directory : extraImageDirs)
		AddUniqueDirectory(result, directory);

#ifdef _WIN32
	char* environmentValue = nullptr;
	size_t environmentLength = 0;
	if (_dupenv_s(&environmentValue, &environmentLength, "PVZ_REANIM_FALLBACK_DIR") == 0) {
		AddEnvironmentDirectories(result, environmentValue);
		std::free(environmentValue);
	}
#else
	AddEnvironmentDirectories(result, std::getenv("PVZ_REANIM_FALLBACK_DIR"));
#endif

	const sgf::String newresourceDirectory = DirectoryName(animationDirectory);
	if (EqualsNoCase(BaseName(newresourceDirectory), "newresource")) {
		const sgf::String repositoryDirectory = DirectoryName(newresourceDirectory);
		AddUniqueDirectory(result, JoinPath(repositoryDirectory, "assets/unpacked/reanim"));
	}

	AddUniqueDirectory(result, "assets/unpacked/reanim");
	return result;
}

bool ViewerApp::TryLoadImageResource(const sgf::String& imageId, sgf::String& resolvedPath)
{
	std::vector<sgf::String> imageNames;
	auto addImageName = [&imageNames](sgf::String imageName) {
		if (imageName.empty())
			return;
		imageName = NormalizePath(imageName);
		if (!EndsWithNoCase(imageName, ".png"))
			imageName += ".png";
		for (const auto& existing : imageNames) {
			if (EqualsNoCase(existing, imageName))
				return;
		}
		imageNames.push_back(imageName);
	};
	addImageName(imageId);
	const sgf::String imagePrefix = "IMAGE_REANIM_";
	if (StartsWithNoCase(imageId, imagePrefix))
		addImageName(imageId.substr(imagePrefix.size()));

	for (const auto& directory : mImageSearchDirs) {
		for (const auto& imageName : imageNames) {
			const sgf::String path = JoinPath(directory, imageName);
			if (!sgf::FileManager::IsRealFileExist(path))
				continue;

			SDL_Surface* surface = IMG_Load(path.c_str());
			if (!surface)
				continue;

			sgf::SimpleImage* image = new sgf::SimpleImage();
			image->LoadFromSurface(surface);
			if (!image->mSurface) {
				delete image;
				continue;
			}

			auto oldResource = mResourceManager.mResourcePool.find(imageId);
			if (oldResource != mResourceManager.mResourcePool.end())
				delete static_cast<sgf::SimpleImage*>(oldResource->second);
			mResourceManager.mResourcePool[imageId] = image;
			resolvedPath = path;
			return true;
		}
	}
	return false;
}

bool ViewerApp::RegisterPlaceholderImage(const sgf::String& imageId)
{
	SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
		0, 1, 1, 32, SDL_PIXELFORMAT_ABGR8888);
	if (!surface)
		return false;
	SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, 0, 0, 0, 0));

	sgf::SimpleImage* image = new sgf::SimpleImage();
	image->LoadFromSurface(surface);
	if (!image->mSurface) {
		delete image;
		return false;
	}
	mResourceManager.mResourcePool[imageId] = image;
	return true;
}

void ViewerApp::SelectInitialWorkRange()
{
	if (!mAnimator || !mReanimPtr || mReanimPtr->mTracks->empty())
		return;

	sgf::String workTrack;
	for (const auto& track : *mReanimPtr->mTracks) {
		if (EqualsNoCase(track.mTrackName, "anim_idle")) {
			workTrack = track.mTrackName;
			break;
		}
	}
	if (workTrack.empty()) {
		for (const auto& track : *mReanimPtr->mTracks) {
			if (StartsWithNoCase(track.mTrackName, "anim_")) {
				workTrack = track.mTrackName;
				break;
			}
		}
	}
	if (!workTrack.empty()) {
		const std::pair<int, int> range = mAnimator->GetTrackRange(workTrack);
		if (range.first >= 0 && range.second >= range.first)
			mAnimator->SetFrameRange(range.first, range.second);
	}
	mAnimator->mFrameIndexNow = mAnimator->mFrameIndexBegin;
	mAnimator->Play();
}

bool ViewerApp::LoadReanim(const sgf::String& reanimPath, const std::vector<sgf::String>& extraImageDirs)
{
	const sgf::String requestedPath = NormalizePath(reanimPath);
	const std::vector<sgf::String> requestedExtraDirs = extraImageDirs;
	if (requestedPath.empty()) {
		mLoadStatus = "Load failed: empty animation path.";
		return false;
	}

	std::unique_ptr<sgf::Reanimation> candidate(new sgf::Reanimation());
	if (!candidate->LoadFromFile(requestedPath.c_str())) {
		mLoadStatus = "Load failed: " + candidate->mLastError;
		if (candidate->mLastError.empty())
			mLoadStatus = "Load failed: parser rejected the animation.";
		return false;
	}
	if (!candidate->mTracks || candidate->mTracks->empty()) {
		mLoadStatus = "Load failed: animation contains no tracks.";
		return false;
	}
	for (const auto& track : *candidate->mTracks) {
		if (track.mFrames.empty()) {
			mLoadStatus = "Load failed: animation contains an empty track.";
			return false;
		}
	}
	if (candidate->mFPS <= 0.0f)
		candidate->mFPS = 12.0f;

	ClearLoadedAnimation();
	mTargetPath = requestedPath;
	mExtraImageDirs.clear();
	for (const auto& directory : requestedExtraDirs)
		AddUniqueDirectory(mExtraImageDirs, directory);
	mImageSearchDirs = BuildImageSearchDirs(mTargetPath, mExtraImageDirs);
	mReanimPtr = candidate.release();
	mReanimPtr->mResourceManager = &mResourceManager;

	const size_t totalImages = mReanimPtr->mImagesSet->size();
	size_t loadedImages = 0;
	for (const auto& imageId : *mReanimPtr->mImagesSet) {
		sgf::String resolvedPath;
		if (TryLoadImageResource(imageId, resolvedPath)) {
			++loadedImages;
			continue;
		}

		mMissingImages.push_back(imageId);
		if (!RegisterPlaceholderImage(imageId)) {
			mLoadStatus = "Load failed: could not allocate image placeholder.";
			ClearLoadedAnimation();
			return false;
		}
	}

	mTrackShowList.assign(mReanimPtr->mTracks->size(), 1);
	mAnimator = new sgf::Animator(mReanimPtr);
	UpdateAnimatorState();
	SelectInitialWorkRange();
	mReanimSpeed = 1.0f;
	mScale = 1.0f;
	mDragDelta = { 0, 0 };
	mMapPos = { 0, 0 };
	mCenterOnNextFrame = true;

	std::ostringstream status;
	status << "Loaded " << BaseName(mTargetPath) << " ("
		<< loadedImages << "/" << totalImages << " images";
	if (!mMissingImages.empty())
		status << ", " << mMissingImages.size() << " transparent placeholders";
	status << ").";
	mLoadStatus = status.str();
	SetWindowCaptain((sgf::String("PVZ Reanim Viewer - ") + BaseName(mTargetPath)).c_str());
	std::cout << mLoadStatus << std::endl;
	return true;
}

void ViewerApp::UpdateAnimatorState()
{
	if (!mAnimator)
		return;
	const size_t count = std::min(mAnimator->mExtraInfos.size(), mTrackShowList.size());
	for (size_t i = 0; i < count; i++)
	{
		mAnimator->mExtraInfos[i].mVisible = mTrackShowList[i] != 0;
	}
}

void ViewerApp::PresentAnimator()
{
	if (!mAnimator || mWindowSize.x <= 0.0f || mWindowSize.y <= 20.0f)
		return;
	mGraphics->SetCubeColor({ 1,1,1,1 });
	mGraphics->MoveTo(0, 0);
	mGraphics->SetClipRect({ mWindowPos.x ,mWindowPos.y + 20,mWindowSize.x ,mWindowSize.y - 20 });

	mGraphics->MoveTo(mMapPos.x + mDragDelta.x, mMapPos.y + mDragDelta.y - 20);
	mAnimator->PresentMatrix(mGraphics, glm::scale(glm::mat4x4(1.0f), glm::vec3(mScale, mScale, 1.0f)));

	mGraphics->ClearClipRect();
}
