#include "GuiGameList.h"
#include "../InputManager.h"
#include <iostream>
#include "GuiMenu.h"
#include "GuiFastSelect.h"
#include <boost/filesystem.hpp>
#include "../Log.h"
#include "../Settings.h"


std::vector<FolderData::SortState> GuiGameList::sortStates;

bool GuiGameList::isDetailed() const
{
	if(mSystem == NULL)
		return false;

	return mSystem->hasGamelist();
}

GuiGameList::GuiGameList(Window* window) : GuiComponent(window), 
	mTheme(new ThemeComponent(mWindow)),
	mList(window, 0.0f, 0.0f, Font::get(*window->getResourceManager(), Font::getDefaultPath(), FONT_SIZE_MEDIUM)), 
	mImages(2, ImageComponent(window)),
	mDescription(window), 
	mDescContainer(window), 
	mTransitionImage(window, 0.0f, 0.0f, "", (float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight(), true), 
	mHeaderText(mWindow), 
    sortStateIndex(Settings::getInstance()->getInt("GameListSortIndex")),
	mLockInput(false),
	mEffectFunc(NULL), mEffectTime(0), mGameLaunchEffectLength(700)
{
	//first object initializes the vector
	if (sortStates.empty()) {
		sortStates.push_back(FolderData::SortState(FolderData::compareFileName, true, "file name, ascending"));
		sortStates.push_back(FolderData::SortState(FolderData::compareFileName, false, "file name, descending"));
		sortStates.push_back(FolderData::SortState(FolderData::compareRating, true, "database rating, ascending"));
		sortStates.push_back(FolderData::SortState(FolderData::compareRating, false, "database rating, descending"));
		sortStates.push_back(FolderData::SortState(FolderData::compareUserRating, true, "your rating, ascending"));
		sortStates.push_back(FolderData::SortState(FolderData::compareUserRating, false, "your rating, descending"));
        sortStates.push_back(FolderData::SortState(FolderData::compareTimesPlayed, true, "played least often"));
        sortStates.push_back(FolderData::SortState(FolderData::compareTimesPlayed, false, "played most often"));
		sortStates.push_back(FolderData::SortState(FolderData::compareLastPlayed, true, "played least recently"));
		sortStates.push_back(FolderData::SortState(FolderData::compareLastPlayed, false, "played most recently"));
	}

    for (	std::vector<ImageComponent>::iterator it = mImages.begin();
			it != mImages.end();
			++it) {
		mImageAnimation.addChild(&(*it));
	}
	mDescContainer.addChild(&mDescription);

	//scale delay with screen width (higher width = more text per line)
	//the scroll speed is automatically scaled by component size
	mDescContainer.setAutoScroll((int)(1500 + (Renderer::getScreenWidth() * 0.5)), 0.025f);

	mTransitionImage.setPosition((float)Renderer::getScreenWidth(), 0);
	mTransitionImage.setOrigin(0, 0);

	mHeaderText.setColor(0xFF0000FF);
	mHeaderText.setFont(Font::get(*mWindow->getResourceManager(), Font::getDefaultPath(), FONT_SIZE_LARGE));
	mHeaderText.setPosition(0, 1);
	mHeaderText.setSize((float)Renderer::getScreenWidth(), 0);
	mHeaderText.setCentered(true);

	addChild(mTheme);
	addChild(&mHeaderText);
	for (	std::vector<ImageComponent>::iterator it = mImages.begin();
			it != mImages.end();
			++it) {
		addChild(&(*it));
    }
	addChild(&mDescContainer);
	addChild(&mList);
	addChild(&mTransitionImage);

	mTransitionAnimation.addChild(this);

	setSystemId(0);
}

GuiGameList::~GuiGameList()
{
	delete mTheme;
}

void GuiGameList::setSystemId(int id)
{
	if(SystemData::sSystemVector.size() == 0)
	{
		LOG(LogError) << "Error - no systems found!";
		return;
	}

	//make sure the id is within range
	if(id >= (int)SystemData::sSystemVector.size())
		id -= SystemData::sSystemVector.size();
	if(id < 0)
		id += SystemData::sSystemVector.size();

	mSystemId = id;
	mSystem = SystemData::sSystemVector.at(mSystemId);

	//clear the folder stack
	while(mFolderStack.size()){ mFolderStack.pop(); }

	mFolder = mSystem->getRootFolder();

	updateTheme();
	updateList();
	updateDetailData();
	mWindow->normalizeNextUpdate(); //image loading can be slow
}

void GuiGameList::render(const Eigen::Affine3f& parentTrans)
{
	Eigen::Affine3f trans = parentTrans * getTransform();
	renderChildren(trans);
}

bool GuiGameList::input(InputConfig* config, Input input)
{	
	if(mLockInput)
		return false;

	mList.input(config, input);

	if(config->isMappedTo("a", input) && mFolder->getFileCount() > 0 && input.value != 0)
	{
		//play select sound
		mTheme->getSound("menuSelect")->play();

		FileData* file = mList.getSelectedObject();
		if(file->isFolder()) //if you selected a folder, add this directory to the stack, and use the selected one
		{
			mFolderStack.push(mFolder);
			mFolder = (FolderData*)file;
			updateList();
			updateDetailData();
			return true;
		}else{
			mList.stopScrolling();

			mEffectFunc = &GuiGameList::updateGameLaunchEffect;
			mEffectTime = 0;
			mGameLaunchEffectLength = (int)mTheme->getSound("menuSelect")->getLengthMS();
			if(mGameLaunchEffectLength < 800)
				mGameLaunchEffectLength = 800;

			mLockInput = true;

			return true;
		}
	}

	//if there's something on the directory stack, return to it
	if(config->isMappedTo("b", input) && input.value != 0 && mFolderStack.size())
	{
		mFolder = mFolderStack.top();
		mFolderStack.pop();
		updateList();
		updateDetailData();

		//play the back sound
		mTheme->getSound("menuBack")->play();

		return true;
	}

	//only allow switching systems if more than one exists (otherwise it'll reset your position when you switch and it's annoying)
	if(SystemData::sSystemVector.size() > 1 && input.value != 0)
	{
		if(config->isMappedTo("right", input))
		{
			setSystemId(mSystemId + 1);
			doTransition(-1);
			return true;
		}
		if(config->isMappedTo("left", input))
		{
			setSystemId(mSystemId - 1);
			doTransition(1);
			return true;
		}
	}

	//change sort order
	if(config->isMappedTo("sortordernext", input) && input.value != 0) {
		setNextSortIndex();
		//std::cout << "Sort order is " << FolderData::getSortStateName(sortStates.at(sortStateIndex).comparisonFunction, sortStates.at(sortStateIndex).ascending) << std::endl;
	}
	else if(config->isMappedTo("sortorderprevious", input) && input.value != 0) {
		setPreviousSortIndex();
		//std::cout << "Sort order is " << FolderData::getSortStateName(sortStates.at(sortStateIndex).comparisonFunction, sortStates.at(sortStateIndex).ascending) << std::endl;
	}

	//open the "start menu"
	if(config->isMappedTo("menu", input) && input.value != 0)
	{
		mWindow->pushGui(new GuiMenu(mWindow, this));
		return true;
	}

	//open the fast select menu
	if(config->isMappedTo("select", input) && input.value != 0)
	{
        mWindow->pushGui(new GuiFastSelect(mWindow, this, &mList, mList.getSelectedObject()->getName()[0], mTheme));
		return true;
	}

	if(isDetailed())
	{
		if(config->isMappedTo("up", input) || config->isMappedTo("down", input) || config->isMappedTo("pageup", input) || config->isMappedTo("pagedown", input))
		{
			if(input.value == 0)
				updateDetailData();
			else
				clearDetailData();
		}
		return true;
	}

	return false;
}

const FolderData::SortState & GuiGameList::getSortState() const
{
    return sortStates.at(sortStateIndex);
}

void GuiGameList::setSortIndex(size_t index)
{
	//make the index valid
	if (index >= sortStates.size()) {
		index = 0;
	}
	if (index != sortStateIndex) {
		//get sort state from vector and sort list
		sortStateIndex = index;
		sort(sortStates.at(sortStateIndex).comparisonFunction, sortStates.at(sortStateIndex).ascending);
	}
    //save new index to settings
    Settings::getInstance()->setInt("GameListSortIndex", sortStateIndex);
}

void GuiGameList::setNextSortIndex()
{
	//make the index wrap around
	if ((sortStateIndex - 1) >= sortStates.size()) {
		setSortIndex(0);
	}
	setSortIndex(sortStateIndex + 1);
}

void GuiGameList::setPreviousSortIndex()
{
	//make the index wrap around
	if (((int)sortStateIndex - 1) < 0) {
		setSortIndex(sortStates.size() - 1);
	}
	setSortIndex(sortStateIndex - 1);
}

void GuiGameList::sort(FolderData::ComparisonFunction & comparisonFunction, bool ascending)
{
	//resort list and update it
	mFolder->sort(comparisonFunction, ascending);
	updateList();
	updateDetailData();
}

void GuiGameList::updateList()
{
	mList.clear();

	for(unsigned int i = 0; i < mFolder->getFileCount(); i++)
	{
		FileData* file = mFolder->getFile(i);

		if(file->isFolder())
			mList.addObject(file->getName(), file, mTheme->getColor("secondary"));
		else
			mList.addObject(file->getName(), file, mTheme->getColor("primary"));
	}
}

std::string GuiGameList::getThemeFile()
{
	std::string themePath;

	themePath = getHomePath();
	themePath += "/.emulationstation/" +  mSystem->getName() + "/theme.xml";
	if(boost::filesystem::exists(themePath))
		return themePath;

	themePath = mSystem->getStartPath() + "/theme.xml";
	if(boost::filesystem::exists(themePath))
		return themePath;

	themePath = getHomePath();
	themePath += "/.emulationstation/es_theme.xml";
	if(boost::filesystem::exists(themePath))
		return themePath;

	return "";
}

void GuiGameList::updateTheme()
{
	mTheme->readXML(getThemeFile(), isDetailed());

	mList.setSelectorColor(mTheme->getColor("selector"));
	mList.setSelectedTextColor(mTheme->getColor("selected"));
	mList.setScrollSound(mTheme->getSound("menuScroll"));

	mList.setFont(mTheme->getListFont());
	mList.setPosition(0.0f, Font::get(*mWindow->getResourceManager(), Font::getDefaultPath(), FONT_SIZE_LARGE)->getHeight() + 2.0f);

	if(!mTheme->getBool("hideHeader"))
	{
		mHeaderText.setText(mSystem->getDescName());
	}else{
		mHeaderText.setText("");
	}

	if(isDetailed())
	{
		mList.setCentered(mTheme->getBool("listCentered"));

		mList.setPosition(mTheme->getFloat("listOffsetX") * Renderer::getScreenWidth(), mList.getPosition().y());
		mList.setTextOffsetX((int)(mTheme->getFloat("listTextOffsetX") * Renderer::getScreenWidth()));
        
        for (int iImg = 0; iImg < mTheme->getNumGameImages(); iImg++) {
			Eigen::Vector3f imagePos = mTheme->getImagePos(iImg);
            mImages[iImg].setPosition(
					imagePos[0] * Renderer::getScreenWidth(),
					imagePos[1] * Renderer::getScreenHeight());
            mImages[iImg].setOrigin(
					mTheme->getFloat("gameImageOriginX"),
					mTheme->getFloat("gameImageOriginY"));
            mImages[iImg].setResize(
					mTheme->getFloat("gameImageWidth") * Renderer::getScreenWidth(),
					mTheme->getFloat("gameImageHeight") * Renderer::getScreenHeight(), false);
        }


		mDescription.setColor(mTheme->getColor("description"));
		mDescription.setFont(mTheme->getDescriptionFont());
	}else{
		mList.setCentered(true);
		mList.setPosition(0, mList.getPosition().y());
		mList.setTextOffsetX(0);

		//mDescription.setFont(nullptr);
	}
}

void GuiGameList::updateDetailData()
{
	if(!isDetailed())
	{
		mDescription.setText("");
        for (	std::vector<ImageComponent>::iterator it = mImages.begin();
				it != mImages.end();
				++it) {
			(*it).setImage("");
		}
	}else{
		//if we've selected a game
		if(mList.getSelectedObject() && !mList.getSelectedObject()->isFolder())
		{
			//set image to either "not found" image or metadata image
			Eigen::Vector3f imgOffset = Eigen::Vector3f(Renderer::getScreenWidth() * 0.10f, 0, 0);
			for (int iImg = 0; iImg < mImages.size(); iImg++)
			{
				if(((GameData*)mList.getSelectedObject())->getImagePath(iImg).empty())
					mImages[iImg].setImage(mTheme->getString("imageNotFoundPath"));
				else
					mImages[iImg].setImage(((GameData*)mList.getSelectedObject())->getImagePath(iImg));
				
				mImages[iImg].setPosition(mTheme->getImagePos(iImg) - imgOffset);
			}

			mImageAnimation.fadeIn(35);
			mImageAnimation.move(imgOffset.x(), imgOffset.y(), 20);

			//TODO:MJF: think about the best place to put the desc container, maybe just user defined?
			mDescContainer.setPosition(Eigen::Vector3f(
					Renderer::getScreenWidth() * 0.03f, 
					12, 0));
			mDescContainer.setSize(Eigen::Vector2f(Renderer::getScreenWidth() * (mTheme->getFloat("listOffsetX") - 0.03f), Renderer::getScreenHeight() - mDescContainer.getPosition().y()));
			mDescContainer.setScrollPos(Eigen::Vector2d(0, 0));
			mDescContainer.resetAutoScrollTimer();

			mDescription.setPosition(0, 0);
			mDescription.setSize(Eigen::Vector2f(Renderer::getScreenWidth() * (mTheme->getFloat("listOffsetX") - 0.03f), 0));
			mDescription.setText(((GameData*)mList.getSelectedObject())->getDescription());
		}else{
			mDescription.setText("");
			for (	std::vector<ImageComponent>::iterator it = mImages.begin();
					it != mImages.end();
					++it) {
				(*it).setImage("");
			}
		}
	}
}

void GuiGameList::clearDetailData()
{
	if(isDetailed())
	{
		mImageAnimation.fadeOut(35);
		mDescription.setText("");
	}
}

GuiGameList* GuiGameList::create(Window* window)
{
	GuiGameList* list = new GuiGameList(window);
	window->pushGui(list);
	return list;
}

void GuiGameList::update(int deltaTime)
{
	mTransitionAnimation.update(deltaTime);
	mImageAnimation.update(deltaTime);

	if(mEffectFunc != NULL)
	{
		mEffectTime += deltaTime;
		(this->*mEffectFunc)(mEffectTime);
	}

	GuiComponent::update(deltaTime);
}

void GuiGameList::doTransition(int dir)
{
	mTransitionImage.copyScreen();
	mTransitionImage.setOpacity(255);

	//put the image of what's currently onscreen at what will be (in screen coords) 0, 0
	mTransitionImage.setPosition((float)Renderer::getScreenWidth() * dir, 0);

	//move the entire thing offscreen so we'll move into place
	setPosition((float)Renderer::getScreenWidth() * -dir, mPosition[1]);

	mTransitionAnimation.move(Renderer::getScreenWidth() * dir, 0, 50);
}

float lerpFloat(const float& start, const float& end, float t)
{
	if(t <= 0)
		return start;
	if(t >= 1)
		return end;

	return (start * (1 - t) + end * t);
}

Eigen::Vector2f lerpVector2f(const Eigen::Vector2f& start, const Eigen::Vector2f& end, float t)
{
	if(t <= 0)
		return start;
	if(t >= 1)
		return end;

	return (start * (1 - t) + end * t);
}

float clamp(float min, float max, float val)
{
	if(val < min)
		val = min;
	else if(val > max)
		val = max;

	return val;
}

//http://en.wikipedia.org/wiki/Smoothstep
float smoothStep(float edge0, float edge1, float x)
{
    // Scale, and clamp x to 0..1 range
    x = clamp(0, 1, (x - edge0)/(edge1 - edge0));
	
    // Evaluate polynomial
    return x*x*x*(x*(x*6 - 15) + 10);
}

void GuiGameList::updateGameLaunchEffect(int t)
{
	const int endTime = mGameLaunchEffectLength;

	const int zoomTime = endTime;
	const int centerTime = endTime - 50;

	const int fadeDelay = endTime - 600;
	const int fadeTime = endTime - fadeDelay - 100;

    Eigen::Vector2f imageCenter(0.5f, 0.5f);
    if (!mImages.empty()) {
	    imageCenter = Eigen::Vector2f(mImages[0].getCenter());
    }

	if(!isDetailed())
	{
		imageCenter << mList.getPosition().x() + mList.getSize().x() / 2, mList.getPosition().y() + mList.getSize().y() / 2;
	}

	const Eigen::Vector2f centerStart(Renderer::getScreenWidth() / 2, Renderer::getScreenHeight() / 2);

	//remember to clamp or zoom factor will be incorrect with a negative t because squared
	const float tNormalized = clamp(0, 1, (float)t / endTime);

	mWindow->setCenterPoint(lerpVector2f(centerStart, imageCenter, smoothStep(0.0, 1.0, tNormalized)));
	mWindow->setZoomFactor(lerpFloat(1.0f, 3.0f, tNormalized*tNormalized));
	mWindow->setFadePercent(lerpFloat(0.0f, 1.0f, (float)(t - fadeDelay) / fadeTime));

	if(t > endTime)
	{
		//effect done
		mTransitionImage.setImage(""); //fixes "tried to bind uninitialized texture!" since copyScreen()'d textures don't reinit
		mSystem->launchGame(mWindow, (GameData*)mList.getSelectedObject());
		mEffectFunc = &GuiGameList::updateGameReturnEffect;
		mEffectTime = 0;
		mGameLaunchEffectLength = 700;
		mLockInput = false;
	}
}

void GuiGameList::updateGameReturnEffect(int t)
{
	updateGameLaunchEffect(mGameLaunchEffectLength - t);

	if(t >= mGameLaunchEffectLength)
		mEffectFunc = NULL;
}
