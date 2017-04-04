/*
 * Logger2.cpp
 *
 *  Created on: 15 Jun 2012
 *      Author: thomas
 */

#include "Logger2.h"

Logger2::Logger2(int width, int height, int fps, std::string folder)
 : dropping(std::pair<bool, int64_t>(false, -1)),
   lastWritten(-1),
   writeThread(0),
   width(width),
   height(height),
   fps(fps),
   logFile(0),
   numFrames(0),
   logToMemory(false),
   compressed(true)
{
    depth_compress_buf_size = width * height * sizeof(int16_t) * 4;
    depth_compress_buf = (uint8_t*)malloc(depth_compress_buf_size);

    encodedImage = 0;
    readTrue = 1;

    writing.assignValue(false);

//    folder_ = "/home/fxy/Kintinuous/rgbd_dataset_freiburg1_desk";
    folder_ = folder;

    // add by myself
    if (folder_[folder_.size() - 1] != '\\' && folder_[folder_.size() - 1] != '/')
        folder_.push_back('/');

    std::cout << "Initializing evaluation from folder: " << folder_ << std::endl;

    associatedFile = folder_ + "assoc.txt";

    setMatchFile(associatedFile);

    for(int i = 0; i < numBuffers; i++)
    {
        uint8_t * newDepth = (uint8_t *)calloc(width * height * 2, sizeof(uint8_t));
        uint8_t * newImage = (uint8_t *)calloc(width * height * 3, sizeof(uint8_t));
        frameBuffers[i] = std::pair<std::pair<uint8_t *, uint8_t *>, int64_t>(std::pair<uint8_t *, uint8_t *>(newDepth, newImage), 0);
    }
//    cv::Mat depthMat, rgbMat;
//    grab(0, depthMat, rgbMat);

}

Logger2::~Logger2()
{
    free(depth_compress_buf);

    assert(!writing.getValue() && "Please stop writing cleanly first");

    if(encodedImage != 0)
    {
        cvReleaseMat(&encodedImage);
    }
}


void Logger2::encodeJpeg(cv::Vec<unsigned char, 3> * rgb_data)
{
    cv::Mat3b rgb(height, width, rgb_data, width * 3);

    IplImage * img = new IplImage(rgb);

    int jpeg_params[] = {CV_IMWRITE_JPEG_QUALITY, 90, 0};

    if(encodedImage != 0)
    {
        cvReleaseMat(&encodedImage);
    }

    encodedImage = cvEncodeImage(".jpg", img, jpeg_params);

    delete img;
}

void Logger2::startWriting(std::string filename)
{
//    if(!writeThread && !writing.getValue() && !logFile)
//    {
//        std::cout << "please open the assoc.txt file first!";
//        return;
//    }

    assert(!writeThread && !writing.getValue() && !logFile);

    lastTimestamp = -1;

    this->filename = filename;

    writing.assignValue(true);

    numFrames = 0;

    if(logToMemory)
    {
        memoryBuffer.clear();
        memoryBuffer.addData((unsigned char *)&numFrames, sizeof(int32_t));
    }
    else
    {
        logFile = fopen(filename.c_str(), "wb+");
        fwrite(&numFrames, sizeof(int32_t), 1, logFile);
    }

     writeThread = new boost::thread(boost::bind(&Logger2::loggingThread,
                                                 this));
}

void Logger2::stopWriting(QWidget * parent)
{
    // assert(writeThread && writing.getValue());
    assert(writing.getValue());    

    writing.assignValue(false);

    // writeThread->join();

    dropping.assignValue(std::pair<bool, int64_t>(false, -1));

    // if(logToMemory)
    // {
    //     memoryBuffer.writeOutAndClear(filename, numFrames, parent);
    // }
    // else
    // {
    //     fseek(logFile, 0, SEEK_SET);
    //     fwrite(&numFrames, sizeof(int32_t), 1, logFile);

    //     fflush(logFile);
    //     fclose(logFile);
    // }

//    writeThread = 0;

//    logFile = 0;

//    numFrames = 0;
}

