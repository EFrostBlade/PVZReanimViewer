#include "ReanimViewer.h"
#include "Constant.h"
#include "IconFonts.h"
#include "GamePacker/GamePacker.h"
#include <nfd.h>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <sstream>

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

	ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
	ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

	DisplayMenuBar();
	
	ImGui::End();

	DisplayBenchLayer();

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
	
	SDL_GL_SwapWindow(mGameWindow);
}

void ViewerApp::Update()
{
	if (mAnimator)
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
			if (ImGui::MenuItem("Open")) {
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
			ImGui::EndMenu();
		}

		if (!mTargetPath.empty()) {
			if (ImGui::MenuItem("Reload")) {
				LoadReanim(mTargetPath, mExtraImageDirs);
			}
		}

		ImGui::Text("FPS: %d", mFPS);
		ImGui::EndMenuBar();
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
