//
// Created by Sen Yu on 3/26/16.
// Copyright (c) 2016 Sen Yu. All rights reserved.
//

#include "Optimizer.hpp"



using std::vector;
using std::list;
using std::map;
using std::set;


namespace g2o {
    using namespace Eigen;
    Vector2d project2d(const Vector3d& v)  {
        Vector2d res;
        res(0) = v(0)/v(2);
        res(1) = v(1)/v(2);
        return res;
    }

    Vector3d unproject2d(const Vector2d& v)  {
        Vector3d res;
        res(0) = v(0);
        res(1) = v(1);
        res(2) = 1;
        return res;
    }

    EdgeSE3ProjectXYZ::EdgeSE3ProjectXYZ() : BaseBinaryEdge<2, Vector2d, VertexSBAPointXYZ, VertexSE3Expmap>() {
    }

    bool EdgeSE3ProjectXYZ::read(std::istream &is) {
        for (int i = 0; i < 2; i++) {
            is >> _measurement[i];
        }
        for (int i = 0; i < 2; i++)
            for (int j = i; j < 2; j++) {
                is >> information()(i, j);
                if (i != j)
                    information()(j, i) = information()(i, j);
            }
        return true;
    }

    bool EdgeSE3ProjectXYZ::write(std::ostream &os) const {

        for (int i = 0; i < 2; i++) {
            os << measurement()[i] << " ";
        }

        for (int i = 0; i < 2; i++)
            for (int j = i; j < 2; j++) {
                os << " " << information()(i, j);
            }
        return os.good();
    }


    void EdgeSE3ProjectXYZ::linearizeOplus() {
        VertexSE3Expmap *vj = static_cast<VertexSE3Expmap *>(_vertices[1]);
        SE3Quat T(vj->estimate());
        VertexSBAPointXYZ *vi = static_cast<VertexSBAPointXYZ *>(_vertices[0]);
        Vector3d xyz = vi->estimate();
        Vector3d xyz_trans = T.map(xyz);

        double x = xyz_trans[0];
        double y = xyz_trans[1];
        double z = xyz_trans[2];
        double z_2 = z * z;

        Matrix<double, 2, 3> tmp;
        tmp(0, 0) = fx;
        tmp(0, 1) = 0;
        tmp(0, 2) = -x / z * fx;

        tmp(1, 0) = 0;
        tmp(1, 1) = fy;
        tmp(1, 2) = -y / z * fy;

        _jacobianOplusXi = -1. / z * tmp * T.rotation().toRotationMatrix();

        _jacobianOplusXj(0, 0) = x * y / z_2 * fx;
        _jacobianOplusXj(0, 1) = -(1 + (x * x / z_2)) * fx;
        _jacobianOplusXj(0, 2) = y / z * fx;
        _jacobianOplusXj(0, 3) = -1. / z * fx;
        _jacobianOplusXj(0, 4) = 0;
        _jacobianOplusXj(0, 5) = x / z_2 * fx;

        _jacobianOplusXj(1, 0) = (1 + y * y / z_2) * fy;
        _jacobianOplusXj(1, 1) = -x * y / z_2 * fy;
        _jacobianOplusXj(1, 2) = -x / z * fy;
        _jacobianOplusXj(1, 3) = 0;
        _jacobianOplusXj(1, 4) = -1. / z * fy;
        _jacobianOplusXj(1, 5) = y / z_2 * fy;
    }