void Logger2::loggingThread()
{
    if (accociations_.empty())
    {
        std::cout << "Please set match file" << std::endl;
        exit(0);
    }

    while(writing.getValueWait(1000))
    {
        if (numFrames >= accociations_.size() - 1)
        {
            fseek(logFile, 0, SEEK_SET);
            fwrite(&numFrames, sizeof(int32_t), 1, logFile);

            fflush(logFile);
            fclose(logFile);

//            std::cout << "associations.size() " << accociations_.size() << std::cout << std::endl;
//            std::cout.flush();
            
            return;
        }

        std::string depthFile = folder_ + accociations_[numFrames].name2;
        std::string colorFile = folder_ + accociations_[numFrames].name1;
        double temp = accociations_[numFrames].time1;
// int64_t colorTime = temp * 1000000.0;
        int64_t colorTime = temp * 1000000.0;

        cv::Mat depthImage = cv::imread(depthFile, CV_LOAD_IMAGE_ANYDEPTH);
        if(depthImage.empty())
        {
            std::cout << "the depth file " << depthFile  << " cannot read! Please check the file!" << std::endl;
            numFrames++;

            continue;
        }

        if (depthImage.elemSize() != sizeof(unsigned short))
        {
            std::cout << "Image was not opend in 16-bit format. Please use OpenCV 2.3.1 or higher" << std::endl;
            numFrames++;

            exit(1);
        }

        depthImage.convertTo(depthMat, depthImage.type(), 0.2);  // 0.2

        cv::Mat bgr = cv::imread(colorFile);
        if(bgr.empty())
        {
            std::cout << "the bgr file " << colorFile  << " cannot read! Please check the file!" << std::endl;
            numFrames++;

            continue;
        }

        cv::cvtColor(bgr, rgbMat, CV_BGR2RGB);

        uint8_t * depthPtr = (uint8_t *)depthMat.data;
        uint8_t * rgbPtr = (uint8_t *)rgbMat.data;

        uint16_t bufferIndex = (numFrames) % numBuffers;

        memcpy(frameBuffers[bufferIndex].first.first, depthPtr, depthMat.cols * depthMat.rows * 2);
        memcpy(frameBuffers[bufferIndex].first.second, rgbPtr, rgbMat.cols * rgbMat.rows * 3);

        frameBuffers[bufferIndex].second = numFrames;

        unsigned char * rgbData = 0;
        unsigned char * depthData = 0;
        unsigned long depthSize = depth_compress_buf_size;
        int32_t rgbSize = 0;

        if(compressed)
        {
//            boost::thread_group threads;

//            threads.add_thread(new boost::thread(compress2,
//                                                 depth_compress_buf,
//                                                 &depthSize,
//                                                 (const Bytef*)depthPtr,
//                                                 width * height * sizeof(short),
//                                                 Z_BEST_SPEED));

//            threads.add_thread(new boost::thread(boost::bind(&Logger2::encodeJpeg,
//                                                             this,
//                                                             (cv::Vec<unsigned char, 3> *)rgbPtr)));
//            threads.join_all();

            compress2(depth_compress_buf,
                    &depthSize,
                    (const Bytef*)depthPtr,
                    width * height * sizeof(short),
                    Z_BEST_SPEED);
                                                
            Logger2::encodeJpeg((cv::Vec<unsigned char, 3> *)rgbPtr);

            rgbSize = encodedImage->width;

            depthData = (unsigned char *)depth_compress_buf;
            rgbData = (unsigned char *)encodedImage->data.ptr;
        }
        else
        {
            depthSize = width * height * sizeof(short);
            rgbSize = width * height * sizeof(unsigned char) * 3;

            depthData = (unsigned char *)depthPtr;
            rgbData = (unsigned char *)rgbPtr;
        }


        if(logToMemory)
        {
            memoryBuffer.addData((unsigned char *)&numFrames, sizeof(int64_t));
            memoryBuffer.addData((unsigned char *)&depthSize, sizeof(int32_t));
            memoryBuffer.addData((unsigned char *)&rgbSize, sizeof(int32_t));
            memoryBuffer.addData(depthData, depthSize);
            memoryBuffer.addData(rgbData, rgbSize);
        }
        else
        {
std::cout << "after compression rgbSize: " << rgbSize << std::endl;            
std::cout << "after compression depthSize: " << depthSize << std::endl;
std::cout.flush();

            logData((int64_t *)&colorTime,
                    (int32_t *)&depthSize,
                    &rgbSize,
                    depthData,
                    rgbData);
        }

        numFrames++;

//        std::cout << "numFrames: " << numFrames << std::endl;
//        std::cout.flush();
    }

//    while stop user push stop button
    fseek(logFile, 0, SEEK_SET);
    fwrite(&numFrames, sizeof(int32_t), 1, logFile);

    fflush(logFile);
    fclose(logFile);
    
    return;
}

void Logger2::logData(int64_t * timestamp,
                      int32_t * depthSize,
                      int32_t * imageSize,
                      unsigned char * depthData,
                      unsigned char * rgbData)
{
//std::cout << "colorTime: " << *timestamp << std::endl;
//std::cout << "depthSize: " << *depthSize << std::endl;
//std::cout.flush();

    fwrite(timestamp, sizeof(int64_t), 1, logFile);
    fwrite(depthSize, sizeof(int32_t), 1, logFile);
    fwrite(imageSize, sizeof(int32_t), 1, logFile);
    fwrite(depthData, *depthSize, 1, logFile);
    fwrite(rgbData, *imageSize, 1, logFile);
}

bool Logger2::grab (size_t index, cv::Mat & depthMat, cv::Mat & rgbMat)
{
    if (accociations_.empty())
    {
        std::cout << "Please set match file" << std::endl;
        exit(0);
    }

    if ( index >= accociations_.size())
       return false;

    std::string depthFile = folder_ + accociations_[index].name2;
    std::string colorFile = folder_ + accociations_[index].name1;

//    std::cout << "depthFile: " << depthFile << std::endl;
//    std::cout.flush();

    cv::Mat depthImage = cv::imread(depthFile, CV_LOAD_IMAGE_ANYDEPTH);
    if(depthImage.empty())
    {
        return false;
    }

    if (depthImage.elemSize() != sizeof(unsigned short))
    {
        std::cout << "Image was not opend in 16-bit format. Please use OpenCV 2.3.1 or higher" << std::endl;
        exit(1);
    }

    // Datasets are with factor 5000 (pixel to m)
    // http://cvpr.in.tum.de/data/datasets/rgbd-dataset/file_formats#color_images_and_depth_maps

    depthImage.convertTo(depthMat, depthImage.type(), 0.2);

    cv::Mat bgr = cv::imread(colorFile);
    if(bgr.empty())
        return false;

    cv::cvtColor(bgr, rgbMat, CV_BGR2RGB);

    return true;
}

void Logger2::setMatchFile(const std::string file)
{
//   std::string full = folder_ + file;

  std::ifstream iff(file.c_str());

  if(!iff)
  {
    std::cout << "Can't read " << file << std::endl;
    exit(1);
  }

  accociations_.clear();
  while (!iff.eof())
  {
    Association acc;
    iff >> acc.time1 >> acc.name1 >> acc.time2 >> acc.name2;
    accociations_.push_back(acc);
  }
}
