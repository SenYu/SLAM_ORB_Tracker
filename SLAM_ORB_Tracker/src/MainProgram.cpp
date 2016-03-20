//
//  MainProgram.cpp
//  SLAM_ORB_Tracker
//
//  Created by Sen Yu on 3/19/16.
//  Copyright © 2016 Sen Yu. All rights reserved.
//

#include "stdafx.hpp"
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include "Config.hpp"
#include "InputBuffer.hpp"
#include "Vocabulary.hpp"



int main(int argc, char * argv[]) {
  
    // initialize Config for config load
    Config::parse(argc, argv);
    Config::loadConfig(Config::sPathConfigFile);
    
    // initialize InputBuffer for image read
    InputBuffer inputBuffer(Config::sPathImageLoad, Config::iImageLoadBegin, Config::iImageLoadEnd);
    boost::thread inputBufferThread( boost::bind(&InputBuffer::run, &inputBuffer) );
    
    std::cout<< "isVocLoaded 1" <<std::endl;
    Vocabulary vocabulary;
    Config::time("voc");
    bool isVocLoaded = vocabulary.loadFromTextFile("/tmp/voc.txt");
    Config::timeEnd("voc");
    std::cout<< "isVocLoaded" << isVocLoaded<<std::endl;
    
    while(1) {
        
        boost::shared_ptr<FrameState> ptrFrame = inputBuffer.get();
        cv::imshow("inputBuffer", ptrFrame->mImage);
        cv::waitKey(1000);
    }
    
    // wait for quit
    getchar();
    return 0;
}
