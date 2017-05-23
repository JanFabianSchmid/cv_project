#include <iostream>
#include <sstream>
#include <boost/foreach.hpp>

#include "candidate_locator.h"

CandidateLocator::CandidateLocator() : it_(nh_)
{
  sub_candidates_ = nh_.subscribe("/candidates_snapshot", 1000, &CandidateLocator::candidatesCallback, this);
  sub_depth_cam_info_ = it_.subscribeCamera("camera/depth/image_raw", 1000, &CandidateLocator::cameraInfoCallback, this);

  pub_point_clouds_ = nh_.advertise<candidate_locator::ArrayPointClouds>("/candidate_point_clouds", 1);

  //DEBUG
  pub_debug_ = nh_.advertise<sensor_msgs::PointCloud2>("/candidatePC", 1);

  ROS_INFO("Constructed CandidateLocator.");
}

void CandidateLocator::candidatesCallback(const object_candidates::SnapshotMsg& msg)
{
  ROS_INFO_STREAM("");
  ROS_INFO_STREAM("----------");

  // If cam_model_ has not been assigned (i.e. the cameraInfoCallback method has not
  // run at least once) then the localisation won't work; hence, return straight away.
  if (!cam_model_assigned) { return; }

  candidate_locator::ArrayPointClouds array_pc_msg;

  // Assign depth image to member variable
  try
  {
    depth_image_ = cv_bridge::toCvCopy(msg.depth_image, "32FC1")->image;
  }
  catch (cv_bridge::Exception& e)
  {
    ROS_ERROR("%s. Encoding of image is %s", e.what(), msg.depth_image.encoding.c_str());
  }

  // Get transforms
  this->getTransforms(msg.depth_image.header.stamp);

  // Variables for candidate localisation
  cv::Mat candidate;

  ROS_INFO_STREAM("Number of candidates: " << msg.candidates.data.size());

  // Iterate through candidates and localise
  for(uint i = 0; i < msg.candidates.data.size(); i++)
  {
    try
    {
      candidate = cv_bridge::toCvCopy(msg.candidates.data[i], "mono8")->image;
    }
    catch (cv_bridge::Exception& e)
    {
      ROS_ERROR("%s", e.what());
    }

    pcl::PointCloud<pcl::PointXYZ> point_cloud;
    sensor_msgs::PointCloud2 pc_msg;

    ROS_INFO_STREAM("Candidate " << i << ": ");
    this->calculateObjectPoints(candidate, point_cloud);

    pcl::toROSMsg(point_cloud, pc_msg);
    // pc_msg.height = pc_msg.width = 1;
    pcl_ros::transformPointCloud(
      "/map",
      map_transform_,
      pc_msg,
      pc_msg);

    array_pc_msg.data.push_back(pc_msg);
  }

  pub_point_clouds_.publish(array_pc_msg);

  //DEBUG
  pub_debug_.publish(array_pc_msg.data[0]);

}

void CandidateLocator::getTransforms(const ros::Time& stamp)
{
  ROS_INFO("Requesting transforms for timestamp %d.%d", stamp.sec, stamp.nsec);
  
  try
  {
    tf_listener_.waitForTransform(
      "/map",
      "/camera_optical_frame",
      stamp,
      ros::Duration(2.0));

    tf_listener_.lookupTransform(
      "/map",
      "/camera_optical_frame",
      stamp,
      map_transform_);

    // tf_listener_.lookupTransform(
    //   "/camera_optical_frame",
    //   "/camera_depth_frame",
    //   stamp,
    //   camera_transform_);

    ROS_INFO("Transforms received.");
  }
  catch (tf::TransformException &e)
  {
    ROS_ERROR("%s", e.what());
  }
}

void CandidateLocator::calculateObjectPoints(cv::Mat& I, pcl::PointCloud<pcl::PointXYZ>& point_cloud)
{
  int channels = I.channels();

  int nRows = I.rows;
  int nCols = I.cols * channels;

  int i,j;
  uchar* p;
  
  int pixelCount = 0;

  for(i = 0; i < nRows; ++i)
  {
    p = I.ptr<uchar>(i);
    for ( j = 0; j < nCols; ++j)
    {
      if (p[j] != 0)
      {
        cv::Point imagePoint = cv::Point(i,j);
        
        // TODO The docs state this should be a *rectified* point, but I don't think it currently is
        cv::Point3d point3d = cam_model_.projectPixelTo3dRay(imagePoint);
        
        // TODO Fiddle point3d to get the distance right

        point_cloud.points.push_back(pcl::PointXYZ(point3d.x, point3d.y, point3d.z));

        pixelCount++;
      }
    }
  }

  ROS_DEBUG_STREAM("Size of candidate: " << pixelCount << " pixels.");
}

void CandidateLocator::cameraInfoCallback(const sensor_msgs::ImageConstPtr& depth_img_msg, const sensor_msgs::CameraInfoConstPtr& info_msg)
{
  cam_model_.fromCameraInfo(info_msg);

  if(cam_model_assigned == false)
  {
    cam_model_assigned = true;
    ROS_INFO("Camera model assigned.");
  }
}