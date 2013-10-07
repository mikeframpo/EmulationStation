
#include <Eigen/Dense>

class GameImageInfo {
public:
	GameImageInfo(int id, Eigen::Vector3f imagePos);
	GameImageInfo();
	int getId();
	Eigen::Vector3f getImagePos();

private:
	int mId;
	Eigen::Vector3f mImagePos;
};

