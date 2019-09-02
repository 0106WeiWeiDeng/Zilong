#include<iostream>
#include<chrono>
#include<string>
#include<vector>
#include<thread>
#include<mutex>
#include<fstream>
#include<sstream>
#include<boost/program_options.hpp>
#include<boost/filesystem.hpp>
#include<boost/config.hpp>
#include<opencv2/imgproc.hpp>
#include<opencv2/highgui.hpp>

using namespace std;
namespace po = boost::program_options;
namespace fs = boost::filesystem;

// 计时用的类
class Timer
{
private:
	using clock_t = chrono::high_resolution_clock;
	using second_t = chrono::duration<double, std::ratio<1> >;

	chrono::time_point<clock_t> m_beg;

public:
	Timer() : m_beg(clock_t::now())
	{
	}

	void reset()
	{
		m_beg = clock_t::now();
	}

	double elapsed() const
	{
		return chrono::duration_cast<second_t>(clock_t::now() - m_beg).count();
	}
};

// 线程读取照片出错时，用该互斥锁输出错误信息
mutex cerr_mutex;

// 命令行参数
string in, out, img_file_extension; // 输入输出目录

int shots; // 连拍的次数
int threads; // 使用的线程数
int maxLevel; // mean-shift 的参数
int foreground_ratio; // 照片颜色变化部分的像素个数

double spatialRadius; // mean-shift 的参数
double colorRadius; // mean-shift 的参数
double edge_length; // 处理雾气照片时，轮廓变化阈值

bool copy_img; // 是否复制动物照片到输出目录，默认为否
bool foggy_weather; // 处理的照片是否存在雾气，是则需要用edge_length参数，否则不用，将edge_length设为0

int timestamp; // 0：timestamp没有；1：在照片上部分 2：在照片下部分

cv::Rect roi; // 去除照片中的描述部分

//============================分割线==============================================================

// 得到timestamp的行
int get_timestamp_line(cv::Mat img, int p)
{
    int x = 0; // 列
    int y = 0; // 行

    if(p == 1) // timestamp在图片上部分
    {
        cv::Vec3b* q=img.ptr<cv::Vec3b>(9);
        for(int i=10; i<img.rows; i++)
        {
            cv::Vec3b* p = img.ptr<cv::Vec3b>(i);
            if(abs(int(p[x][0])-int(q[x][0]))<5 && abs(int(p[x][1])-int(q[x][1]))<5 && abs(int(p[x][2])-int(q[x][2]))<5)
            continue;
            else
            {
                y=i;
                break;
            }
        }
        return y-1;
    }
    else // timestamp在图片的下部分
    {
        cv::Vec3b* q=img.ptr<cv::Vec3b>(img.rows-9);
        for(int i=img.rows-10; i>0; i--)
        {
            cv::Vec3b* p = img.ptr<cv::Vec3b>(i);
            if(abs(int(p[x][0])-int(q[x][0]))<5 && abs(int(p[x][1])-int(q[x][1]))<5 && abs(int(p[x][2])-int(q[x][2]))<5)
            continue;
            else
            {
                y=i;
                break;
            }
        }
        return y+1;
    }
}

// soble 算子计算边界
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

// 判断照片是灰度还是彩色的（效率低慢）
bool is_gray(cv::Mat& img)
{
    for(int i=0; i<img.rows; ++i)
    {
        cv::Vec3b* p = img.ptr<cv::Vec3b>(i);
        for(int j=0; j<img.cols; ++j)
        {
            if(!(p[j][0] == p[j][1] && p[j][1] == p[j][2]))
                return false;
        }
    }
    return true;
}

// 颜色变化函数
bool is_color_change(vector<cv::Mat>& images)
{
    for(int i=1; i<images.size(); ++i)
    {
        int num_foreground_pixel = 0;
        cv::Mat shift1, shift2, result_pixel;

        cv::pyrMeanShiftFiltering(images[i], shift1, spatialRadius, colorRadius, maxLevel);
        cv::pyrMeanShiftFiltering(images[i-1], shift2, spatialRadius, colorRadius, maxLevel);
        cv::absdiff(shift1, shift2, result_pixel);
        cv::threshold(result_pixel, result_pixel, 15, 255, cv::THRESH_BINARY);

        for (int x = 0; x < result_pixel.rows; ++x)
        {
            cv::Vec3b* p_in = result_pixel.ptr<cv::Vec3b>(x);
            for (int y = 0; y < result_pixel.cols; ++y)
            {
                int n = 0;
                if (p_in[y][0] == 255) ++n;
                if (p_in[y][1] == 255) ++n;
                if (p_in[y][2] == 255) ++n;
                if (n == 3) num_foreground_pixel++;
            }
        }

        if(num_foreground_pixel > foreground_ratio)
            return true;
    }
    return false;
}

