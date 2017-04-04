#include "main.h"

int find_argument(int argc, char** argv, const char* argument_name)
{
    for(int i = 1; i < argc; ++i)
    {
        if(strcmp(argv[i], argument_name) == 0)
        {
            return (i);
        }
    }
    return (-1);
}

int parse_argument(int argc, char** argv, const char* str, int &val)
{
    int index = find_argument(argc, argv, str) + 1;

    if(index > 0 && index < argc)
    {
        val = atoi(argv[index]);
    }

    return (index - 1);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    int width = 640;
    int height = 480;
    int fps = 30;
    std::string folder;

    parse_argument(argc, argv, "-w", width);
    parse_argument(argc, argv, "-h", height);
    parse_argument(argc, argv, "-f", fps);

    MainWindow * window = new MainWindow(width, height, fps);

    window->show();

    return app.exec();
}

MainWindow::MainWindow(int width, int height, int fps)
 : logger(0),
   depthImage(width, height, QImage::Format_RGB888),
   rgbImage(width, height, QImage::Format_RGB888),
   recording(false),
   lastDrawn(0),
   width(width),
   height(height),
   fps(fps)
{
    this->setMaximumSize(width * 2, height + 160);
    this->setMinimumSize(width * 2, height + 160);

    QVBoxLayout * wrapperLayout = new QVBoxLayout;

    QHBoxLayout * mainLayout = new QHBoxLayout;
    QHBoxLayout * fileLayout = new QHBoxLayout;
    QHBoxLayout * optionLayout = new QHBoxLayout;
    QHBoxLayout * buttonLayout = new QHBoxLayout;

    wrapperLayout->addLayout(mainLayout);

    depthLabel = new QLabel(this);
    depthLabel->setPixmap(QPixmap::fromImage(depthImage));
    mainLayout->addWidget(depthLabel);

    imageLabel = new QLabel(this);
    imageLabel->setPixmap(QPixmap::fromImage(rgbImage));
    mainLayout->addWidget(imageLabel);

    wrapperLayout->addLayout(fileLayout);
    wrapperLayout->addLayout(optionLayout);

    QLabel * logLabel = new QLabel("Output file: ", this);
    logLabel->setMaximumWidth(logLabel->fontMetrics().boundingRect(logLabel->text()).width());
    fileLayout->addWidget(logLabel);

    logFile = new QLabel(this);
    logFile->setTextInteractionFlags(Qt::TextSelectableByMouse);
    logFile->setStyleSheet("border: 1px solid grey");
    fileLayout->addWidget(logFile);

#ifdef __APPLE__
    int cushion = 25;
#else
    int cushion = 10;
#endif

    browseButton = new QPushButton("Browse", this);
    browseButton->setMaximumWidth(browseButton->fontMetrics().boundingRect(browseButton->text()).width() + cushion);
    connect(browseButton, SIGNAL(clicked()), this, SLOT(fileBrowse()));
    fileLayout->addWidget(browseButton);

    dateNameButton = new QPushButton("Date filename", this);
    dateNameButton->setMaximumWidth(dateNameButton->fontMetrics().boundingRect(dateNameButton->text()).width() + cushion);
    connect(dateNameButton, SIGNAL(clicked()), this, SLOT(dateFilename()));
    fileLayout->addWidget(dateNameButton);

    autoExposure = new QCheckBox("Auto Exposure");
    autoExposure->setChecked(false);

    autoWhiteBalance = new QCheckBox("Auto White Balance");
    autoWhiteBalance->setChecked(false);

    compressed = new QCheckBox("Compressed");
    compressed->setChecked(true);

    memoryRecord = new QCheckBox("Record to RAM");
    memoryRecord->setChecked(false);

    memoryStatus = new QLabel("");

    connect(autoExposure, SIGNAL(stateChanged(int)), this, SLOT(setExposure()));
    connect(autoWhiteBalance, SIGNAL(stateChanged(int)), this, SLOT(setWhiteBalance()));
    connect(compressed, SIGNAL(released()), this, SLOT(setCompressed()));
    connect(memoryRecord, SIGNAL(stateChanged(int)), this, SLOT(setMemoryRecord()));

    optionLayout->addWidget(autoExposure);
    optionLayout->addWidget(autoWhiteBalance);
    optionLayout->addWidget(compressed);
    optionLayout->addWidget(memoryRecord);
    optionLayout->addWidget(memoryStatus);

    wrapperLayout->addLayout(buttonLayout);

    startStop = new QPushButton("Convert", this);
    connect(startStop, SIGNAL(clicked()), this, SLOT(recordToggle()));
    buttonLayout->addWidget(startStop);

    QPushButton * quitButton = new QPushButton("Quit", this);
    connect(quitButton, SIGNAL(clicked()), this, SLOT(quit()));
    buttonLayout->addWidget(quitButton);

    setLayout(wrapperLayout);

    startStop->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    quitButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QFont currentFont = startStop->font();
    currentFont.setPointSize(currentFont.pointSize() + 8);

    startStop->setFont(currentFont);
    quitButton->setFont(currentFont);

    painter = new QPainter(&depthImage);

//    painter->setPen(Qt::green);
//    painter->setFont(QFont("Arial", 30));
//    painter->drawText(10, 50, "Attempting to start ...");
    depthLabel->setPixmap(QPixmap::fromImage(depthImage));

#ifndef OS_WINDOWS
    char * homeDir = getenv("HOME");
    logFolder.append(homeDir);
    logFolder.append("/");
#else
    char * homeDrive = getenv("HOMEDRIVE");
    char * homeDir = getenv("HOMEPATH");
    logFolder.append(homeDrive);
    logFolder.append("\\");
    logFolder.append(homeDir);
    logFolder.append("\\");
#endif

    logFolder.append("Kinect_Logs");

    boost::filesystem::path p(logFolder.c_str());
    boost::filesystem::create_directory(p);

//    logFile->setText(QString::fromStdString(getNextFilename()));

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(timerCallback()));
    timer->start(15);
}