    Vector2d EdgeSE3ProjectXYZ::cam_project(const Vector3d &trans_xyz) const {
        Vector2d proj = project2d(trans_xyz);
        Vector2d res;
        res[0] = proj[0] * fx + cx;
        res[1] = proj[1] * fy + cy;
        return res;
    }

}
int Optimizer::PoseOptimization(shared_ptr<FrameState> pFrame) {

    g2o::SparseOptimizer optimizer;
    g2o::BlockSolverX::LinearSolverType * linearSolver;

    linearSolver = new g2o::LinearSolverDense<g2o::BlockSolverX::PoseMatrixType>();

    g2o::BlockSolverX *pSolver = new g2o::BlockSolverX(linearSolver);

    g2o::OptimizationAlgorithmLevenberg* pAlgoLM = new g2o::OptimizationAlgorithmLevenberg(pSolver);
    optimizer.setAlgorithm(pAlgoLM);

    optimizer.setVerbose(false);

    int nInitialCorrespondences=0;

    // SET FRAME VERTEX
    g2o::VertexSE3Expmap * vSE3 = new g2o::VertexSE3Expmap();
    vSE3->setEstimate(Utils::convertToSE3Quat(pFrame->mT2w));
    vSE3->setId(0);
    vSE3->setFixed(false);
    optimizer.addVertex(vSE3);

    // SET MAP POINT VERTICES
    vector<g2o::EdgeSE3ProjectXYZ*> vpEdges;
    vector<g2o::VertexSBAPointXYZ*> vVertices;
    vector<float> vInvSigmas2;
    vector<size_t> vnIndexEdge;

    const int N = pFrame->mvpMapPoint.size();
    vpEdges.reserve(N);
    vVertices.reserve(N);
    vInvSigmas2.reserve(N);
    vnIndexEdge.reserve(N);

    const float delta = sqrt(5.991);

    g2o::VertexSE3Expmap* vSE3_recov;
    g2o::SE3Quat SE3quat_recov;
    cv::Mat pose;

//    vSE3_recov = static_cast<g2o::VertexSE3Expmap*>(optimizer.vertex(0));
//    SE3quat_recov = vSE3_recov->estimate();
//    pose = Utils::convertToCvMat44(SE3quat_recov);
//    std::cout<<"opt before0: "<<pose<<std::endl;
    int count = 0;
    for(int i=0; i<N; i++)
    {
        shared_ptr<MapPoint> pMP = pFrame->mvpMapPoint[i];
        if(pMP)
        {
            count++;
//            std::cout<< "MapPoint("<<i<<")"<< pMP->mPos<<std::endl;
//            std::cout<< "KeyPoint("<<i<<")"<<(pFrame->mvKeyPoint[i].pt - cv::Point2f( Config::dCx,
//            Config::dCy) )<<std::endl<<std::endl;

            g2o::VertexSBAPointXYZ* vPoint = new g2o::VertexSBAPointXYZ();
            vPoint->setEstimate(Utils::convertToEigenMat31(pMP->mPos));
            vPoint->setId(i+1);
            vPoint->setFixed(true);
            optimizer.addVertex(vPoint);
            vVertices.push_back(vPoint);

            nInitialCorrespondences++;
            pFrame->mvbOutlier[i] = false;

            //SET EDGE
            Eigen::Matrix<double,2,1> obs;
            cv::KeyPoint kpUn = pFrame->mvKeyPoint[i];
            obs << kpUn.pt.x, kpUn.pt.y;

            g2o::EdgeSE3ProjectXYZ* e = new g2o::EdgeSE3ProjectXYZ();

            e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(i+1)));
            e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(0)));
            e->setMeasurement(obs);

            const float invSigma2 = Config::vInvLevelSigma2[kpUn.octave];
            e->setInformation(Eigen::Matrix2d::Identity()*invSigma2);

            g2o::RobustKernelHuber* rk = new g2o::RobustKernelHuber;
            e->setRobustKernel(rk);
            rk->setDelta(delta);

            e->fx = Config::dFx;
            e->fy = Config::dFy;
            e->cx = Config::dCx;
            e->cy = Config::dCy;

            e->setLevel(0);

            optimizer.addEdge(e);

            vpEdges.push_back(e);
            vInvSigmas2.push_back(invSigma2);
            vnIndexEdge.push_back(i);
        }

    }
    std::cout<<"==========N="<<N<<" avaMapPoint"<<count<<std::endl;
    // We perform 4 optimizations, decreasing the inlier region
    // From second to final optimization we include only inliers in the optimization
    // At the end of each optimization we check which points are inliers
    const float chi2[4]={9.210,7.378,5.991,5.991};
    const int its[4]={20,20,10,10};

    int nBad=0;

