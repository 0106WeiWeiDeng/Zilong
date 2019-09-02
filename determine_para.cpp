#include<iostream>
#include<string>
#include<vector>
#include<opencv2/imgproc.hpp>
#include<opencv2/highgui.hpp>

using namespace std;

int maxLevel;
double spatialRadius;
double colorRadius;

void mySobel(cv::Mat& in, cv::Mat& out)
{
	cv::Mat grad_x, grad_y;
	cv::Mat abs_grad_x, abs_grad_y;
	int scale = 1;
	int delta = 0;
	int ddepth = CV_16S;
	cv::Sobel(in, grad_x, ddepth, 1, 0, 3, scale, delta, cv::BORDER_DEFAULT);
	cv::convertScaleAbs(grad_x, abs_grad_x);
	cv::Sobel(in, grad_y, ddepth, 0, 1, 3, scale, delta, cv::BORDER_DEFAULT);
	cv::convertScaleAbs(grad_y, abs_grad_y);
	cv::addWeighted(abs_grad_x, 0.5, abs_grad_y, 0.5, 0, out);
}

// 得到timestamp的行
int get_timestamp_line(cv::Mat img, int p)
{
	int x = 0; // 列
	int y = 0; // 行

	if (p == 1) // timestamp在图片上部分
	{
		cv::Vec3b* q = img.ptr<cv::Vec3b>(9);
		for (int i = 10; i < img.rows; i++)
		{
			cv::Vec3b* p = img.ptr<cv::Vec3b>(i);
			if (abs(int(p[x][0]) - int(q[x][0])) < 5 && abs(int(p[x][1]) - int(q[x][1])) < 5 && abs(int(p[x][2]) - int(q[x][2])) < 5)
				continue;
			else
			{
				y = i;
				break;
			}
		}
		return y - 1;
	}
	else // timestamp在图片的下部分
	{
		cv::Vec3b* q = img.ptr<cv::Vec3b>(img.rows - 9);
		for (int i = img.rows - 10; i > 0; i--)
		{
			cv::Vec3b* p = img.ptr<cv::Vec3b>(i);
			if (abs(int(p[x][0]) - int(q[x][0])) < 5 && abs(int(p[x][1]) - int(q[x][1])) < 5 && abs(int(p[x][2]) - int(q[x][2])) < 5)
				continue;
			else
			{
				y = i;
				break;
			}
		}
		return y + 1;
	}
}

