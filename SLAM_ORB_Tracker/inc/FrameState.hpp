//
//  FrameState.hpp
//  SLAM_ORB_Tracker
//
//  Created by Sen Yu on 3/20/16.
//  Copyright © 2016 Sen Yu. All rights reserved.
//

#ifndef FrameState_hpp
#define FrameState_hpp

#include "stdafx.hpp"

class FrameState {
private:
    bool loadImage(int _id);
    
public:
    int mId;
    bool mLoaded;
    cv::Mat mImage;
    
    FrameState(int _id);
};

#endif /* FrameState_hpp */