//    vSE3_recov = static_cast<g2o::VertexSE3Expmap*>(optimizer.vertex(0));
//    SE3quat_recov = vSE3_recov->estimate();
//    pose = Utils::convertToCvMat44(SE3quat_recov);
//    std::cout<<"opt before: "<<pose<<std::endl;

    for(size_t it=0; it<4; it++)
    {
        optimizer.initializeOptimization(0);
        optimizer.optimize(its[it]);


        nBad=0;
        for(size_t i=0, iend=vpEdges.size(); i<iend; i++)
        {
            g2o::EdgeSE3ProjectXYZ* e = vpEdges[i];

            const size_t idx = vnIndexEdge[i];

            if(pFrame->mvbOutlier[idx])
                e->computeError();

            float curError = e->chi2();

            if(curError>chi2[it])
            {
                pFrame->mvbOutlier[idx]=true;
                e->setLevel(1);
                nBad++;
            }
            else if(curError<=chi2[it])
            {
                pFrame->mvbOutlier[idx]=false;
                e->setLevel(0);
            }
        }
        std::cout<<"bad at i"<<it<<" is "<<nBad<<std::endl;
        if(optimizer.edges().size()<10)
            break;
    }

    // Recover optimized pose and return number of inliers
    vSE3_recov = static_cast<g2o::VertexSE3Expmap*>(optimizer.vertex(0));
    SE3quat_recov = vSE3_recov->estimate();
    pose = Utils::convertToCvMat44(SE3quat_recov);
//    std::cout<<"opt after: "<<pose<<std::endl;
    pose.copyTo(pFrame->mT2w);

    return nInitialCorrespondences-nBad;
}