// 轮廓变化函数
bool is_edge_change(vector<cv::Mat>& images)
{
    for(int i=1; i<images.size(); ++i)
    {
        cv::Mat gray1, gray2, edge1, edge2, out_edge;

        cv::cvtColor(images[i], gray1, cv::COLOR_BGR2GRAY);
        cv::cvtColor(images[i-1], gray2, cv::COLOR_BGR2GRAY);

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
            if (temp >= edge_length) return true;
        }
    }
    return false;
}

// 处理没有没有雾气的照片的线程
void thread_no_fog(vector<string>& all_images, vector<int>& animals, size_t start, size_t end)
{
   for(size_t x=start; x<end; x+=shots)
   {
       //标记照片是否能正确读取
       int flag_img_read_err = 0;
       vector<cv::Mat> images;

       for(int i=0; i<shots; i++)
       {
           cv::Mat img = cv::imread(all_images[x+i], cv::IMREAD_COLOR);
           if(img.empty())
           {
               lock_guard<mutex> l3(cerr_mutex);
               cerr << "\nCannot open picture file: \"" << all_images[x+i] << "\"!" << endl;
               cerr << "Skip this group of images!" << endl << endl;
               flag_img_read_err = 1;
           }
           if(flag_img_read_err == 1) break;
           cv::Mat img_roi = img(roi);
           images.push_back(move(img_roi));
       }
       if(flag_img_read_err == 1) continue; // 读取照片出错，跳过该组

       if(is_color_change(images))
           for(int i=0; i<shots; ++i)
               animals[x+i]=1;
       else
           for(int i=0; i<shots; ++i)
               animals[x+i]=0;
   }
}

// 处理有雾气的照片的线程
void thread_fog(vector<string>& all_images, vector<int>& animals, size_t start, size_t end)
{
  for(size_t x=start; x<end; x+=shots)
  {
      //标记照片是否能正确读取
       int flag_img_read_err = 0;
       vector<cv::Mat> images;

       for(int i=0; i<shots; i++)
       {
           cv::Mat img = cv::imread(all_images[x+i], cv::IMREAD_COLOR);
           if(img.empty())
           {
               lock_guard<mutex> l3(cerr_mutex);
               cerr << "\nCannot open picture file: \"" << all_images[x+i] << "\"!" << endl;
               cerr << "Skip this group of images!" << endl << endl;
               flag_img_read_err = 1;
           }
           if(flag_img_read_err == 1) break;
           cv::Mat img_roi = img(roi);
           images.push_back(move(img_roi));
       }
       if(flag_img_read_err == 1) continue; // 读取照片出错，跳过该组

       if(is_gray(images[0])) // 灰度照片只查看颜色变化
       {
            if(is_color_change(images))
                for(int i=0; i<shots; ++i)
                    animals[x+i]=1;
            else
                for(int i=0; i<shots; ++i)
                    animals[x+i]=0;
       }
       else // 彩色照片要轮廓变化以及颜色变化同时满足
       {
            if(is_color_change(images) && is_edge_change(images))
                for(int i=0; i<shots; ++i)
                    animals[x+i]=1;
            else
                for(int i=0; i<shots; ++i)
                    animals[x+i]=0;
       }
  }
}

