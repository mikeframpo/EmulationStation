#ifndef _GAMEIMAGEINFO_H_
#define _GAMEIMAGEINFO_H_

#include "GameImageInfo.h"
#include "Renderer.h"

GameImageInfo::GameImageInfo(int id, Eigen::Vector3f imagePos)
{
	mId = id;
	mImagePos = imagePos;
}

GameImageInfo::GameImageInfo()
{
	mId = -1;
}

int GameImageInfo::getId()
{
	return mId;
}

Eigen::Vector3f GameImageInfo::getImagePos()
{
	return Eigen::Vector3f(
			mImagePos[0] * Renderer::getScreenWidth(),
			mImagePos[1] * Renderer::getScreenHeight(),
			0);
}

#endif