//void Optimizer::LocalBundleAdjustment(shared_ptr<KeyFrameState> pKF, bool pbStopFlag)
//{
//    // Local KeyFrames: First Breath Search from Current Keyframe
//    std::list<shared_ptr<KeyFrameState>> lLocalKeyFrames;
//
//    lLocalKeyFrames.push_back(pKF);
//    pKF->mnBALocalForKF = pKF->mId;
//
//    std::vector<shared_ptr<KeyFrameState>> vNeighKFs = pKF->GetVectorCovisibleKeyFrames();
//    for(int i=0, iend=vNeighKFs.size(); i<iend; i++)
//    {
//        shared_ptr<KeyFrameState> pKFi = vNeighKFs[i];
//        pKFi->mnBALocalForKF = pKF->mpFrame->mId;
//        if(!pKFi->isBad())
//            lLocalKeyFrames.push_back(pKFi);
//    }
//
//    // Local MapPoints seen in Local KeyFrames
//    std::list<shared_ptr<MapPoint>> lLocalMapPoints;
//    for(list<shared_ptr<KeyFrameState>>::iterator lit=lLocalKeyFrames.begin() , lend=lLocalKeyFrames.end(); lit!=lend; lit++)
//    {
//        std::vector<shared_ptr<MapPoint>> vpMPs = (*lit)->GetMapPointMatches();
//        for(vector<shared_ptr<MapPoint>>::iterator vit=vpMPs.begin(), vend=vpMPs.end(); vit!=vend; vit++)
//        {
//            shared_ptr<MapPoint> pMP = *vit;
//            if(pMP)
//            if(!pMP->isBad())
//            if(pMP->mnBALocalForKF!=pKF->mnId)
//            {
//                lLocalMapPoints.push_back(pMP);
//                pMP->mnBALocalForKF=pKF->mpFrame->mId;
//            }
//        }
//    }
//
//    // Fixed Keyframes. Keyframes that see Local MapPoints but that are not Local Keyframes
//    std::list<shared_ptr<KeyFrameState>> lFixedCameras;
//    for(std::list<shared_ptr<MapPoint>>::iterator lit=lLocalMapPoints.begin(), lend=lLocalMapPoints.end(); lit!=lend; lit++)
//    {
//        std::map<shared_ptr<KeyFrameState>,size_t> observations = (*lit)->GetObservations();
//        for(std::map<shared_ptr<KeyFrameState>,size_t>::iterator mit=observations.begin(), mend=observations.end(); mit!=mend; mit++)
//        {
//            shared_ptr<KeyFrameState> pKFi = mit->first;
//
//            if(pKFi->mnBALocalForKF!=pKF->mnId && pKFi->mnBAFixedForKF!=pKF->mnId)
//            {
//                pKFi->mnBAFixedForKF=pKF->mnId;
//                if(!pKFi->isBad())
//                    lFixedCameras.push_back(pKFi);
//            }
//        }
//    }
//
//    // Setup optimizer
//    g2o::SparseOptimizer optimizer;
//    g2o::BlockSolverX::LinearSolverType * linearSolver;
//
//    linearSolver = new g2o::LinearSolverEigen<g2o::BlockSolverX::PoseMatrixType>();
//
//    g2o::BlockSolverX * solver_ptr = new g2o::BlockSolverX(linearSolver);
//
//    g2o::OptimizationAlgorithmLevenberg* solver = new g2o::OptimizationAlgorithmLevenberg(solver_ptr);
//    optimizer.setAlgorithm(solver);
//
//    if(pbStopFlag)
//        optimizer.setForceStopFlag(pbStopFlag);
//
//    long unsigned int maxKFid = 0;
//
//    // SET LOCAL KEYFRAME VERTICES
//    for(std::list<shared_ptr<KeyFrameState>>::iterator lit=lLocalKeyFrames.begin(), lend=lLocalKeyFrames.end(); lit!=lend; lit++)
//    {
//        shared_ptr<KeyFrameState> pKFi = *lit;
//        g2o::VertexSE3Expmap * vSE3 = new g2o::VertexSE3Expmap();
//        vSE3->setEstimate(Converter::toSE3Quat(pKFi->GetPose()));
//        vSE3->setId(pKFi->mnId);
//        vSE3->setFixed(pKFi->mnId==0);
//        optimizer.addVertex(vSE3);
//        if(pKFi->mnId>maxKFid)
//            maxKFid=pKFi->mnId;
//    }
//
//    // SET FIXED KEYFRAME VERTICES
//    for(list<shared_ptr<KeyFrameState>>::iterator lit=lFixedCameras.begin(), lend=lFixedCameras.end(); lit!=lend; lit++)
//    {
//        shared_ptr<KeyFrameState> pKFi = *lit;
//        g2o::VertexSE3Expmap * vSE3 = new g2o::VertexSE3Expmap();
//        vSE3->setEstimate(Converter::toSE3Quat(pKFi->GetPose()));
//        vSE3->setId(pKFi->mnId);
//        vSE3->setFixed(true);
//        optimizer.addVertex(vSE3);
//        if(pKFi->mnId>maxKFid)
//            maxKFid=pKFi->mnId;
//    }
//
//    // SET MAP POINT VERTICES
//    const int nExpectedSize = (lLocalKeyFrames.size()+lFixedCameras.size())*lLocalMapPoints.size();
//
//    vector<g2o::EdgeSE3ProjectXYZ*> vpEdges;
//    vpEdges.reserve(nExpectedSize);
//
//    vector<shared_ptr<KeyFrameState>> vpEdgeKF;
//    vpEdgeKF.reserve(nExpectedSize);
//
//    vector<float> vSigmas2;
//    vSigmas2.reserve(nExpectedSize);
//
//    vector<shared_ptr<MapPoint>> vpMapPointEdge;
//    vpMapPointEdge.reserve(nExpectedSize);
//
//    const float thHuber = sqrt(5.991);
//
//    for(list<shared_ptr<MapPoint>>::iterator lit=lLocalMapPoints.begin(), lend=lLocalMapPoints.end(); lit!=lend; lit++)
//    {
//        shared_ptr<MapPoint> pMP = *lit;
//        g2o::VertexSBAPointXYZ* vPoint = new g2o::VertexSBAPointXYZ();
//        vPoint->setEstimate(Converter::toVector3d(pMP->GetWorldPos()));
//        int id = pMP->mnId+maxKFid+1;
//        vPoint->setId(id);
//        vPoint->setMarginalized(true);
//        optimizer.addVertex(vPoint);
//
//        map<shared_ptr<KeyFrameState>,size_t> observations = pMP->GetObservations();
//
//        //SET EDGES
//        for(map<shared_ptr<KeyFrameState>,size_t>::iterator mit=observations.begin(), mend=observations.end(); mit!=mend; mit++)
//        {
//            shared_ptr<KeyFrameState> pKFi = mit->first;
//
//            if(!pKFi->isBad())
//            {
//                Eigen::Matrix<double,2,1> obs;
//                cv::KeyPoint kpUn = pKFi->GetKeyPointUn(mit->second);
//                obs << kpUn.pt.x, kpUn.pt.y;
//
//                g2o::EdgeSE3ProjectXYZ* e = new g2o::EdgeSE3ProjectXYZ();
//
//                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(id)));
//                e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(pKFi->mnId)));
//                e->setMeasurement(obs);
//                float sigma2 = pKFi->GetSigma2(kpUn.octave);
//                float invSigma2 = pKFi->GetInvSigma2(kpUn.octave);
//                e->setInformation(Eigen::Matrix2d::Identity()*invSigma2);
//
//                g2o::RobustKernelHuber* rk = new g2o::RobustKernelHuber;
//                e->setRobustKernel(rk);
//                rk->setDelta(thHuber);
//
//                e->fx = pKFi->fx;
//                e->fy = pKFi->fy;
//                e->cx = pKFi->cx;
//                e->cy = pKFi->cy;
//
//                optimizer.addEdge(e);
//                vpEdges.push_back(e);
//                vpEdgeKF.push_back(pKFi);
//                vSigmas2.push_back(sigma2);
//                vpMapPointEdge.push_back(pMP);
//            }
//        }
//    }
//
//    optimizer.initializeOptimization();
//    optimizer.optimize(5);
//
//    // Check inlier observations
//    for(size_t i=0, iend=vpEdges.size(); i<iend;i++)
//    {
//        g2o::EdgeSE3ProjectXYZ* e = vpEdges[i];
//        shared_ptr<MapPoint> pMP = vpMapPointEdge[i];
//
//        if(pMP->isBad())
//            continue;
//
//        if(e->chi2()>5.991 || !e->isDepthPositive())
//        {
//            shared_ptr<KeyFrameState> pKFi = vpEdgeKF[i];
//            pKFi->EraseMapPointMatch(pMP);
//            pMP->EraseObservation(pKFi);
//
//            optimizer.removeEdge(e);
//            vpEdges[i]=NULL;
//        }
//    }
//
//    // Recover optimized data
//
//    //Keyframes
//    for(list<shared_ptr<KeyFrameState>>::iterator lit=lLocalKeyFrames.begin(), lend=lLocalKeyFrames.end(); lit!=lend; lit++)
//    {
//        shared_ptr<KeyFrameState> pKF = *lit;
//        g2o::VertexSE3Expmap* vSE3 = static_cast<g2o::VertexSE3Expmap*>(optimizer.vertex(pKF->mnId));
//        g2o::SE3Quat SE3quat = vSE3->estimate();
//        pKF->SetPose(Converter::toCvMat(SE3quat));
//    }
//    //Points
//    for(list<shared_ptr<MapPoint>>::iterator lit=lLocalMapPoints.begin(), lend=lLocalMapPoints.end(); lit!=lend; lit++)
//    {
//        shared_ptr<MapPoint> pMP = *lit;
//        g2o::VertexSBAPointXYZ* vPoint = static_cast<g2o::VertexSBAPointXYZ*>(optimizer.vertex(pMP->mnId+maxKFid+1));
//        pMP->SetWorldPos(Converter::toCvMat(vPoint->estimate()));
//        pMP->UpdateNormalAndDepth();
//    }
//
//    // Optimize again without the outliers
//
//    optimizer.initializeOptimization();
//    optimizer.optimize(10);
//
//    // Check inlier observations
//    for(size_t i=0, iend=vpEdges.size(); i<iend;i++)
//    {
//        g2o::EdgeSE3ProjectXYZ* e = vpEdges[i];
//
//        if(!e)
//            continue;
//
//        shared_ptr<MapPoint> pMP = vpMapPointEdge[i];
//
//        if(pMP->isBad())
//            continue;
//
//        if(e->chi2()>5.991 || !e->isDepthPositive())
//        {
//            shared_ptr<KeyFrameState> pKF = vpEdgeKF[i];
//            pKF->EraseMapPointMatch(pMP->GetIndexInKeyFrame(pKF));
//            pMP->EraseObservation(pKF);
//        }
//    }
//
//    // Recover optimized data
//
//    //Keyframes
//    for(list<shared_ptr<KeyFrameState>>::iterator lit=lLocalKeyFrames.begin(), lend=lLocalKeyFrames.end(); lit!=lend; lit++)
//    {
//        shared_ptr<KeyFrameState> pKF = *lit;
//        g2o::VertexSE3Expmap* vSE3 = static_cast<g2o::VertexSE3Expmap*>(optimizer.vertex(pKF->mnId));
//        g2o::SE3Quat SE3quat = vSE3->estimate();
//        pKF->SetPose(Converter::toCvMat(SE3quat));
//    }
//
//    //Points
//    for(list<shared_ptr<MapPoint>>::iterator lit=lLocalMapPoints.begin(), lend=lLocalMapPoints.end(); lit!=lend; lit++)
//    {
//        shared_ptr<MapPoint> pMP = *lit;
//        g2o::VertexSBAPointXYZ* vPoint = static_cast<g2o::VertexSBAPointXYZ*>(optimizer.vertex(pMP->mnId+maxKFid+1));
//        pMP->SetWorldPos(Converter::toCvMat(vPoint->estimate()));
//        pMP->UpdateNormalAndDepth();
//    }
//}
//