void generate_output_mat(cv::Mat src1, cv::Mat src2, cv::Mat& out)
{
	// 1. 颜色变化
	cv::Mat meanShift_out1, meanShift_out2;
	cv::pyrMeanShiftFiltering(src1, meanShift_out1, spatialRadius, colorRadius, maxLevel);
	cv::pyrMeanShiftFiltering(src2, meanShift_out2, spatialRadius, colorRadius, maxLevel);
	cv::Mat reduce_result;
	cv::absdiff(meanShift_out1, meanShift_out2, reduce_result);
	cv::threshold(reduce_result, reduce_result, 15, 255, cv::THRESH_BINARY);
	int num_foreground_pixel = 0;
	for (int x = 0; x < reduce_result.rows; ++x)
	{
		cv::Vec3b* p_in = reduce_result.ptr<cv::Vec3b>(x);
		for (int y = 0; y < reduce_result.cols; ++y)
		{
			int n = 0;
			if (p_in[y][0] == 255) ++n;
			if (p_in[y][1] == 255) ++n;
			if (p_in[y][2] == 255) ++n;
			if (n == 3) num_foreground_pixel++;
			else { p_in[y][0] = 0; p_in[y][1] = 0; p_in[y][2] = 0; }
		}
	}

	// 2. 轮廓变化
	double max_length = 0.0;
	cv::Mat gray1, gray2, edge1, edge2, out_edge;
	cv::cvtColor(src1, gray1, cv::COLOR_BGR2GRAY);
	cv::cvtColor(src2, gray2, cv::COLOR_BGR2GRAY);
	mySobel(gray1, edge1);
	mySobel(gray2, edge2);
	cv::absdiff(edge1, edge2, out_edge);
	cv::threshold(out_edge, out_edge, 20, 255, cv::THRESH_BINARY);
	cv::Mat elem = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
	cv::erode(out_edge, out_edge, elem);
	vector<vector<cv::Point>> contours;
	cv::findContours(out_edge, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
	for (int x = 0; x < contours.size(); ++x)
	{
		double temp = cv::arcLength(contours[x], false);
		if (temp > max_length) max_length = temp;
	}

	// 3. 产生组合照片结果
	cv::Mat result(cv::Size(src1.cols * 2, src1.rows * 3 + 100), src1.type(), cv::Scalar::all(0));
	cv::Mat mat_roi = result(cv::Rect(0, 0, src1.cols, src1.rows));
	src1.copyTo(mat_roi);
	mat_roi = result(cv::Rect(src1.cols, 0, src1.cols, src1.rows));
	src2.copyTo(mat_roi);
	mat_roi = result(cv::Rect(0, src1.rows, src1.cols, src1.rows));
	meanShift_out1.copyTo(mat_roi);
	mat_roi = result(cv::Rect(src1.cols, src1.rows, src1.cols, src1.rows));
	meanShift_out2.copyTo(mat_roi);
	mat_roi = result(cv::Rect(0, src1.rows * 2, src1.cols, src1.rows));
	reduce_result.copyTo(mat_roi);
	mat_roi = result(cv::Rect(src1.cols, src1.rows * 2, src1.cols, src1.rows));
	cv::cvtColor(out_edge, out_edge, cv::COLOR_GRAY2BGR);
	out_edge.copyTo(mat_roi);

	// 添加文字：num_foreground_pixel，max_edge_lenghth
	char text[100];
	sprintf(text, "num_foreground_pixel: %d, max_edge_length: %f", num_foreground_pixel, max_length);
	mat_roi = result(cv::Rect(0, src1.rows * 3, src1.cols * 2, 100));
	cv::putText(mat_roi, string(text), cv::Point(src1.cols * 0.7, 80), cv::FONT_HERSHEY_SIMPLEX, 2.0, cv::Scalar(0, 0, 255), 5, 8, false);

	result.copyTo(out);
}


int main(int argc, char** argv)
{
	if (argc != 7)
	{
		cerr << "Usage: " << argv[0] << " img1 img2 time_stamp_pos spatialRadius colorRadius maxLevel" << endl;
		return -1;
	}

	maxLevel = stoi(string(argv[6]));
	spatialRadius = stod(string(argv[4]));
	colorRadius = stod(string(argv[5]));

	cv::Mat deter_src1, deter_src2, deter_dst;
	cv::Rect roi;
	vector<int> jpeg_out;
	jpeg_out.push_back(cv::IMWRITE_JPEG_QUALITY);
	jpeg_out.push_back(100);
	int timestamp = atoi(argv[3]);
	deter_src1 = cv::imread(argv[1]);
	deter_src2 = cv::imread(argv[2]);

	if (timestamp != 0)
	{
		int line = get_timestamp_line(deter_src1, timestamp);
		if (timestamp == 1) roi = cv::Rect(0, line + 1, deter_src1.cols, deter_src1.rows - line - 1);
		else roi = cv::Rect(0, 0, deter_src1.cols, line);
	}
	else roi = cv::Rect(0, 0, deter_src1.cols, deter_src1.rows);

	if (deter_src1.empty() || deter_src2.empty())
	{
		cerr << "Loading input images error!" << endl;
		return -1;
	}

	generate_output_mat(deter_src1(roi), deter_src2(roi), deter_dst);

	cv::imwrite("out.jpeg", deter_dst, jpeg_out);
}