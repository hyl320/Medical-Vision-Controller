#include"Letterbox.h"
#include<algorithm>
#include<cmath>


cv::Mat Letterbox(const cv::Mat& src, const cv::Size& target_size)
{
	if (src.empty()) {
		return cv::Mat();
	}

	int src_w = src.cols;
	int src_h = src.rows;
	int target_h = target_size.height;
	int target_w = target_size.width;

	float scale = std::min(
		static_cast<float>(target_w) / static_cast<float>(src_w),
		static_cast<float>(target_h) / static_cast<float>(src_h)
	);

	int new_w = static_cast<int>(std::round(src_w * scale));
	int new_h = static_cast<int>(std::round(src_h * scale));

	cv::Mat resized;
	cv::resize(src, resized, cv::Size(new_w, new_h));
	
	int pad_w = target_w - new_w;
	int pad_h = target_h - new_h;

	int left = pad_w / 2;
	int right = pad_w - left;
	int top = pad_h / 2;
	int bottom = pad_h - top;

	cv::Mat output;
	cv::copyMakeBorder(
		resized,
		output,
		top,
		bottom,
		left,
		right,
		cv::BORDER_CONSTANT,
		cv::Scalar(114,114,114)
	);

	return output;

}
