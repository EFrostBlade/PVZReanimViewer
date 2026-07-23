#ifndef __REANIM_VIEWER__
#define __REANIM_VIEWER__


#include "GameApp.h"
#include "Animator.h"
#include "Reanimation.h"
#include "ResourceManager.h"
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

	void PresentAnimator();
	void UpdateAnimatorState();

private:
	void ClearLoadedAnimation();
	std::vector<sgf::String> BuildImageSearchDirs(
		const sgf::String& reanimPath,
		const std::vector<sgf::String>& extraImageDirs) const;
	bool TryLoadImageResource(const sgf::String& imageId, sgf::String& resolvedPath);
	bool RegisterPlaceholderImage(const sgf::String& imageId);
	void SelectInitialWorkRange();
};

extern ViewerApp* gViewerApp;

#endif // __REANIM_VIEWER__
