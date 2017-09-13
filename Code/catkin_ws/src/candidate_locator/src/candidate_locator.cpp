#include <iostream>
#include <sstream>

#include "candidate_locator.h"

CandidateLocator::CandidateLocator() : it_(nh_), tf_listener_(ros::Duration(60))
{
  sub_depth_cam_info_ = it_.subscribeCamera("camera/depth/image_raw", 1000, &CandidateLocator::cameraInfoCallback, this);

  // pub_point_clouds_ = nh_.advertise<candidate_locator::ArrayPointClouds>("/candidate_point_clouds", 1);

  ROS_INFO("Constructed CandidateLocator.");
}

void CandidateLocator::candidatesCallback(const object_candidates::SnapshotMsg& msg)
{
  // pub_point_clouds_.publish(locateCandidates(
  //   msg.depth_image,
  //   msg.rgb_image,
  //   msg.rgb_info,
  //   msg.candidates));
}

candidate_locator::ArrayPointClouds CandidateLocator::locateCandidates(
  const sensor_msgs::Image& depth_image,
  const sensor_msgs::Image& rgb_image,
  const sensor_msgs::CameraInfo& rgb_info,
  const object_candidates::ArrayImages& candidates)
{
  ROS_INFO_STREAM("");
  ROS_INFO_STREAM("----------");

  // If cam_model_ has not been assigned (i.e. the cameraInfoCallback method has not
  // run at least once) then the localisation won't work; hence, return straight away.
  // Commented out when method signature changed to have a return value
  // if (!cam_model_assigned) { return; }

  candidate_locator::ArrayPointClouds array_pc_msg;

  // Assign depth image to member variable
  try
  {
    depth_image_ = cv_bridge::toCvCopy(depth_image, "32FC1")->image;
  }
  catch (cv_bridge::Exception& e)
  {
    ROS_ERROR("%s. Encoding of image is %s", e.what(), depth_image.encoding.c_str());
  }

  // Get transforms
  this->getTransforms(depth_image.header.stamp);

  // Variables for candidate localisation
  cv::Mat candidate;

  ROS_INFO_STREAM("Number of candidates: " << candidates.data.size());

  // Iterate through candidates and localise
  for(uint i = 0; i < candidates.data.size(); i++)
  {
    try
    {
      candidate = cv_bridge::toCvCopy(candidates.data[i], "mono8")->image;
    }
    catch (cv_bridge::Exception& e)
    {
      ROS_ERROR("%s", e.what());
    }

    pcl::PointCloud<pcl::PointXYZRGB> point_cloud;
    sensor_msgs::PointCloud2 pc_msg;

    ROS_INFO_STREAM("Candidate " << candidate_id_ << ": ");
    this->calculateObjectPoints(candidate, point_cloud);

    pcl::toROSMsg(point_cloud, pc_msg);
    pc_msg.header.frame_id = "/camera_depth_frame";
    //pc_msg.header.frame_id = "/real_camera_pose";
    
    // pcl_ros::transformPointCloud(
    //   "/map",
    //   map_transform_,
    //   pc_msg,
    //   pc_msg);

    array_pc_msg.data.push_back(pc_msg);
  }

  return array_pc_msg;

}

void CandidateLocator::getTransforms(const ros::Time& stamp)
{
  ROS_INFO("Requesting transforms for timestamp %d.%d", stamp.sec, stamp.nsec);
  
  try
  {
    tf_listener_.waitForTransform(
      "/map",
      "/camera_depth_frame",
      //"/real_camera_pose",
      stamp,
      ros::Duration(2.0));

    tf_listener_.lookupTransform(
      "/map",
      "/camera_depth_frame",
      //"/real_camera_pose",
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

void CandidateLocator::calculateObjectPoints(cv::Mat& candidate, pcl::PointCloud<pcl::PointXYZRGB>& point_cloud)
{
  // Calculate desired "colour" from candidate_id_
  uint red, green, blue = 0;
  red = candidate_id_ / 256;
  green = candidate_id_ % 256;
  ROS_DEBUG_STREAM("Candidate " << candidate_id_ << ": red = " << red << " green = " << green);
  candidate_id_++;

  // Code adapted from function GazeboRosOpenniKinect::FillPointCloudHelper

  uint candidate_size = 0;
  uint channels = candidate.channels();
  uint nRows = candidate.rows;
  uint nCols = candidate.cols * channels;

  double point_cloud_cutoff = 0.4;
  double point_cloud_cutoff_max = 5.0;

  // double hfov = this->parentSensor->DepthCamera()->HFOV().Radian();
  // TODO: set hfov dynamically from camera info (if possible)
  double hfov = 1.021018; // 58.5 degrees
  double fl = (double)nCols / (2.0 * tan(hfov/2.0));
  
  // convert depth to point cloud
  for (uint32_t j=0; j<nCols; j++)
  {
    double pAngle;
    if (nCols>1) pAngle = atan2( (double)j - 0.5*(double)(nCols-1), fl);
    else            pAngle = 0.0;

    for (uint32_t i=0; i<nRows; i++)
    {
      if (candidate.ptr<uchar>(i)[nCols-1-j] != 0)
      {
        candidate_size++;

        double yAngle;
        if (nRows>1) yAngle = atan2( (double)i - 0.5*(double)(nRows-1), fl);
        else            yAngle = 0.0;

        double depth = depth_image_.at<float>(i, nCols-j);

        if(depth > point_cloud_cutoff &&
           depth < point_cloud_cutoff_max)
        {
          pcl::PointXYZRGB pclPoint;
          pclPoint.x = depth * tan(yAngle);
          pclPoint.y = depth * tan(pAngle);
          pclPoint.z = depth;
          pclPoint.r = red;
          pclPoint.g = green;
          pclPoint.b = blue;
          point_cloud.push_back(pclPoint);
        }
        // else //point in the unseeable range
        // {
        //   *iter_x = *iter_y = *iter_z = std::numeric_limits<float>::quiet_NaN ();
        //   point_cloud_msg.is_dense = false;
        // }
      }
    }
  }

  ROS_INFO_STREAM("Point cloud created for candidate. Candidate size: " << candidate_size);
}

void CandidateLocator::cameraInfoCallback(const sensor_msgs::ImageConstPtr& depth_img_msg, const sensor_msgs::CameraInfoConstPtr& info_msg)
{
  cam_model_.fromCameraInfo(info_msg);

  if(cam_model_assigned == false)
  {
    cam_model_assigned = true;
    ROS_INFO("Camera model assigned.");
    ROS_INFO_STREAM("cx: " << cam_model_.cx() << " cy: " << cam_model_.cy());
    ROS_INFO_STREAM("fx: " << cam_model_.fx() << " fy: " << cam_model_.fy());
    ROS_INFO_STREAM("Tx: " << cam_model_.Tx() << " Ty: " << cam_model_.Ty());
  }
}
