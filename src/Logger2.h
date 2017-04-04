/*
 * Logger2.h
 *
 *  Created on: 15 Jun 2012
 *      Author: thomas
 */

/**
 * Format is:
 * int32_t at file beginning for frame count
 *
 * For each frame:
 * int64_t: timestamp
 * int32_t: depthSize
 * int32_t: imageSize
 * depthSize * unsigned char: depth_compress_buf
 * imageSize * unsigned char: encodedImage->data.ptr
 */

#ifndef LOGGER2_H_
#define LOGGER2_H_

#include <zlib.h>

#include <limits>
#include <cassert>
#include <iostream>
#include <fstream>

#include <opencv2/opencv.hpp>

#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/thread/condition_variable.hpp>

#include "MemoryBuffer.h"

class Logger2
{
    public:
        Logger2(int width, int height, int fps, std::string folder);
        virtual ~Logger2();

        void startWriting(std::string filename);
        void stopWriting(QWidget * parent);

        void setMatchFile(const std::string file);
        bool grab (size_t index, cv::Mat & depthMat, cv::Mat & rgbMat);

//        bool grab (double stamp, pcl::gpu::PtrStepSz<const unsigned short>& depth, pcl::gpu::PtrStepSz<const RGB>& rgb24);

        struct Association
        {
            double time1, time2;
            std::string name1, name2;
        };

        std::vector< Association > accociations_;

        MemoryBuffer & getMemoryBuffer()
        {
            return memoryBuffer;
        }

        void setMemoryRecord(bool value)
        {
            assert(!writing.getValue());

            logToMemory = value;
        }

        void setCompressed(bool value)
        {
            assert(!writing.getValue());

            compressed = value;
        }

        ThreadMutexObject<std::pair<bool, int64_t> > dropping;
        std::string associatedFile;

        bool readTrue;

        static const int numBuffers = 100;
        std::pair<std::pair<uint8_t *, uint8_t *>, int64_t> frameBuffers[numBuffers];

        ThreadMutexObject<bool> writing;

        cv::Mat depthMat, rgbMat;
        
        int32_t numFrames;

    private:
        MemoryBuffer memoryBuffer;

        int depth_compress_buf_size;
        uint8_t * depth_compress_buf;
        CvMat * encodedImage;

        int lastWritten;
        boost::thread * writeThread;
        std::string filename;
        int64_t lastTimestamp;

        int width;
        int height;
        int fps;

        uint8_t * tcpBuffer;

        void encodeJpeg(cv::Vec<unsigned char, 3> * rgb_data);
        void loggingThread();

        FILE * logFile;

        bool logToMemory;
        bool compressed;

        void logData(int64_t * timestamp,
                     int32_t * depthSize,
                     int32_t * imageSize,
                     unsigned char * depthData,
                     unsigned char * rgbData);

        // add by myself
        std::string folder_;
        bool visualization_;

        std::vector< std::pair<double, std::string> > rgb_stamps_and_filenames_;
        std::vector< std::pair<double, std::string> > depth_stamps_and_filenames_;

        void readFile(const std::string& file, std::vector< std::pair<double, std::string> >& output);

        struct Impl;
        boost::shared_ptr<Impl> impl_;
};

#endif /* LOGGER2_H_ */