int main(int argc, char* argv[])
{
    Timer t; // 程序计时用

    // 初始化命令行参数列表
    try
    {
        po::options_description desc("Options");
        desc.add_options()
            ("help,h", "print this help information")
            ("shots", po::value<int>(&shots)->default_value(3), "int, number of continuous shot of camera") // 连拍次数默认为3
            ("threads", po::value<int>(&threads)->default_value(2), "int, number of threads to use") // 使用的线程数默认为2
            ("maxLevel", po::value<int>(&maxLevel)->default_value(4), "double, maxLevel argument for mean-shift function in OpenCV") // 默认maxLevel为4，对应照片为2K分辨率
            ("num_foreground_pixel", po::value<int>(&foreground_ratio)->default_value(10), "int, foreground pixel number") // 默认为0，即最敏感
            ("spatialRadius", po::value<double>(&spatialRadius)->default_value(3.0), "double, spatialRadius argument for mean-shift function in OpenCV") // 默认spatialRadius为3，对应照片为2K分辨率
            ("colorRadius", po::value<double>(&colorRadius)->default_value(300.0), "double, colorRadius argument for mean-shift function in OpenCV") // 默认colorRadius为300， 对应照片为2K分辨率
            ("max_edge_length", po::value<double>(&edge_length)->default_value(0.0), "double, threshold for longest chaged edge length") // 默认为0，即处理无雾气照片
            ("copy_image", po::value<bool>(&copy_img)->default_value(false), "bool, animal images would be copy to output directory")
            ("foggy_weather", po::value<bool>(&foggy_weather)->default_value(false), "bool, processing foggy weather images or not ")
            ("in", po::value<string>(&in), "string, diretory where input images locate")
            ("out", po::value<string>(&out), "string, output diretory")
            ("img_file_extension", po::value<string>(&img_file_extension)->default_value(".JPG"), "string, image file extension")
            ("timestamp", po::value<int>(&timestamp)->default_value(2), "flag for timestamp, 0: no timestamp; 1: timestamp in img's top; 2: timestamp in img's bottom")
            ;

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if(vm.count("help"))
        {
            cout << '\n' << desc << endl << endl;
            return 0;
        }

        if(foggy_weather == false && edge_length != 0.0)
        {
            cerr << '\n' << "Option \"foggy_weather\" or \"edge_length\" was wrong!" << endl << endl;
            return -1;
        }
    }
    catch(const po::error& e)
    {
        cerr << '\n' << e.what() << endl << endl;
        return -1;
    }

    // 判断输入输出目录是否存在
    fs::path in_dir(in), out_dir(out);

    if(fs::exists(in_dir) && fs::exists(out_dir))
    {
        if(!(fs::is_directory(in_dir) && fs::is_directory(out_dir)))
        {
            cerr << '\n' << in << " or " << out << " was not diretory!" << endl << endl;
            return -1;
        }
    }
    else
    {
        cerr << '\n' << "input or output diretory was not exist!" << endl << endl;
        return -1;
    }

    // 读取输入目录中的照片文件
    vector<string> all_images;

    for(fs::directory_entry& x : fs::directory_iterator(in_dir))
    {
        if(x.path().has_extension())
        {
            if(x.path().extension().string() == img_file_extension)
                all_images.push_back(x.path().string());
        }
    }

    // 判断照片数是否能被连拍数整除
    if(all_images.size() % shots != 0)
    {
        cerr << '\n' << "Number of images cannot be divisible by number of continuous shootting!" << endl << endl;
    }

    // 生成roi
    cv::Mat imgx = cv::imread(all_images[0], cv::IMREAD_COLOR);
    if(timestamp!=0)
    {
        int line = get_timestamp_line(imgx, timestamp);
        if(timestamp == 1) roi= cv::Rect(0,line+1,imgx.cols,imgx.rows-line-1);
        else roi= cv::Rect(0,0,imgx.cols,line);
    }
    else roi = cv::Rect(0,0,imgx.cols,imgx.rows);

    // 建一个数组，对应每一张照片是否包含动物，有则设为1，无则设为0
    vector<int> animals;
    for(int i=0; i<all_images.size(); ++i)
        animals.push_back(1);

    sort(all_images.begin(), all_images.end());

    // 输出命令行结果
    cout << "\nARGS:" << endl << endl
        << "input dir: " << in << endl
        << "output dir: " << out << endl
        << "image file extension: " << img_file_extension << endl
        << "shots: " << shots << endl
        << "threads: " << threads << endl
        << "maxLevel: " << maxLevel << endl
        << "foreground_ratio: " << foreground_ratio << endl
        << "spatialRadius: " << spatialRadius << endl
        << "colorRadius: " << colorRadius << endl
        << "edge_length: " << edge_length << endl
        << "copy_image: " << copy_img << endl
        << "foggy_weather: " << foggy_weather << endl
        << "number of images: " << all_images.size() << endl
        << "timestamp: " << timestamp << endl
        << "image roi: " << roi << endl
        << "time elasped: " << t.elapsed() << endl << endl;

    // 处理照片
    cout << "Start processing images, using " << threads << " threads..." << endl << endl;
    t.reset();

    vector<thread> use_thread;
    size_t num_group = all_images.size()/shots;
    size_t big_thread_num = num_group%threads;
    size_t average_group_per_little_thread = num_group/threads;
    size_t average_group_per_big_thread = average_group_per_little_thread + 1;

    if(foggy_weather)
    {
        for(int i=0; i<threads; ++i)
        {
            if(i<big_thread_num)
            {
                size_t n = average_group_per_big_thread;
                use_thread.push_back(thread(thread_fog,ref(all_images),ref(animals), i*n*shots, (i+1)*n*shots-1));
            }
            else
            {
                size_t n = average_group_per_little_thread;
                size_t start_point = average_group_per_big_thread*shots*big_thread_num; // 注意：起始点改变
                size_t res = i - big_thread_num;
                use_thread.push_back(thread(thread_fog,ref(all_images),ref(animals), start_point+res*n*shots, start_point+(res+1)*n*shots-1));
            }
        }
    }
    else
    {
         for(int i=0; i<threads; ++i)
        {
            if(i<big_thread_num)
            {
                size_t n = average_group_per_big_thread;
                use_thread.push_back(thread(thread_no_fog,ref(all_images),ref(animals), i*n*shots, (i+1)*n*shots-1));
            }
            else
            {
                size_t n = average_group_per_little_thread;
                size_t start_point = average_group_per_big_thread*shots*big_thread_num; // 注意：起始点改变
                size_t res = i - big_thread_num; //剩下的小线程
                use_thread.push_back(thread(thread_no_fog,ref(all_images),ref(animals), start_point+res*n*shots, start_point+(res+1)*n*shots-1));
            }
        }
    }

    for(auto it=use_thread.begin(); it!=use_thread.end(); ++it)
        (*it).join();


    // 输出处理结果
    int num_valid=0, num_invalid=0;
    ofstream out_invalid, out_valid;
    ostringstream name_invalid, name_valid;

	if (BOOST_PLATFORM == "Win32")
	{
		name_invalid << out << "\\empty_images.txt" << flush;
		name_valid << out << "\\animal_images.txt" << flush;
	}
	else
	{
		name_invalid << out << "/empty_images.txt" << flush;
		name_valid << out << "/animal_images.txt" << flush;
	}

    out_invalid.open(name_invalid.str());
    out_valid.open(name_valid.str());

    for(int i=0; i<animals.size(); ++i)
    {
        if(animals[i] == 1)
        {
            num_valid++;
            out_valid << all_images[i] << endl;
        }
        else
        {
            num_invalid++;
            out_invalid << all_images[i] << endl;
        }
    }
    out_invalid.close();
    out_valid.close();

    if(copy_img)
    {
        ostringstream dir_invalid, dir_valid;

        if (BOOST_PLATFORM == "Win32")
		{
			dir_invalid << out << "\\empty_images" << flush;
			dir_valid << out << "\\animal_images" << flush;
		}
		else
		{
			dir_invalid << out << "/empty_images" << flush;
			dir_valid << out << "/animal_images" << flush;
		}

        fs::path path_invalid(dir_invalid.str());
        fs::path path_valid(dir_valid.str());

        fs::create_directory(path_invalid);
        fs::create_directory(path_valid);

        for(int i=0; i<animals.size(); ++i)
        {
            if(animals[i] == 1)
            {
                fs::path path_img(all_images[i]);
				fs::path out_file_path;
				if (BOOST_PLATFORM == "Win32") out_file_path = fs::path(path_valid.string() + "\\" + path_img.filename().string());
				else out_file_path = fs::path(path_valid.string() + "/" + path_img.filename().string());
                fs::copy_file(path_img, out_file_path);
            }
            else
            {
                fs::path path_img(all_images[i]);
				fs::path out_file_path;
				if (BOOST_PLATFORM == "Win32") out_file_path = fs::path(path_invalid.string() + "\\" + path_img.filename().string());
				else out_file_path = fs::path(path_invalid.string() + "/" + path_img.filename().string());
				fs::copy_file(path_img, out_file_path);
            }
        }
    }

    cout << "Processing complete!" << endl
        << "Summary:" << endl
        << "All images: " << all_images.size() << endl
        << "Empty images: " << num_invalid << endl
        << "Animal images: " << num_valid << endl
        << "Time elasped: " << t.elapsed() << " seconds" << endl << endl;
}
