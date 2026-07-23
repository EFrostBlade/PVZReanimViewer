#ifndef __REANIM_VIEWER__
#define __REANIM_VIEWER__


#include "GameApp.h"
#include "Animator.h"
#include "Reanimation.h"
#include "ResourceManager.h"
#include <cstdio>
#include <string>
#include <vector>

class ViewerApp: public sgf::GameApp {
public:
	sgf::Reanimation* mReanimPtr = nullptr;
	sgf::Animator* mAnimator = nullptr;

	sgf::String mTargetPath;
	std::vector<sgf::String> mExtraImageDirs;
	std::vector<sgf::String> mImageSearchDirs;
	std::vector<sgf::String> mMissingImages;
	std::vector<char> mTrackShowList;
	sgf::String mLoadStatus = "No animation loaded.";

	ImVec2 mWindowSize = { 0, 0 };
	ImVec2 mWindowPos = { 0, 0 };
	float mScale = 1.0f;
	ImVec2 mDragDelta = { 0, 0 };
	ImVec2 mMapPos = { 0, 0 };
	bool mFocused = false;
	bool mCenterOnNextFrame = false;
	bool mHasIconFont = false;
	bool mNfdInitialized = false;
	float mReanimSpeed = 1.0f;

	bool mExportDialogRequested = false;
	bool mExporting = false;
	bool mExportFinished = false;
	bool mExportSucceeded = false;
	bool mExportExitWhenDone = false;
	bool mAutoExportFailed = false;
	int mExportFormat = 0;
	int mExportWidth = 512;
	int mExportHeight = 512;
	int mExportFPS = 12;
	int mExportFrameBegin = 0;
	int mExportFrameEnd = 0;
	int mExportFrameCurrent = 0;
	int mExportFramesCaptured = 0;
	int mExportFrameCount = 0;
	float mExportScale = 1.0f;
	bool mExportTransparent = true;
	ImVec4 mExportBackground = { 0.41f, 0.38f, 0.36f, 1.0f };
	sgf::String mExportOutputPath;
	sgf::String mExportStatus = "Ready.";
	std::wstring mExportOutputPathWide;
	std::wstring mExportRawPath;
	std::wstring mExportFfmpegPath;
	FILE* mExportRawFile = nullptr;
	unsigned int mExportFramebuffer = 0;
	unsigned int mExportTexture = 0;
	std::vector<unsigned char> mExportPixels;
	float mExportSavedFrame = 0.0f;
	float mExportSavedRangeBegin = 0.0f;
	float mExportSavedRangeEnd = 0.0f;
	bool mExportSavedPlaying = false;
	sgf::Animator::PlayState mExportSavedPlayState = sgf::Animator::PLAY_NONE;


public:
	ViewerApp();
	~ViewerApp();

public:
	virtual void DrawImgui() override;
	virtual void Draw() override;
	virtual void Update() override;
	virtual void CopeEvent(SDL_Event& theEvent) override;

	void DisplayBenchLayer();
	void DisplayMenuBar();
	void DrawTopLayer();

	bool LoadReanim(const sgf::String& reanimPath, const std::vector<sgf::String>& extraImageDirs);
	bool StartExport(
		const sgf::String& outputPath,
		int width,
		int height,
		float scale,
		int fps,
		bool transparent,
		bool exitWhenDone = false,
		int frameBegin = -1,
		int frameEnd = -1);

	void PresentAnimator();
	void UpdateAnimatorState();

private:
	void ClearLoadedAnimation();
	void DisplayExportDialog();
	bool BeginExport(const sgf::String& outputPath, bool exitWhenDone);
	bool CaptureExportFrame();
	bool EncodeExport();
	void FinishExport(bool succeeded, const sgf::String& status);
	void ReleaseExportResources(bool removeRawFile);
	void RestoreAnimatorAfterExport();
	void OpenExportResult(bool selectInFolder) const;
	sgf::String BuildDefaultExportName() const;
	std::vector<sgf::String> BuildImageSearchDirs(
		const sgf::String& reanimPath,
		const std::vector<sgf::String>& extraImageDirs) const;
	bool TryLoadImageResource(const sgf::String& imageId, sgf::String& resolvedPath);
	bool RegisterPlaceholderImage(const sgf::String& imageId);
	void SelectInitialWorkRange();
};

extern ViewerApp* gViewerApp;

#endif // __REANIM_VIEWER__
