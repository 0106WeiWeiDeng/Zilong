# What’s Zilong
Camera traps contain a large proportion of empty images. Zilong is a command line application which can identify these empty images using non-machine learn algorithm, it can run on both Linux and Windows, and it enables multithreading. The algorithm within Zilong is based on the following assumption: camera is fixed, and it will take a sequence of images within a short period of time when triggering. The ideal situation for Zilong is that there is no vegetation can swine in front of camera.
# How to set up Zilong
There are two ways to set up Zilong: 1) using prebuild binaries, 2) building from source.
## Using prebuild binaries (recommended)
Zilong is write by C++, so it can be compiled as standalone executable files, which can run on both Linux and Windows systems without dependencies. The flowing are steps to set up Zilong binaries:
1. Download Zilong form Github. Go to https://github.com/0106WeiWeiDeng/Zilong website. Click a green bottom named “Clone or download”, and then click “download ZIP”. A ZIP file named “Zilong-master.zip” will be downloaded.
2. Add binaries to system path. PATH or Path is an environmental variable in Linux or Windows system that tells shell or cmd which directories to search for executable files. Uncompressing “Zilong-master.zip” will generate a directory named “Zilong-master”. Within “Zilong-master” directory, “Linux” directory contains binaries run on Linux system, “Windows” directory contains binaries run on Windows system. On Windows, “path\to\Zilong-master\Windows”should be added to system path. From the desktop, right click computer icon, choose Properties, click Advanced system settings link, click Environment Variables, in the section System Variables, find the Path environment variables and select it, click Edit, in the Edit environment variable window, click New and then click Browse, in the pop-up window, select “path\to\Zilong-master\Windows”, click OK, close all remaining windows by click OK. On Linux, “/path/to/Zilong-master/Linux” should be add to PATH environment variable. In bash, typing “cd /path/to/Zilong-master/Linux” and Enter, then typing “chmod +x *” and Enter to make binaries get executable permission, typing “vi ~/.bash_profile” and Enter to edit the .bash_profile file, typing “i” (mean insert) and add “export PATH=/path/to/Zilong-master/Linux:$PATH” at the end of file, press “Esc” two times and typing “:wq”(mean write and quit) and Enter, and then typing “source ~/.bash_profile” and Enter.
3. Testing. In bash or Command prompt window, typing “zilong -h” and Enter, it will print help information of Zilong.
## Building from source
Building from source needs many extra works. We don’t recommend user do this.
### Prerequisites
1. g++ of recent version of GCC. 
Linux: https://gcc.gnu.org/wiki/InstallingGCC.
Windows: https://nuwen.net/files/mingw/mingw-16.1.exe (with prebuild Boost)
2. cmake: https://cmake.org/download/ (required for OpenCV building)
3. Opencv 3: https://github.com/opencv/opencv/archive/3.4.7.zip (build as word and no shared libraries)
4. Boost: https://www.boost.org/ (required on Linux)
### Compilation
#### On Linux: 
1. g++ -static -pthread -o zilong zilong.cpp -I/path/to/boost_build/include -L/path/to/boost_build/lib -lboost_system -lboost_filesystem -lboost_program_options \`pkg-config opencv --static --libs --cflags opencv\`
2. g++ -static -o determine_para determine_para.cpp \`pkg-config opencv --static --libs --cflags opencv\`
#### On Windows:
1. g++ -static -o zilong zilong.cpp -I/path_to/MinGW/include -I/path_to_opencv/install/include -L/path_to/install/x64/mingw/staticlib/ -lboost_system -lboost_filesystem -lboost_program_options -lopencv_img_hash -lopencv_world -llibwebp -llibpng -llibjpeg-turbo -llibtiff -llibprotobuf -llibjasper -lade -lIlmImf -lquirc -lzlib
2. g++ -static -o determine_para determine_para.cpp -I/path_to/MinGW/include -I/path_to_opencv/install/include -L/path_to/install/x64/mingw/staticlib/ -lopencv_img_hash -lopencv_world -llibwebp -llibpng -llibjpeg-turbo -llibtiff -llibprotobuf -llibjasper -lade -lIlmImf -lquirc -lzlib
# How to use Zilong
## Using Zilong alone
After set up, typing “zilong -h” and Enter in bash or cmd, it will print the help information which explain Zilong options.
### Examples
1. Linux: zilong --shots 3 --threads 20 --maxLevel 5 --spatialRadius 5 --colorRadius 400 --max_edge_length 100 --num_foreground_pixel 10 --copy_image true --foggy_weather true --img_file_extension .JPG --timestamp 2 --in ~/9_fog --out ~/9_fog_out
2. Windows: zilong --shots 3 --threads 20 --maxLevel 5 --spatialRadius 5 --colorRadius 400 --max_edge_length 100 --num_foreground_pixel 10 --copy_image true --foggy_weather true --img_file_extension .JPG --timestamp 2 --in C:\data\9_fog --out C:\data\9_fog_out
## Using Zilong within R
After set up, Zilong binaries can be treated as system commands (like cd in Linux or CD in Windows). Therefore, Zilong binaries can be call within R by using R build-in system() (or system2()) function. For example:
### Using system() function:
\> try(system("zilong --shots 3 --threads 20 --maxLevel 5 --spatialRadius 5 --colorRadius 400 --max_edge_length 100 --num_foreground_pixel 10 --copy_image true --foggy_weather true --img_file_extension .JPG --timestamp 2 --in ~/9_fog --out ~/9_fog_out_r"))
### Using system2() function: 
\> try(system2("zilong", arg="--shots 3 --threads 20 --maxLevel 5 --spatialRadius 5 --colorRadius 400 --max_edge_length 100 --num_foreground_pixel 10 --copy_image true --foggy_weather true --img_file_extension .JPG --timestamp 2 --in ~/9_fog --out ~/9_fog_out_r2"))
# Using determine_para to get proper value of parameters of Zilong
Zilong is command line application, and it has many parameters. Among these parameters, “num_foreground_pixel”, “max_edge_length”, “colorRadius”, “spatialRadius” and “maxLevel” will directly affect Zilong processing results. The former two are user input criterions corresponding to c1 and c2 mentioned in Method of paper. The latter three are mean-shift algorithm parameters. Zilong employs mean-shift to blur continuous shooting images. Therefore, mean-shift parameters control the degree of blurry of images. After blurring, Zilong computes number of foreground pixels of two continuous shooting images. If the number is larger than user input criterion, zilong would think that there should be color change between these two images. If max length of edge change of these two images is longer than user input criterion, it’s considered that these two images contain edge change.

Therefore, different size of images or different target animals requires different value of these five parameters. As you can see in table 3 in Result, values of mean-shift parameter of 2560\*1920 images are different with that of 4000\*3000 images. Moreover, if users want Zilong to detect only median to large size of animals, users can set larger value to mean-shift parameters to blur little color change, or increase value of criterions to let Zilong ignore little color change and edge change.

To help users set proper value to these five parameters, we provide another command line application – determine_para. It takes two continuous shooting images, timestamp position in image and mean-shift parameters as input, and it will output a huge image (named out.jpeg). The output image contains 2 input images, blur images, color change image, edge change image and values of num_foreground_pixel and max_edge_length of input images. When users change value of mean-shift parameter, the output image will also change. Comparing outputs of different parameters, users can get proper values of the five parameters for Zilong.