MainWindow::~MainWindow()
{
    timer->stop();
    delete logger;
}

std::string MainWindow::getNextFilename()
{
    static char const* const fmt = "%Y-%m-%d";
    std::ostringstream ss;

    ss.imbue(std::locale(std::cout.getloc(), new boost::gregorian::date_facet(fmt)));
    ss << boost::gregorian::day_clock::universal_day();

    std::string dateFilename;

    if(!lastFilename.length())
    {
        dateFilename = ss.str();
    }
    else
    {
        dateFilename = lastFilename;
    }

    std::string currentFile;

    int currentNum = 0;

    while(true)
    {
        std::stringstream strs;
        strs << logFolder;
#ifndef OS_WINDOWS
        strs << "/";
#else
        strs << "\\";
#endif
        strs << dateFilename << ".";
        strs << std::setfill('0') << std::setw(2) << currentNum;
        strs << ".klg";

        if(!boost::filesystem::exists(strs.str().c_str()))
        {
            return strs.str();
        }

        currentNum++;
    }

    return "";
}

void MainWindow::dateFilename()
{
    lastFilename.clear();
    logFile->setText(QString::fromStdString(getNextFilename()));
}

void MainWindow::fileBrowse()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open associate File"),
                                                    "/media/fxy/Elements/RGBD_SLAM_TEST_DATABASE/",
                                                    tr("assoc (*.txt)"));

    if(!fileName.isEmpty())
    {
#ifndef OS_WINDOWS
        logFolder = fileName.toStdString().substr(0, fileName.toStdString().rfind("/"));
#else
        logFolder = fileName.toStdString().substr(0, fileName.toStdString().rfind("\\"));
#endif

        std::stringstream strs;
        strs << logFolder;

#ifndef OS_WINDOWS
        strs << "/";
#else
        strs << "\\";
#endif
        strs << "converted";
        strs << ".klg";

        logFile->setText(QString::fromStdString(strs.str()));

        if(!logger)
        {
            logger = new Logger2(width, height, fps, logFolder);

            memset(depthImage.bits(), 0, width * height * 3);
            painter->setPen(Qt::green);
            painter->setFont(QFont("Arial", 30));
            painter->drawText(10, 50, "Starting stream...");
            depthLabel->setPixmap(QPixmap::fromImage(depthImage));
        }
    }
}

void MainWindow::recordToggle()
{
    if(!recording)
    {
        if(logFile->text().length() == 0)
        {
            QMessageBox::information(this, "Information", "You have not selected an assoc.txt file");
        }
        else
        {
            memoryRecord->setEnabled(false);
            compressed->setEnabled(false);
            logger->startWriting(logFile->text().toStdString());
            startStop->setText("Stop");
            recording = true;
        }
    }
    else
    {
        logger->stopWriting(this);
        memoryRecord->setEnabled(true);
        compressed->setEnabled(true);
        startStop->setText("Record");
        recording = false;
        logFile->setText(QString::fromStdString(getNextFilename()));
    }
}

void MainWindow::setExposure()
{
    // logger->getOpenNI2Interface()->setAutoExposure(autoExposure->isChecked());
}

void MainWindow::setWhiteBalance()
{
    // logger->getOpenNI2Interface()->setAutoWhiteBalance(autoWhiteBalance->isChecked());
}

void MainWindow::setCompressed()
{
    if(compressed->isChecked())
    {
        logger->setCompressed(compressed->isChecked());
    }
    else if(!compressed->isChecked())
    {
        if(QMessageBox::question(this, "Compression?", "If you don't have a fast machine or an SSD hard drive you might drop frames, are you sure?", "&No", "&Yes", QString::null, 0, 1 ))
        {
            logger->setCompressed(compressed->isChecked());
        }
        else
        {
            compressed->setChecked(true);
            logger->setCompressed(compressed->isChecked());
        }
    }
}

void MainWindow::setMemoryRecord()
{
    logger->setMemoryRecord(memoryRecord->isChecked());
}

void MainWindow::quit()
{
    if(QMessageBox::question(this, "Quit?", "Are you sure you want to quit?", "&No", "&Yes", QString::null, 0, 1 ))
    {
        if(recording)
        {
            recordToggle();
        }
        this->close();
    }
}

void MainWindow::timerCallback()
{
    // output memory use
    int64_t usedMemory = MemoryBuffer::getUsedSystemMemory();
    int64_t totalMemory = MemoryBuffer::getTotalSystemMemory();
    int64_t processMemory = MemoryBuffer::getProcessMemory();

    float usedGB = (usedMemory / (float)1073741824);
    float totalGB = (totalMemory / (float)1073741824);

#ifdef __APPLE__
float processGB = (processMemory / (float)1073741824);
#else
float processGB = (processMemory / (float)1048576);
#endif

    QString memoryInfo = QString::number(usedGB, 'f', 2) + "/" + QString::number(totalGB, 'f', 2) + "GB memory used, " + QString::number(processGB, 'f', 2) + "GB by Logger2";

    memoryStatus->setText(memoryInfo);

    if(!logger)
    {
//        logger = new Logger2(width, height, fps, logFolder);

//        memset(depthImage.bits(), 0, width * height * 3);
//        painter->setPen(Qt::green);
//        painter->setFont(QFont("Arial", 30));
//        painter->drawText(10, 50, "Starting stream...");
//        depthLabel->setPixmap(QPixmap::fromImage(depthImage));

        return;
    }

    if(!logger->writing.getValue())
    {
        return;
    }

    uint16_t bufferIndex = logger->numFrames % logger->numBuffers;

    // 如果相等说明没获取到新图像
    if(bufferIndex == lastDrawn)
    {
        return;
    }

    // depth 要麻烦些，还要对 depth 图像做后期处理
    memcpy(&depthBuffer[0], logger->frameBuffers[bufferIndex].first.first, width * height * 2);

    // rgb memcpy 过来即可
    memcpy(rgbImage.bits(), logger->frameBuffers[bufferIndex].first.second, width * height * 3);

    // 归一化深度图像用于显示
    cv::Mat1w depth(height, width, (unsigned short *)&depthBuffer[0]);
    normalize(depth, tmp, 0, 255, cv::NORM_MINMAX, 0);

    cv::Mat3b depthImg(height, width, (cv::Vec<unsigned char, 3> *)depthImage.bits());
    cv::cvtColor(tmp, depthImg, CV_GRAY2RGB);

    painter->setPen(recording ? Qt::red : Qt::green);
    painter->setFont(QFont("Arial", 30));
    painter->drawText(10, 50, recording ? ("Recording") : "Viewing");

    depthLabel->setPixmap(QPixmap::fromImage(depthImage));
    imageLabel->setPixmap(QPixmap::fromImage(rgbImage));

    if(logger->getMemoryBuffer().memoryFull.getValue())
    {
        std::cout << "memory full" << std::endl;
        std::cout.flush();

        assert(recording);
        recordToggle();

        QMessageBox msgBox;
        msgBox.setText("Recording has been automatically stopped to prevent running out of system memory.");
        msgBox.exec();
    }

    lastDrawn = bufferIndex;
}
