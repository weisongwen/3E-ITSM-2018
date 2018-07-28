#include <ros/ros.h>

#include <pcl_conversions/pcl_conversions.h>
#include <pcl/PCLPointCloud2.h>
#include <pcl/conversions.h>
#include <pcl_ros/transforms.h>
#include <pcl_ros/point_cloud.h>

#include <pcl/ModelCoefficients.h>
#include <pcl/point_types.h>
#include <pcl/point_types.h>
#include <pcl/filters/passthrough.h>
#include <pcl_conversions/pcl_conversions.h>
#include <sensor_msgs/PointCloud2.h>  
#include <pcl/filters/extract_indices.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/filters/conditional_removal.h>

#include <pcl/features/normal_3d.h>
#include <pcl/features/normal_3d_omp.h>
#include <pcl/features/don.h>
#include <pcl/features/fpfh_omp.h>

#include <pcl/kdtree/kdtree.h>

#include <pcl/sample_consensus/method_types.h>
#include <pcl/sample_consensus/model_types.h>

#include <pcl/segmentation/sac_segmentation.h>
#include <pcl/segmentation/extract_clusters.h>
#include <pcl/segmentation/conditional_euclidean_clustering.h>

#include <pcl/common/common.h>

#include <pcl/search/organized.h>
#include <pcl/search/kdtree.h>

#include <pcl/segmentation/extract_clusters.h>

#include <visualization_msgs/MarkerArray.h>
#include <visualization_msgs/Marker.h>

#include <std_msgs/Float32MultiArray.h>
#include <std_msgs/MultiArrayLayout.h>
#include <std_msgs/MultiArrayDimension.h>

#include <lidar_tracker/centroids.h>
#include <lidar_tracker/CloudCluster.h>
#include <lidar_tracker/CloudClusterArray.h>

#include <vector_map_server/PositionState.h>

#include <jsk_recognition_msgs/BoundingBox.h>
#include <jsk_recognition_msgs/BoundingBoxArray.h>
#include <jsk_rviz_plugins/Pictogram.h>
#include <jsk_rviz_plugins/PictogramArray.h>

#include <tf/tf.h>

#include <limits>
#include <cmath>

#include <opencv/cv.h>
#include <opencv/highgui.h>
#include <opencv2/core/version.hpp>
#if (CV_MAJOR_VERSION == 3)
#include "gencolors.cpp"
#else
#include <opencv2/contrib/contrib.hpp>
#endif

#include <chrono>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <math.h>
#include "Cluster.h"

#include <tf/transform_datatypes.h>
//#include <vector_map/vector_map.h>
//#include <vector_map_server/GetSignal.h>
#include "geometry_msgs/Pose.h"
#include "geometry_msgs/PoseArray.h"

#ifdef GPU_CLUSTERING
	#include "gpu_euclidean_clustering.h"
#endif

using namespace cv;

std::vector<cv::Scalar> _colors;
ros::Publisher _pub_cluster_cloud;
ros::Publisher _pub_ground_cloud;
ros::Publisher _centroid_pub;
ros::Publisher _marker_pub;
ros::Publisher _pub_clusters_message;
ros::Publisher _pub_text_pictogram;
ros::Publisher pose_arraybox_pub;//publish boundary of boundingbox
visualization_msgs::Marker _visualization_marker;
visualization_msgs::MarkerArray markers;//topic for ray array
ros::Publisher _pub_ray;//publish ray from LiDAR to boundingbox
visualization_msgs::MarkerArray text_markers_array;//topic for text markers array
ros::Publisher _pub_text_markers_array;//publish text_markers_array
geometry_msgs::Pose poseArrayElement_1;//element for PoseArray to save boundary of boundingbox
geometry_msgs::PoseArray pose_arraybox_1;//poseArray for saving boundary of boundingbox

ros::Publisher _pub_points_lanes_cloud;
ros::Publisher _pub_jsk_boundingboxes;
ros::Publisher _pub_jsk_hulls;

ros::ServiceClient _vectormap_server;

std_msgs::Header _velodyne_header;

pcl::PointCloud<pcl::PointXYZ> _sensor_cloud;

std::vector<double> _clustering_thresholds;
std::vector<double> _clustering_distances;

tf::StampedTransform* _transform;
tf::StampedTransform* _velodyne_output_transform;
tf::TransformListener* _transform_listener;

std::string _output_frame;
std::string _vectormap_frame;
static bool _velodyne_transform_available;
static bool _downsample_cloud;
static bool _pose_estimation;
static double _leaf_size;
static int _cluster_size_min;
static int _cluster_size_max;

static bool _remove_ground;	//only ground

static bool _using_sensor_cloud;
static bool _use_diffnormals;
static bool _use_vector_map;

static double _clip_min_height;
static double _clip_max_height;

static bool _keep_lanes;
static double _keep_lane_left_distance;
static double _keep_lane_right_distance;

static double _max_boundingbox_side;
static double _remove_points_upto;
static double _cluster_merge_threshold;
//排序函数  
void sort(float &a,float &b,float &c)     //对a，b，c 3个数排序。  
{  
    void exchange(float &a,float &b);     //函数声明形参是引用。  
  
    if(a>b)  
        exchange(a,b);   //a<=b  
  
    if(a>c)  
        exchange(a,c);   //a<=c  
  
    if(b>c)  
        exchange(b,c);   //b<=c  
      
}  
  
  
  
//互换函数  
void exchange(float &a,float &b)//形参是引用  
{  
    float temp;  
  
    temp=a;  
  
    a=b;  
  
    b=temp; 
} 
void transformBoundingBox(const jsk_recognition_msgs::BoundingBox& in_boundingbox, jsk_recognition_msgs::BoundingBox& out_boundingbox, const std::string& in_target_frame, const std_msgs::Header& in_header)
{
	geometry_msgs::PoseStamped pose_in, pose_out;
	pose_in.header = in_header;
	pose_in.pose = in_boundingbox.pose;
	try
	{
		_transform_listener->transformPose(in_target_frame, ros::Time(), pose_in, in_header.frame_id,  pose_out);
	}
	catch (tf::TransformException &ex)
	{
		ROS_ERROR("transformBoundingBox: %s",ex.what());
	}
	out_boundingbox.pose = pose_out.pose;
	out_boundingbox.header = in_header;
	out_boundingbox.header.frame_id = in_target_frame;
	out_boundingbox.dimensions = in_boundingbox.dimensions;
	out_boundingbox.value = in_boundingbox.value;
	out_boundingbox.label = in_boundingbox.label;
}

void publishCloudClusters(const ros::Publisher* in_publisher, const lidar_tracker::CloudClusterArray& in_clusters, const std::string& in_target_frame, const std_msgs::Header& in_header)
{
	if (in_target_frame!=in_header.frame_id)
	{
		lidar_tracker::CloudClusterArray clusters_transformed;
		clusters_transformed.header = in_header;
		clusters_transformed.header.frame_id = in_target_frame;
		for (auto i=in_clusters.clusters.begin(); i!= in_clusters.clusters.end(); i++)
		{
			lidar_tracker::CloudCluster cluster_transformed;
			cluster_transformed.header = in_header;
			try
			{
				_transform_listener->lookupTransform(in_target_frame, _velodyne_header.frame_id,
										ros::Time(), *_transform);
				pcl_ros::transformPointCloud(in_target_frame, *_transform, i->cloud, cluster_transformed.cloud);
				_transform_listener->transformPoint(in_target_frame, ros::Time(), i->min_point, in_header.frame_id, cluster_transformed.min_point);
				_transform_listener->transformPoint(in_target_frame, ros::Time(), i->max_point, in_header.frame_id, cluster_transformed.max_point);
				_transform_listener->transformPoint(in_target_frame, ros::Time(), i->avg_point, in_header.frame_id, cluster_transformed.avg_point);
				_transform_listener->transformPoint(in_target_frame, ros::Time(), i->centroid_point, in_header.frame_id, cluster_transformed.centroid_point);

				cluster_transformed.dimensions = i->dimensions;
				cluster_transformed.eigen_values = i->eigen_values;
				cluster_transformed.eigen_vectors = i->eigen_vectors;

				transformBoundingBox(i->bounding_box, cluster_transformed.bounding_box, in_target_frame, in_header);

				clusters_transformed.clusters.push_back(cluster_transformed);
			}
			catch (tf::TransformException &ex)
			{
				ROS_ERROR("publishCloudClusters: %s",ex.what());
			}
		}
		in_publisher->publish(clusters_transformed);
	}
	else
	{
		in_publisher->publish(in_clusters);
	}
}

void publishCentroids(const ros::Publisher* in_publisher, const lidar_tracker::centroids& in_centroids, const std::string& in_target_frame, const std_msgs::Header& in_header)
{
	if (in_target_frame!=in_header.frame_id)
	{
		lidar_tracker::centroids centroids_transformed;
		centroids_transformed.header = in_header;
		centroids_transformed.header.frame_id = in_target_frame;
		for (auto i=centroids_transformed.points.begin(); i!= centroids_transformed.points.end(); i++)
		{
			geometry_msgs::PointStamped centroid_in, centroid_out;
			centroid_in.header = in_header;
			centroid_in.point = *i;
			try
			{
				_transform_listener->transformPoint(in_target_frame, ros::Time(), centroid_in, in_header.frame_id, centroid_out);

				centroids_transformed.points.push_back(centroid_out.point);
			}
			catch (tf::TransformException &ex)
			{
				ROS_ERROR("publishCentroids: %s",ex.what());
			}
		}
		in_publisher->publish(centroids_transformed);
	}
	else
	{
		in_publisher->publish(in_centroids);
	}
}

void publishBoundingBoxArray(const ros::Publisher* in_publisher, const jsk_recognition_msgs::BoundingBoxArray& in_boundingbox_array, const std::string& in_target_frame, const std_msgs::Header& in_header)
{
	if (in_target_frame!=in_header.frame_id)
	{
		jsk_recognition_msgs::BoundingBoxArray boundingboxes_transformed;
		boundingboxes_transformed.header = in_header;
		boundingboxes_transformed.header.frame_id = in_target_frame;
		for (auto i=in_boundingbox_array.boxes.begin(); i!= in_boundingbox_array.boxes.end(); i++)
		{
			jsk_recognition_msgs::BoundingBox boundingbox_transformed;
			transformBoundingBox(*i, boundingbox_transformed, in_target_frame, in_header);
			boundingboxes_transformed.boxes.push_back(boundingbox_transformed);
			std::cout << "boundingbox_transformed.dimensions.x : " << boundingbox_transformed.dimensions.x << std::endl;
			std::cout << "boundingbox_transformed.dimensions.y : " << boundingbox_transformed.dimensions.y << std::endl;
			std::cout << "boundingbox_transformed.dimensions.z : " << boundingbox_transformed.dimensions.z << std::endl;
			// ROS_INFO("boundingbox_transformed.dimensions.x %f",(float)boundingbox_transformed.dimensions.x );
		}
		in_publisher->publish(boundingboxes_transformed);
	}
	else//usually get into this --else-- process
	{
		jsk_recognition_msgs::BoundingBox boundingboxes_temp;
		jsk_recognition_msgs::BoundingBoxArray boundingboxesArray_expanded;//expanded bounding box
		boundingboxesArray_expanded.header=in_header;
		boundingboxesArray_expanded.header.frame_id= in_target_frame; 
		for (auto i=in_boundingbox_array.boxes.begin(); i!= in_boundingbox_array.boxes.end(); i++)
		{
			// ROS_INFO("boundingbox_transformed.dimensions.x %f",(float)boundingbox_transformed.dimensions.x );
			boundingboxes_temp=*i;//get jsk_recognition_msgs::BoundingBox message
			//judge if it is an double decker
			float distance_to_doubledecker=0.0;
			distance_to_doubledecker=sqrt(boundingboxes_temp.pose.position.x*boundingboxes_temp.pose.position.x+
				boundingboxes_temp.pose.position.y*boundingboxes_temp.pose.position.y+
				boundingboxes_temp.pose.position.z*boundingboxes_temp.pose.position.z);
			/**************Rules  for doyuble-decker bus detection*****************/
			/**********************************************************************
			1.boundingboxes_temp.dimensions.x<11.45 && boundingboxes_temp.dimensions.x>9.45
			2.boundingboxes_temp.dimensions.y<3) && (boundingboxes_temp.dimensions.y>0.5
			3.boundingboxes_temp.dimensions.z<4) && (boundingboxes_temp.dimensions.y>1.5
			4.distance_to_doubledecker<10
			**********************************************************************/
			// if( ((boundingboxes_temp.dimensions.x<11.45) && (boundingboxes_temp.dimensions.x>5.45)
			// 	&&(boundingboxes_temp.dimensions.y<3) && (boundingboxes_temp.dimensions.y>0.5)
			// 	&&(boundingboxes_temp.dimensions.z<4.5) && (boundingboxes_temp.dimensions.z>1.5)
			// 	&&(distance_to_doubledecker<10)))
			// {
			// 	ROS_INFO("doubledecker detetced...");
			// 	ROS_INFO("boundingboxes_temp.pose.position.x=%f",boundingboxes_temp.pose.position.x);
			// 	ROS_INFO("boundingboxes_temp.pose.position.y=%f",boundingboxes_temp.pose.position.y);
			// 	ROS_INFO("boundingboxes_temp.pose.position.z=%f",boundingboxes_temp.pose.position.z);
			// 	ROS_INFO("boundingboxes_temp.dimensions.x=%f",boundingboxes_temp.dimensions.x);
			// 	ROS_INFO("boundingboxes_temp.dimensions.y=%f",boundingboxes_temp.dimensions.y);
			// 	ROS_INFO("boundingboxes_temp.dimensions.z=%f",boundingboxes_temp.dimensions.z);
			// 	ROS_INFO("distance_to_doubledecker=%f",distance_to_doubledecker);
			// 	boundingboxes_temp.dimensions.x=10.45;//debug:change to double-decker diemnsions 
			// 	boundingboxes_temp.dimensions.y=2.5;//debug:change to double-decker diemnsions
			// 	boundingboxes_temp.dimensions.z=4.4;//debug:change to double-decker dimension
			// 	boundingboxesArray_expanded.boxes.push_back(boundingboxes_temp);
			// }
		}
		in_publisher->publish(in_boundingbox_array);//initial bounding box
		// in_publisher->publish(boundingboxesArray_expanded);//expanded bounding box
		// boundingboxesArray_expanded.boxes.clear();
		// ROS_INFO("boundingboxesArray_expanded=%d",(int)boundingboxesArray_expanded.boxes.size());
	}
}

void publishCloud(const ros::Publisher* in_publisher, const pcl::PointCloud<pcl::PointXYZ>::Ptr in_cloud_to_publish_ptr)
{
	sensor_msgs::PointCloud2 cloud_msg;
	pcl::toROSMsg(*in_cloud_to_publish_ptr, cloud_msg);
	cloud_msg.header=_velodyne_header;
	in_publisher->publish(cloud_msg);
}

void publishColorCloud(const ros::Publisher* in_publisher, const pcl::PointCloud<pcl::PointXYZRGB>::Ptr in_cloud_to_publish_ptr)
{
	sensor_msgs::PointCloud2 cloud_msg;
	pcl::toROSMsg(*in_cloud_to_publish_ptr, cloud_msg);
	cloud_msg.header=_velodyne_header;
	in_publisher->publish(cloud_msg);
}

void keepLanePoints(const pcl::PointCloud<pcl::PointXYZ>::Ptr in_cloud_ptr,
					pcl::PointCloud<pcl::PointXYZ>::Ptr out_cloud_ptr,
					float in_left_lane_threshold = 1.5,
					float in_right_lane_threshold = 1.5)
{
	pcl::PointIndices::Ptr far_indices (new pcl::PointIndices);
	for(unsigned int i=0; i< in_cloud_ptr->points.size(); i++)
	{
		pcl::PointXYZ current_point;
		current_point.x=in_cloud_ptr->points[i].x;
		current_point.y=in_cloud_ptr->points[i].y;
		current_point.z=in_cloud_ptr->points[i].z;

		if (
				current_point.y > (in_left_lane_threshold) || current_point.y < -1.0*in_right_lane_threshold
			)
		{
			far_indices->indices.push_back(i);
		}
	}
	out_cloud_ptr->points.clear();
	pcl::ExtractIndices<pcl::PointXYZ> extract;
	extract.setInputCloud (in_cloud_ptr);
	extract.setIndices(far_indices);
	extract.setNegative(true);//true removes the indices, false leaves only the indices
	extract.filter(*out_cloud_ptr);
}

#ifdef GPU_CLUSTERING
std::vector<ClusterPtr> clusterAndColorGpu(const pcl::PointCloud<pcl::PointXYZ>::Ptr in_cloud_ptr,
											pcl::PointCloud<pcl::PointXYZRGB>::Ptr out_cloud_ptr,
											jsk_recognition_msgs::BoundingBoxArray& in_out_boundingbox_array,
											lidar_tracker::centroids& in_out_centroids,
											double in_max_cluster_distance=0.5)
{
	std::vector<ClusterPtr> clusters;

	//Convert input point cloud to vectors of x, y, and z

	int size = in_cloud_ptr->points.size();

	if (size == 0)
		return clusters;

	float *tmp_x, *tmp_y, *tmp_z;

	tmp_x = (float *)malloc(sizeof(float) * size);
	tmp_y = (float *)malloc(sizeof(float) * size);
	tmp_z = (float *)malloc(sizeof(float) * size);

	for (int i = 0; i < size; i++) {
		pcl::PointXYZ tmp_point = in_cloud_ptr->at(i);

		tmp_x[i] = tmp_point.x;
		tmp_y[i] = tmp_point.y;
		tmp_z[i] = tmp_point.z;
	}

	GpuEuclideanCluster gecl_cluster;

	gecl_cluster.setInputPoints(tmp_x, tmp_y, tmp_z, size);
	gecl_cluster.setThreshold(in_max_cluster_distance);
	gecl_cluster.setMinClusterPts (_cluster_size_min);
	gecl_cluster.setMaxClusterPts (_cluster_size_max);
	gecl_cluster.extractClusters();
	std::vector<GpuEuclideanCluster::GClusterIndex> cluster_indices = gecl_cluster.getOutput();

	unsigned int k = 0;

	for (auto it = cluster_indices.begin(); it != cluster_indices.end(); it++)
	{
		ClusterPtr cluster(new Cluster());
		cluster->SetCloud(in_cloud_ptr, it->points_in_cluster, _velodyne_header, k, (int)_colors[k].val[0], (int)_colors[k].val[1], (int)_colors[k].val[2], "", _pose_estimation);
		clusters.push_back(cluster);

		k++;
	}

	free(tmp_x);
	free(tmp_y);
	free(tmp_z);

	return clusters;
}
#endif

std::vector<ClusterPtr> clusterAndColor(const pcl::PointCloud<pcl::PointXYZ>::Ptr in_cloud_ptr,
		pcl::PointCloud<pcl::PointXYZRGB>::Ptr out_cloud_ptr,
		jsk_recognition_msgs::BoundingBoxArray& in_out_boundingbox_array,
		lidar_tracker::centroids& in_out_centroids,
		double in_max_cluster_distance=0.5)
{
	pcl::search::KdTree<pcl::PointXYZ>::Ptr tree (new pcl::search::KdTree<pcl::PointXYZ>);

	//create 2d pc
	pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_2d(new pcl::PointCloud<pcl::PointXYZ>);
	pcl::copyPointCloud(*in_cloud_ptr, *cloud_2d);
	//make it flat
	for (size_t i=0; i<cloud_2d->points.size(); i++)
	{
		cloud_2d->points[i].z = 0;
	}

	if (cloud_2d->points.size() > 0)
		tree->setInputCloud (cloud_2d);

	std::vector<pcl::PointIndices> cluster_indices;

	//perform clustering on 2d cloud
	pcl::EuclideanClusterExtraction<pcl::PointXYZ> ec;
	ec.setClusterTolerance (in_max_cluster_distance); //
	ec.setMinClusterSize (_cluster_size_min);
	ec.setMaxClusterSize (_cluster_size_max);
	ec.setSearchMethod(tree);
	ec.setInputCloud (cloud_2d);
	ec.extract (cluster_indices);
	//use indices on 3d cloud

	/*pcl::ConditionalEuclideanClustering<pcl::PointXYZ> cec (true);
	cec.setInputCloud (in_cloud_ptr);
	cec.setConditionFunction (&independentDistance);
	cec.setMinClusterSize (cluster_size_min);
	cec.setMaxClusterSize (cluster_size_max);
	cec.setClusterTolerance (_distance*2.0f);
	cec.segment (cluster_indices);*/

	/////////////////////////////////
	//---	3. Color clustered points
	/////////////////////////////////
	unsigned int k = 0;
	//pcl::PointCloud<pcl::PointXYZRGB>::Ptr final_cluster (new pcl::PointCloud<pcl::PointXYZRGB>);

	std::vector<ClusterPtr> clusters;
	//pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_cluster (new pcl::PointCloud<pcl::PointXYZRGB>);//coord + color cluster
	for (auto it = cluster_indices.begin(); it != cluster_indices.end(); ++it)
	{
		ClusterPtr cluster(new Cluster());
		cluster->SetCloud(in_cloud_ptr, it->indices, _velodyne_header, k, (int)_colors[k].val[0], (int)_colors[k].val[1], (int)_colors[k].val[2], "", _pose_estimation);
		clusters.push_back(cluster);

		k++;
	}
	//std::cout << "Clusters: " << k << std::endl;
	return clusters;

}

void checkClusterMerge(size_t in_cluster_id, std::vector<ClusterPtr>& in_clusters, std::vector<bool>& in_out_visited_clusters, std::vector<size_t>& out_merge_indices, double in_merge_threshold)
{
	//std::cout << "checkClusterMerge" << std::endl;
	pcl::PointXYZ point_a = in_clusters[in_cluster_id]->GetCentroid();
	for(size_t i=0; i< in_clusters.size(); i++)
	{
		if (i != in_cluster_id && !in_out_visited_clusters[i])
		{
			pcl::PointXYZ point_b = in_clusters[i]->GetCentroid();
			double distance = sqrt( pow(point_b.x - point_a.x,2) + pow(point_b.y - point_a.y,2) );
			if (distance <= in_merge_threshold)
			{
				in_out_visited_clusters[i] = true;
				out_merge_indices.push_back(i);
				//std::cout << "Merging " << in_cluster_id << " with " << i << " dist:" << distance << std::endl;
				checkClusterMerge(i, in_clusters, in_out_visited_clusters, out_merge_indices, in_merge_threshold);
			}
		}
	}
}

void mergeClusters(const std::vector<ClusterPtr>& in_clusters, std::vector<ClusterPtr>& out_clusters, std::vector<size_t> in_merge_indices, const size_t& current_index, std::vector<bool>& in_out_merged_clusters)
{
	//std::cout << "mergeClusters:" << in_merge_indices.size() << std::endl;
	pcl::PointCloud<pcl::PointXYZRGB> sum_cloud;
	pcl::PointCloud<pcl::PointXYZ> mono_cloud;
	ClusterPtr merged_cluster(new Cluster());
	for (size_t i=0; i<in_merge_indices.size(); i++)
	{
		sum_cloud += *(in_clusters[in_merge_indices[i]]->GetCloud());
		in_out_merged_clusters[in_merge_indices[i]] = true;
	}
	std::vector<int> indices(sum_cloud.points.size(), 0);
	for (size_t i=0; i<sum_cloud.points.size(); i++)
	{
		indices[i]=i;
	}

	if (sum_cloud.points.size() > 0)
	{
		pcl::copyPointCloud(sum_cloud, mono_cloud);
		//std::cout << "mergedClusters " << sum_cloud.points.size() << " mono:" << mono_cloud.points.size() << std::endl;
		//cluster->SetCloud(in_cloud_ptr, it->indices, _velodyne_header, k, (int)_colors[k].val[0], (int)_colors[k].val[1], (int)_colors[k].val[2], "", _pose_estimation);
		merged_cluster->SetCloud(mono_cloud.makeShared(), indices, _velodyne_header, current_index,(int)_colors[current_index].val[0], (int)_colors[current_index].val[1], (int)_colors[current_index].val[2], "", _pose_estimation);
		out_clusters.push_back(merged_cluster);
	}
}

void checkAllForMerge(std::vector<ClusterPtr>& in_clusters, std::vector<ClusterPtr>& out_clusters, float in_merge_threshold)
{
	//std::cout << "checkAllForMerge" << std::endl;
	std::vector<bool> visited_clusters(in_clusters.size(), false);
	std::vector<bool> merged_clusters(in_clusters.size(), false);
	size_t current_index=0;
	for (size_t i = 0; i< in_clusters.size(); i++)
	{
		if (!visited_clusters[i])
		{
			visited_clusters[i] = true;
			std::vector<size_t> merge_indices;
			checkClusterMerge(i, in_clusters, visited_clusters, merge_indices, in_merge_threshold);
			mergeClusters(in_clusters, out_clusters, merge_indices, current_index++, merged_clusters);
		}
	}
	for(size_t i =0; i< in_clusters.size(); i++)
	{
		//check for clusters not merged, add them to the output
		if (!merged_clusters[i])
		{
			out_clusters.push_back(in_clusters[i]);
		}
	}

	//ClusterPtr cluster(new Cluster());
}

void segmentByDistance(const pcl::PointCloud<pcl::PointXYZ>::Ptr in_cloud_ptr,
		pcl::PointCloud<pcl::PointXYZRGB>::Ptr out_cloud_ptr,
		jsk_recognition_msgs::BoundingBoxArray& in_out_boundingbox_array,
		lidar_tracker::centroids& in_out_centroids,
		lidar_tracker::CloudClusterArray& in_out_clusters,
		jsk_recognition_msgs::PolygonArray& in_out_polygon_array,
		jsk_rviz_plugins::PictogramArray& in_out_pictogram_array)
{
	//cluster the pointcloud according to the distance of the points using different thresholds (not only one for the entire pc)
	//in this way, the points farther in the pc will also be clustered

	//0 => 0-15m d=0.5
	//1 => 15-30 d=1
	//2 => 30-45 d=1.6
	//3 => 45-60 d=2.1
	//4 => >60   d=2.6

	std::vector<pcl::PointCloud<pcl::PointXYZ>::Ptr> cloud_segments_array(5);

	for(unsigned int i=0; i<cloud_segments_array.size(); i++)
	{
		pcl::PointCloud<pcl::PointXYZ>::Ptr tmp_cloud(new pcl::PointCloud<pcl::PointXYZ>);
		cloud_segments_array[i] = tmp_cloud;
	}

	for (unsigned int i=0; i<in_cloud_ptr->points.size(); i++)
	{
		pcl::PointXYZ current_point;
		current_point.x = in_cloud_ptr->points[i].x;
		current_point.y = in_cloud_ptr->points[i].y;
		current_point.z = in_cloud_ptr->points[i].z;

		float origin_distance = sqrt( pow(current_point.x,2) + pow(current_point.y,2) );

		if 		(origin_distance < _clustering_distances[0] )	{cloud_segments_array[0]->points.push_back (current_point);}
		else if(origin_distance < _clustering_distances[1])		{cloud_segments_array[1]->points.push_back (current_point);}
		else if(origin_distance < _clustering_distances[2])		{cloud_segments_array[2]->points.push_back (current_point);}
		else if(origin_distance < _clustering_distances[3])		{cloud_segments_array[3]->points.push_back (current_point);}
		else													{cloud_segments_array[4]->points.push_back (current_point);}
	}

	std::vector <ClusterPtr> all_clusters;
	for(unsigned int i=0; i<cloud_segments_array.size(); i++)
	{
#ifdef GPU_CLUSTERING
		std::vector<ClusterPtr> local_clusters = clusterAndColorGpu(cloud_segments_array[i], out_cloud_ptr, in_out_boundingbox_array, in_out_centroids, _clustering_thresholds[i]);
#else
		std::vector<ClusterPtr> local_clusters = clusterAndColor(cloud_segments_array[i], out_cloud_ptr, in_out_boundingbox_array, in_out_centroids, _clustering_thresholds[i]);
#endif
		all_clusters.insert(all_clusters.end(), local_clusters.begin(), local_clusters.end());
	}

	//Clusters can be merged or checked in here
	//....
	//check for mergable clusters
	std::vector<ClusterPtr> mid_clusters;
	std::vector<ClusterPtr> final_clusters;

	if (all_clusters.size() > 0)
		checkAllForMerge(all_clusters, mid_clusters, _cluster_merge_threshold);
	else
		mid_clusters = all_clusters;

	if (mid_clusters.size() > 0)
			checkAllForMerge(mid_clusters, final_clusters, _cluster_merge_threshold);
	else
		final_clusters = mid_clusters;

	tf::StampedTransform vectormap_transform;
	if (_use_vector_map)
	{
		cv::TickMeter timer;

		try
		{
			//if the frame of the vectormap is different than the input, obtain transform
			if (_vectormap_frame != _velodyne_header.frame_id)
			{
				_transform_listener->lookupTransform(_vectormap_frame, _velodyne_header.frame_id, ros::Time(), vectormap_transform);
			}

			timer.reset();timer.start();

			//check if centroids are inside the drivable area
			for(unsigned int i=0; i<final_clusters.size(); i++)
			{
				//transform centroid points to vectormap frame
				pcl::PointXYZ pcl_centroid = final_clusters[i]->GetCentroid();
				tf::Vector3 vector_centroid (pcl_centroid.x, pcl_centroid.y, pcl_centroid.z);
				tf::Vector3 transformed_centroid;

				if (_vectormap_frame != _velodyne_header.frame_id)
					transformed_centroid = vectormap_transform*vector_centroid;
				else
					transformed_centroid = vector_centroid;

				vector_map_server::PositionState position_state;
				position_state.request.position.x = transformed_centroid.getX();
				position_state.request.position.y = transformed_centroid.getY();
				position_state.request.position.z = transformed_centroid.getZ();


				if (_vectormap_server.call(position_state))
				{
					final_clusters[i]->SetValidity(position_state.response.state);
					/*std::cout << "Original:" << pcl_centroid.x << "," << pcl_centroid.y << "," << pcl_centroid.z <<
							" Transformed:" << transformed_centroid.x() << "," << transformed_centroid.y() << "," << transformed_centroid.z() <<
							" Validity:" << position_state.response.state << std::endl;*/
				}
				else
				{
					ROS_INFO("vectormap_filtering: VectorMap Server Call failed. Make sure vectormap_server is running. No filtering performed.");
					final_clusters[i]->SetValidity(true);
				}
			}
			timer.stop();
			//std::cout << "vm server took " << timer.getTimeMilli() << " ms to check " << final_clusters.size() << std::endl;
		}
		catch(tf::TransformException &ex)
		{
			ROS_INFO("vectormap_filtering: %s", ex.what());
		}
	}
	//Get final PointCloud to be published
	in_out_polygon_array.header = _velodyne_header;
	in_out_pictogram_array.header = _velodyne_header;
	/**************final cluster here*****************/
	/**********************************************************************
	1.clustered point cloud
	2.boundingbox array
	**********************************************************************/	
	for(unsigned int i=0; i<final_clusters.size(); i++)
	{
		*out_cloud_ptr = *out_cloud_ptr + *(final_clusters[i]->GetCloud());

		jsk_recognition_msgs::BoundingBox bounding_box = final_clusters[i]->GetBoundingBox();
		geometry_msgs::PolygonStamped polygon = final_clusters[i]->GetPolygon();
		jsk_rviz_plugins::Pictogram pictogram_cluster;
		pictogram_cluster.header = _velodyne_header;

		//PICTO
		pictogram_cluster.mode = pictogram_cluster.STRING_MODE;
		pictogram_cluster.pose.position.x = final_clusters[i]->GetMaxPoint().x;
		pictogram_cluster.pose.position.y = final_clusters[i]->GetMaxPoint().y;
		pictogram_cluster.pose.position.z = final_clusters[i]->GetMaxPoint().z;
		tf::Quaternion quat(0.0, -0.7, 0.0, 0.7);
		tf::quaternionTFToMsg(quat, pictogram_cluster.pose.orientation);
		pictogram_cluster.size = 4;
		std_msgs::ColorRGBA color;
		color.a = 1; color.r = 1; color.g = 1; color.b = 1;
		pictogram_cluster.color = color;
		pictogram_cluster.character = std::to_string( i );
		//PICTO

		//pcl::PointXYZ min_point = final_clusters[i]->GetMinPoint();
		//pcl::PointXYZ max_point = final_clusters[i]->GetMaxPoint();
		pcl::PointXYZ center_point = final_clusters[i]->GetCentroid();
		geometry_msgs::Point centroid;
		centroid.x = center_point.x; centroid.y = center_point.y; centroid.z = center_point.z;
		bounding_box.header = _velodyne_header;
		polygon.header = _velodyne_header;

		if (	final_clusters[i]->IsValid()
				//&& bounding_box.dimensions.x >0 && bounding_box.dimensions.y >0 && bounding_box.dimensions.z > 0
				//&&	bounding_box.dimensions.x < _max_boundingbox_side && bounding_box.dimensions.y < _max_boundingbox_side
				)
		{
			double building_height = 40;
			in_out_centroids.points.push_back(centroid);
			_visualization_marker.points.push_back(centroid);

			in_out_polygon_array.polygons.push_back(polygon);
			in_out_pictogram_array.pictograms.push_back(pictogram_cluster);

			lidar_tracker::CloudCluster cloud_cluster;
			final_clusters[i]->ToRosMessage(_velodyne_header, cloud_cluster);
			in_out_clusters.clusters.push_back(cloud_cluster);
			//get size of one cluster
			sensor_msgs::PointCloud2 clustered_pointcloud2_temp;
			pcl::PointCloud<pcl::PointXYZ> pcl_pc;
			clustered_pointcloud2_temp=cloud_cluster.cloud;
			pcl::fromROSMsg(clustered_pointcloud2_temp, pcl_pc);
			// ROS_INFO("how many cluster in total=%d",(int)final_clusters.size());
			// ROS_INFO("points saved in one cluster= %d",(int)pcl_pc.size());
			// ROS_INFO("cloud_cluster.bounding_box.dimension.x= %f",cloud_cluster.bounding_box.dimensions.x);
			
			/**************Rules  for doyuble-decker bus detection*****************/
			/**********************************************************************
			1.boundingboxes_temp.dimensions.x<11.45 && boundingboxes_temp.dimensions.x>9.45
			2.boundingboxes_temp.dimensions.y<3) && (boundingboxes_temp.dimensions.y>0.5
			3.boundingboxes_temp.dimensions.z<4) && (boundingboxes_temp.dimensions.y>1.5
			4.distance_to_doubledecker<10
			**********************************************************************/
			//visualize the ray from LiDAR to boundingbox 

			float dimension_x=0.0,dimension_y=0.0,dimension_z=0.0;//dimension
			float position_x=0.0,position_y=0.0,position_z=0.0;//position of clustered points
			float distance_to_doubledecker_cluster=0.0;
			dimension_x=cloud_cluster.bounding_box.dimensions.x;
			dimension_y=cloud_cluster.bounding_box.dimensions.y;
			dimension_z=cloud_cluster.bounding_box.dimensions.z;
			float box_chang=0.0,box_kuan=0.0,box_gao=0.0;
			sort(dimension_x,dimension_y,dimension_z);//make it in sequence
			position_x=cloud_cluster.bounding_box.pose.position.x;
			position_y=cloud_cluster.bounding_box.pose.position.y;
			position_z=cloud_cluster.bounding_box.pose.position.z;
			distance_to_doubledecker_cluster=sqrt(position_x*position_x+position_y*position_y
				+position_z*position_z);
			// if( (dimension_z<13)&&(dimension_z>11.0) &&(distance_to_doubledecker_cluster<9.0)&&(dimension_y<3.5)&&(dimension_y>2.5)) // stable verison
			// if( (dimension_z<13)&&(dimension_z>9.0) &&(distance_to_doubledecker_cluster<9.0)&&(dimension_y<3.5)&&(dimension_y>2.5))
			if( (dimension_z<130)&&(dimension_z>5.0) &&(distance_to_doubledecker_cluster<20.0)&&(dimension_y<23.5)&&(dimension_y>2.5))
			{
				{
					bounding_box.dimensions.z=building_height * 2; // this is height of buildings
					// bounding_box.pose.position.z=4.4; // this is height of double-decker bus

					// if(position_y > 0)
					// {
					// 	in_out_boundingbox_array.boxes.push_back(bounding_box);
					// }

					// ROS_INFO("bounding_box.pose.orientation.x= %f",bounding_box.pose.orientation.x);
					// ROS_INFO("bounding_box.pose.orientation.y= %f",bounding_box.pose.orientation.y);
					// ROS_INFO("bounding_box.pose.orientation.z= %f",bounding_box.pose.orientation.z);
					// ROS_INFO("bounding_box.pose.orientation.w======== %f",bounding_box.pose.orientation.w);
					tf::Quaternion q(bounding_box.pose.orientation.x, bounding_box.pose.orientation.y
						, bounding_box.pose.orientation.z, bounding_box.pose.orientation.w);
					tf::Matrix3x3 m(q);
					double roll, pitch, yaw;
					m.getRPY(roll, pitch, yaw);
					yaw = yaw*(180/3.14159); // radis to degree
					if(yaw < 10.0 && yaw > -10.0) // only consider the building on both sides
					{
						in_out_boundingbox_array.boxes.push_back(bounding_box); // push back the bounding box 
						ROS_INFO("Roll: [%f],Pitch: [%f],Yaw: [%f]",roll,pitch,yaw); // output the orientation angle
					}
					// ROS_INFO("Roll: [%f],Pitch: [%f],Yaw: [%f]",roll,pitch,yaw*(180/3.14159));
					float sx1=0.0,sy1=0.0,sx2=0.0,sy2=0.0;//boundary of boundbox
					sx1=-dimension_z/2.0;
					sy1=-dimension_x/2.0;
					sx2= dimension_z/2.0;
					sy2=-dimension_x/2.0;
					//rotation transfer
					sx1=sx1*cos(yaw)-sy1*sin(yaw);
					sy1=sx1*sin(yaw)-sy1*cos(yaw);
					sx2=sx2*cos(yaw)-sy2*sin(yaw);
					sy2=sx2*sin(yaw)-sy2*cos(yaw);
					//translate transfer
					sx1=sx1+cloud_cluster.bounding_box.pose.position.x;
					sy1=sy1+cloud_cluster.bounding_box.pose.position.y;
					sx2=sx2+cloud_cluster.bounding_box.pose.position.x;
					sy2=sy2+cloud_cluster.bounding_box.pose.position.y;
					// ROS_INFO("cloud_cluster.bounding_box.dimension.x= %f",cloud_cluster.bounding_box.dimensions.x);
					// ROS_INFO("distance_to_doubledecker_cluster-----= %f",distance_to_doubledecker_cluster);
					//visualize ray
					ROS_INFO("dimension_x=%f",dimension_x);
					ROS_INFO("dimension_y=%f",dimension_y);
					ROS_INFO("dimension_z=%f",dimension_z);
					ROS_INFO("position z=%f",cloud_cluster.bounding_box.pose.position.z);
					visualization_msgs::Marker marker,marker_2,marker_3;
			        marker.header.frame_id = "velodyne";
			        marker.header.stamp = cloud_cluster.header.stamp;
			        marker.ns = "Rays1"; // center of the bounding box
			        marker.id = markers.markers.size();
			        marker.type = visualization_msgs::Marker::LINE_STRIP;
			        marker.action = 0;
			        marker.pose.position.x = 0.0;
			        marker.pose.position.y = 0.0;
			        marker.pose.position.z = 0.0;
			        marker.pose.orientation.x = 0.0;
			        marker.pose.orientation.y = 0.0;
			        marker.pose.orientation.z = 0.0;
			        marker.pose.orientation.w = 1.0;
			        //0.025 determine with of LOS
			        marker.scale.x = marker.scale.y = marker.scale.z = 0.095;
			        marker.lifetime = ros::Duration(0.2);
			        marker.frame_locked = true;
			        marker.points.resize(2);
			        marker.points[0].x = 0;
			        marker.points[0].y = 0;
			        marker.points[0].z = 0;
			        marker.points[1].x = cloud_cluster.bounding_box.pose.position.x;
			        marker.points[1].y = cloud_cluster.bounding_box.pose.position.y;
			        marker.points[1].z = cloud_cluster.bounding_box.pose.position.z;
			        marker.colors.resize(2);
			        marker.colors[0].a = 0.9;//0.5
			        marker.colors[0].r = 0.0;
			        marker.colors[0].g = 1.0;
			        marker.colors[0].b = 0.0;
			        marker.colors[1].a = 0.9;//0.2
			        marker.colors[1].r = 0.0;
			        marker.colors[1].g = 1.0;
			        marker.colors[1].b = 0.0;
			        markers.markers.push_back(marker);
			        double elevation_max_point=atan2(building_height,sqrt(cloud_cluster.max_point.point.x *cloud_cluster.max_point.point.x
			         + cloud_cluster.max_point.point.y *cloud_cluster.max_point.point.y));//elevation_max_point

			        double angle_max_point= atan2(cloud_cluster.max_point.point.y, cloud_cluster.max_point.point.x);//azimuth
			        double elevation_min_point=atan2(building_height,sqrt(cloud_cluster.min_point.point.x *cloud_cluster.min_point.point.x
			         + cloud_cluster.min_point.point.y *cloud_cluster.min_point.point.y));//elevation_min_point

			        double angle_min_point= atan2(cloud_cluster.min_point.point.y, cloud_cluster.min_point.point.x);//azimuth
			        if(elevation_max_point<0 || elevation_min_point<0)
			        {
			        	ROS_INFO("angle_max_point=%f",angle_max_point*180/3.14159);
				        ROS_INFO("elevation_max_point=%f",elevation_max_point*180/3.14159);
				        ROS_INFO("angle_min_point=%f",angle_min_point*180/3.14159);
				        ROS_INFO("elevation_min_point=%f",elevation_min_point*180/3.14159);
			        }
			        marker_2=marker;
			        marker_2.ns = "Rays_2"; // one end of the builidng boundary 
			        marker_2.points[1].x = cloud_cluster.max_point.point.x;
			        marker_2.points[1].y = cloud_cluster.max_point.point.y;
			        marker_2.points[1].z = 4.4;
			        // markers.markers.push_back(marker_2);
			        marker_3=marker;
			        marker_3.ns = "Rays_3"; // the other end of the building boundary
			        marker_3.points[1].x = cloud_cluster.min_point.point.x;
			        marker_3.points[1].y = cloud_cluster.min_point.point.y;
			        marker_3.points[1].z = 4.4;
			        // markers.markers.push_back(marker_3);

			        poseArrayElement_1.orientation.x=angle_max_point     *180/3.14159;//azimuth of max point
			        poseArrayElement_1.orientation.y= elevation_max_point*180/3.14159;//elevation of max point
			        poseArrayElement_1.orientation.z=angle_min_point     *180/3.14159;//azimuth of min point
			        poseArrayElement_1.orientation.w= elevation_min_point*180/3.14159;//elevation of min point
			        pose_arraybox_1.poses.push_back(poseArrayElement_1);

			        //visualize text :text_markers_array
			        visualization_msgs::Marker text_marker;
					text_marker.header.frame_id = "velodyne";
					text_marker.header.stamp = ros::Time::now();
					text_marker.ns = "basic_shapes";
					text_marker.id = text_markers_array.markers.size();
					text_marker.type = visualization_msgs::Marker::TEXT_VIEW_FACING;
					text_marker.action = 0;

					// text_marker.pose.position.x = cloud_cluster.bounding_box.pose.position.x ;
					// text_marker.pose.position.y = cloud_cluster.bounding_box.pose.position.x;
					// text_marker.pose.position.z = cloud_cluster.bounding_box.pose.position.x;
					// text_marker.pose.orientation.x = cloud_cluster.bounding_box.pose.orientation.x;
					// text_marker.pose.orientation.y = cloud_cluster.bounding_box.pose.orientation.y;
					// text_marker.pose.orientation.z = cloud_cluster.bounding_box.pose.orientation.z;
					// text_marker.pose.orientation.w = cloud_cluster.bounding_box.pose.orientation.w;
					// text_marker.text = "boundingbox";
					// text_marker.scale.x = 0.3;
					// text_marker.scale.y = 0.3;
					// text_marker.scale.z = 0.1;
					// text_marker.color.r = 0.0f;
					// text_marker.color.g = 1.0f;
					// text_marker.color.b = 0.0f;
					// text_marker.color.a = 1.0;
					text_marker.pose.position.x = 0.0;
			        text_marker.pose.position.y = 0.0;
			        text_marker.pose.position.z = 0.0;
			        text_marker.pose.orientation.x = 0.0;
			        text_marker.pose.orientation.y = 0.0;
			        text_marker.pose.orientation.z = 0.0;
			        text_marker.pose.orientation.w = 1.0;
			        //0.025 determine with of LOS
			        text_marker.scale.x = marker.scale.y = marker.scale.z = 0.095;
			        text_marker.lifetime = ros::Duration(0.2);
			        text_marker.frame_locked = true;
			        text_marker.points.resize(2);
			        text_marker.points[0].x = 0;
			        text_marker.points[0].y = 0;
			        text_marker.points[0].z = 0;
			        text_marker.points[1].x = cloud_cluster.bounding_box.pose.position.x;
			        text_marker.points[1].y = cloud_cluster.bounding_box.pose.position.y;
			        text_marker.points[1].z = cloud_cluster.bounding_box.pose.position.z;
			        text_marker.colors.resize(2);
			        text_marker.colors[0].a = 0.9;//0.5
			        text_marker.colors[0].r = 0.0;
			        text_marker.colors[0].g = 1.0;
			        text_marker.colors[0].b = 0.0;
			        text_marker.colors[1].a = 0.9;//0.2
			        text_marker.colors[1].r = 0.0;
			        text_marker.colors[1].g = 1.0;
			        text_marker.colors[1].b = 0.0;
					text_markers_array.markers.push_back(text_marker);
				}
			}
		}
	}
		    _pub_ray.publish(markers);//publish ray topic
		    markers.markers.clear();//clean memory
		    _pub_text_markers_array.publish(text_markers_array);//publish text ray
		    text_markers_array.markers.clear();//clear memory
		    pose_arraybox_pub.publish(pose_arraybox_1);//publish boundingbox parameters
		    pose_arraybox_1.poses.clear();//clear memory

	for(size_t i=0; i< in_out_polygon_array.polygons.size();i++)
	{
		in_out_polygon_array.labels.push_back(i);
	}

}

void removeFloor(const pcl::PointCloud<pcl::PointXYZ>::Ptr in_cloud_ptr, pcl::PointCloud<pcl::PointXYZ>::Ptr out_nofloor_cloud_ptr, pcl::PointCloud<pcl::PointXYZ>::Ptr out_onlyfloor_cloud_ptr, float in_max_height=0.2, float in_floor_max_angle=0.1)
{
	/*pcl::PointIndicesPtr ground (new pcl::PointIndices);
	// Create the filtering object
	pcl::ProgressiveMorphologicalFilter<pcl::PointXYZ> pmf;
	pmf.setInputCloud (in_cloud_ptr);
	pmf.setMaxWindowSize (20);
	pmf.setSlope (1.0f);
	pmf.setInitialDistance (0.5f);
	pmf.setMaxDistance (3.0f);
	pmf.extract (ground->indices);

	// Create the filtering object
	pcl::ExtractIndices<pcl::PointXYZ> extract;
	extract.setInputCloud (in_cloud_ptr);
	extract.setIndices (ground);
	extract.setNegative(true);//true removes the indices, false leaves only the indices
	extract.filter(*out_nofloor_cloud_ptr);

	//EXTRACT THE FLOOR FROM THE CLOUD
	extract.setNegative(false);//true removes the indices, false leaves only the indices
	extract.filter(*out_onlyfloor_cloud_ptr);*/

	pcl::SACSegmentation<pcl::PointXYZ> seg;
	pcl::PointIndices::Ptr inliers (new pcl::PointIndices);
	pcl::ModelCoefficients::Ptr coefficients (new pcl::ModelCoefficients);

	seg.setOptimizeCoefficients (true);
	seg.setModelType(pcl::SACMODEL_PERPENDICULAR_PLANE);
	seg.setMethodType(pcl::SAC_RANSAC);
	seg.setMaxIterations(100);
	seg.setAxis(Eigen::Vector3f(0,0,1));
	seg.setEpsAngle(in_floor_max_angle);

	seg.setDistanceThreshold (in_max_height);//floor distance
	seg.setOptimizeCoefficients(true);
	seg.setInputCloud(in_cloud_ptr);
	seg.segment(*inliers, *coefficients);
	if (inliers->indices.size () == 0)
	{
		std::cout << "Could not estimate a planar model for the given dataset." << std::endl;
	}

	//REMOVE THE FLOOR FROM THE CLOUD
	pcl::ExtractIndices<pcl::PointXYZ> extract;
	extract.setInputCloud (in_cloud_ptr);
	extract.setIndices(inliers);
	extract.setNegative(true);//true removes the indices, false leaves only the indices
	extract.filter(*out_nofloor_cloud_ptr);

	//EXTRACT THE FLOOR FROM THE CLOUD
	extract.setNegative(false);//true removes the indices, false leaves only the indices
	extract.filter(*out_onlyfloor_cloud_ptr);
}

void downsampleCloud(const pcl::PointCloud<pcl::PointXYZ>::Ptr in_cloud_ptr, pcl::PointCloud<pcl::PointXYZ>::Ptr out_cloud_ptr, float in_leaf_size=0.2)
{
	pcl::VoxelGrid<pcl::PointXYZ> sor;
	sor.setInputCloud(in_cloud_ptr);
	sor.setLeafSize((float)in_leaf_size, (float)in_leaf_size, (float)in_leaf_size);
	sor.filter(*out_cloud_ptr);
}

void clipCloud(const pcl::PointCloud<pcl::PointXYZ>::Ptr in_cloud_ptr, pcl::PointCloud<pcl::PointXYZ>::Ptr out_cloud_ptr, float in_min_height=-1.3, float in_max_height=0.5)
{
	out_cloud_ptr->points.clear();
	for (unsigned int i=0; i<in_cloud_ptr->points.size(); i++)
	{
		if (in_cloud_ptr->points[i].z >= in_min_height &&
				in_cloud_ptr->points[i].z <= in_max_height)
		{
			out_cloud_ptr->points.push_back(in_cloud_ptr->points[i]);
		}
	}
}

void differenceNormalsSegmentation(const pcl::PointCloud<pcl::PointXYZ>::Ptr in_cloud_ptr, pcl::PointCloud<pcl::PointXYZ>::Ptr out_cloud_ptr)
{
	float small_scale=0.5;
	float large_scale=2.0;
	float angle_threshold=0.5;
	pcl::search::Search<pcl::PointXYZ>::Ptr tree;
	if (in_cloud_ptr->isOrganized ())
	{
		tree.reset (new pcl::search::OrganizedNeighbor<pcl::PointXYZ> ());
	}
	else
	{
		tree.reset (new pcl::search::KdTree<pcl::PointXYZ> (false));
	}

	// Set the input pointcloud for the search tree
	tree->setInputCloud (in_cloud_ptr);

	pcl::NormalEstimationOMP<pcl::PointXYZ, pcl::PointNormal> normal_estimation;
	//pcl::gpu::NormalEstimation<pcl::PointXYZ, pcl::PointNormal> normal_estimation;
	normal_estimation.setInputCloud (in_cloud_ptr);
	normal_estimation.setSearchMethod (tree);

	normal_estimation.setViewPoint (std::numeric_limits<float>::max (), std::numeric_limits<float>::max (), std::numeric_limits<float>::max ());

	pcl::PointCloud<pcl::PointNormal>::Ptr normals_small_scale (new pcl::PointCloud<pcl::PointNormal>);
	pcl::PointCloud<pcl::PointNormal>::Ptr normals_large_scale (new pcl::PointCloud<pcl::PointNormal>);

	normal_estimation.setRadiusSearch (small_scale);
	normal_estimation.compute (*normals_small_scale);

	normal_estimation.setRadiusSearch (large_scale);
	normal_estimation.compute (*normals_large_scale);

	pcl::PointCloud<pcl::PointNormal>::Ptr diffnormals_cloud (new pcl::PointCloud<pcl::PointNormal>);
	pcl::copyPointCloud<pcl::PointXYZ, pcl::PointNormal>(*in_cloud_ptr, *diffnormals_cloud);

	// Create DoN operator
	pcl::DifferenceOfNormalsEstimation<pcl::PointXYZ, pcl::PointNormal, pcl::PointNormal> diffnormals_estimator;
	diffnormals_estimator.setInputCloud (in_cloud_ptr);
	diffnormals_estimator.setNormalScaleLarge (normals_large_scale);
	diffnormals_estimator.setNormalScaleSmall (normals_small_scale);

	diffnormals_estimator.initCompute();

	diffnormals_estimator.computeFeature(*diffnormals_cloud);

	pcl::ConditionOr<pcl::PointNormal>::Ptr range_cond (new pcl::ConditionOr<pcl::PointNormal>() );
	range_cond->addComparison (pcl::FieldComparison<pcl::PointNormal>::ConstPtr (
			new pcl::FieldComparison<pcl::PointNormal> ("curvature", pcl::ComparisonOps::GT, angle_threshold) )
			);
	// Build the filter
	pcl::ConditionalRemoval<pcl::PointNormal> cond_removal;
	cond_removal.setCondition(range_cond);
	cond_removal.setInputCloud (diffnormals_cloud);

	pcl::PointCloud<pcl::PointNormal>::Ptr diffnormals_cloud_filtered (new pcl::PointCloud<pcl::PointNormal>);

	// Apply filter
	cond_removal.filter (*diffnormals_cloud_filtered);

	pcl::copyPointCloud<pcl::PointNormal, pcl::PointXYZ>(*diffnormals_cloud, *out_cloud_ptr);
}

void removePointsUpTo(const pcl::PointCloud<pcl::PointXYZ>::Ptr in_cloud_ptr, pcl::PointCloud<pcl::PointXYZ>::Ptr out_cloud_ptr, const double in_distance)
{
	out_cloud_ptr->points.clear();
	for (unsigned int i=0; i<in_cloud_ptr->points.size(); i++)
	{
		float origin_distance = sqrt( pow(in_cloud_ptr->points[i].x,2) + pow(in_cloud_ptr->points[i].y,2) );
		if (origin_distance > in_distance)
		{
			out_cloud_ptr->points.push_back(in_cloud_ptr->points[i]);
		}
	}
}

void velodyne_callback(const sensor_msgs::PointCloud2ConstPtr& in_sensor_cloud)
{
	if (!_using_sensor_cloud)
	{
		_using_sensor_cloud = true;

		pcl::PointCloud<pcl::PointXYZ>::Ptr current_sensor_cloud_ptr (new pcl::PointCloud<pcl::PointXYZ>);
		pcl::PointCloud<pcl::PointXYZ>::Ptr removed_points_cloud_ptr (new pcl::PointCloud<pcl::PointXYZ>);
		pcl::PointCloud<pcl::PointXYZ>::Ptr downsampled_cloud_ptr (new pcl::PointCloud<pcl::PointXYZ>);
		pcl::PointCloud<pcl::PointXYZ>::Ptr inlanes_cloud_ptr (new pcl::PointCloud<pcl::PointXYZ>);
		pcl::PointCloud<pcl::PointXYZ>::Ptr nofloor_cloud_ptr (new pcl::PointCloud<pcl::PointXYZ>);
		pcl::PointCloud<pcl::PointXYZ>::Ptr onlyfloor_cloud_ptr (new pcl::PointCloud<pcl::PointXYZ>);
		pcl::PointCloud<pcl::PointXYZ>::Ptr diffnormals_cloud_ptr (new pcl::PointCloud<pcl::PointXYZ>);
		pcl::PointCloud<pcl::PointXYZ>::Ptr clipped_cloud_ptr (new pcl::PointCloud<pcl::PointXYZ>);
		pcl::PointCloud<pcl::PointXYZRGB>::Ptr colored_clustered_cloud_ptr (new pcl::PointCloud<pcl::PointXYZRGB>);

		lidar_tracker::centroids centroids;
		lidar_tracker::CloudClusterArray cloud_clusters;
		jsk_recognition_msgs::BoundingBoxArray boundingbox_array;
		jsk_recognition_msgs::PolygonArray polygon_array;
		jsk_rviz_plugins::PictogramArray pictograms_array;

		pcl::fromROSMsg(*in_sensor_cloud, *current_sensor_cloud_ptr);

		_velodyne_header = in_sensor_cloud->header;

		cv::TickMeter timer;

		timer.reset();timer.start();

		if (_remove_points_upto > 0.0)
		{
			removePointsUpTo(current_sensor_cloud_ptr, removed_points_cloud_ptr, _remove_points_upto);
		}
		else
			removed_points_cloud_ptr = current_sensor_cloud_ptr;

		//std::cout << "Downsample before: " <<removed_points_cloud_ptr->points.size();
		if (_downsample_cloud)
			downsampleCloud(removed_points_cloud_ptr, downsampled_cloud_ptr, _leaf_size);
		else
			downsampled_cloud_ptr =removed_points_cloud_ptr;

		//std::cout << " after: " <<downsampled_cloud_ptr->points.size();
		timer.stop(); //std::cout << "downsampleCloud:" << timer.getTimeMilli() << "ms" << std::endl;

		timer.reset();timer.start();
		clipCloud(downsampled_cloud_ptr, clipped_cloud_ptr, _clip_min_height, _clip_max_height);
		timer.stop(); //std::cout << "clipCloud:" << clipped_cloud_ptr->points.size() << "time " << timer.getTimeMilli() << "ms" << std::endl;

		timer.reset();timer.start();
		if(_keep_lanes)
			keepLanePoints(clipped_cloud_ptr, inlanes_cloud_ptr, _keep_lane_left_distance, _keep_lane_right_distance);
		else
			inlanes_cloud_ptr = clipped_cloud_ptr;
		timer.stop(); //std::cout << "keepLanePoints:" << timer.getTimeMilli() << "ms" << std::endl;

		timer.reset();timer.start();
		if(_remove_ground)
		{
			removeFloor(inlanes_cloud_ptr, nofloor_cloud_ptr, onlyfloor_cloud_ptr);
			publishCloud(&_pub_ground_cloud, onlyfloor_cloud_ptr);
		}
		else
			nofloor_cloud_ptr = inlanes_cloud_ptr;
		timer.stop(); //std::cout << "removeFloor:" << timer.getTimeMilli() << "ms" << std::endl;

		publishCloud(&_pub_points_lanes_cloud, nofloor_cloud_ptr);

		timer.reset();timer.start();
		if (_use_diffnormals)
			differenceNormalsSegmentation(nofloor_cloud_ptr, diffnormals_cloud_ptr);
		else
			diffnormals_cloud_ptr = nofloor_cloud_ptr;
		timer.stop(); //std::cout << "differenceNormalsSegmentation:" << timer.getTimeMilli() << "ms" << std::endl;

		timer.reset();timer.start();
		segmentByDistance(diffnormals_cloud_ptr, colored_clustered_cloud_ptr, boundingbox_array, centroids, cloud_clusters, polygon_array, pictograms_array);
		//timer.stop(); std::cout << "segmentByDistance:" << timer.getTimeMilli() << "ms" << std::endl;

		timer.reset();timer.start();
		publishColorCloud(&_pub_cluster_cloud, colored_clustered_cloud_ptr);
		timer.stop(); //std::cout << "publishColorCloud:" << timer.getTimeMilli() << "ms" << std::endl;
		// Publish BB
		boundingbox_array.header = _velodyne_header;

		_pub_jsk_hulls.publish(polygon_array);//publish convex hulls
		_pub_text_pictogram.publish(pictograms_array);//publish_ids

		timer.reset();timer.start();
		publishBoundingBoxArray(&_pub_jsk_boundingboxes, boundingbox_array, _output_frame, _velodyne_header);
		centroids.header = _velodyne_header;
		timer.stop(); //std::cout << "publishBoundingBoxArray:" << timer.getTimeMilli() << "ms" << std::endl;

		timer.reset();timer.start();
		publishCentroids(&_centroid_pub, centroids, _output_frame, _velodyne_header);
		timer.stop(); //std::cout << "publishCentroids:" << timer.getTimeMilli() << "ms" << std::endl;

		_marker_pub.publish(_visualization_marker);
		_visualization_marker.points.clear();//transform? is it used?
		cloud_clusters.header = _velodyne_header;

		timer.reset();timer.start();
		publishCloudClusters(&_pub_clusters_message, cloud_clusters, _output_frame, _velodyne_header);
		timer.stop(); //std::cout << "publishCloudClusters:" << timer.getTimeMilli() << "ms" << std::endl << std::endl;

		_using_sensor_cloud = false;
	}
}

/*
void vectormap_callback(const visualization_msgs::MarkerArray::Ptr in_vectormap_markers)
{
	float min_x=std::numeric_limits<float>::max();float max_x=-std::numeric_limits<float>::max();
	float min_y=std::numeric_limits<float>::max();float max_y=-std::numeric_limits<float>::max();
	pcl::PointXYZ min_point;
	pcl::PointXYZ max_point;
	std::vector<geometry_msgs::Point> vectormap_points;
	std::string marker_frame;
	double map_scale = -10.0;
	for(auto i=in_vectormap_markers->markers.begin(); i!= in_vectormap_markers->markers.end(); i++)
	{
		visualization_msgs::Marker current_marker = *i;
		marker_frame = current_marker.header.frame_id;
		if (current_marker.ns == "road_edge")
		{
			for (unsigned int j=0; j< current_marker.points.size(); j++)
			{
				geometry_msgs::Point p = current_marker.points[j];
				p.x*=map_scale;
				p.y*=map_scale;
				if(p.x<min_x)	min_x = p.x;
				if(p.y<min_y)	min_y = p.y;
				if(p.x>max_x)	max_x = p.x;
				if(p.y>max_y)	max_y = p.y;
				vectormap_points.push_back(p);
			}
		}
	}
	min_point.x = min_x;	min_point.y = min_y;
	max_point.x = max_x;	max_point.y = max_y;

	min_point.x*=-1.0;
	min_point.y*=-1.0;
	//translate the points to the minimum point
	for (auto i=vectormap_points.begin(); i!=vectormap_points.end(); i++)
	{
		(*i).x+=min_point.x;
		(*i).y+=min_point.y;
	}
	max_point.x+=min_point.x;
	max_point.y+=min_point.y;
	//get world tf
	std::string error_transform_msg;
	tf::Vector3 map_origin_point;
	if(_transform_listener->waitForTransform("/map", marker_frame, ros::Time(0), ros::Duration(5), ros::Duration(0.1), &error_transform_msg))
	{
		_transform_listener->lookupTransform("/map", marker_frame, ros::Time(0), *_transform);
		map_origin_point = _transform->getOrigin();
		map_origin_point.setX( map_origin_point.x() - min_point.x);
		map_origin_point.setY( map_origin_point.y() - min_point.y);
	}
	else
	{
		ROS_INFO("Euclidean Cluster (vectormap_callback): %s", error_transform_msg.c_str());
	}

	cv::Mat map_image = cv::Mat::zeros(max_point.y, max_point.x, CV_8UC3);

	std::cout << "W,H:" << max_point << std::endl;

	cv::Point image_start_point (vectormap_points[0].x, vectormap_points[0].y);
	cv::Point prev_point = image_start_point;
	for (auto i=vectormap_points.begin(); i!=vectormap_points.end(); i++)
	{
		cv::line(map_image, prev_point, cv::Point((int)(i->x), (int)(i->y)), cv::Scalar::all(255));

		prev_point.x = (int)(i->x);
		prev_point.y = (int)(i->y);
	}
	cv::circle(map_image, image_start_point, 3, cv::Scalar(255,0,0));
	cv::imshow("vectormap", map_image);
	cv::waitKey(0);
}*/

int main (int argc, char** argv)
{
	// Initialize ROS
	ros::init (argc, argv, "euclidean_cluster");

	ros::NodeHandle h;
	ros::NodeHandle private_nh("~");

	tf::StampedTransform transform;
	tf::TransformListener listener;

	_transform = &transform;
	_transform_listener = &listener;

#if (CV_MAJOR_VERSION == 3)
	generateColors(_colors, 100);
#else
	cv::generateColors(_colors, 100);
#endif

	_pub_cluster_cloud = h.advertise<sensor_msgs::PointCloud2>("/points_cluster",1);
	_pub_ground_cloud = h.advertise<sensor_msgs::PointCloud2>("/points_ground",1);
	_centroid_pub = h.advertise<lidar_tracker::centroids>("/cluster_centroids",1);
	_marker_pub = h.advertise<visualization_msgs::Marker>("centroid_marker",1);

	_pub_points_lanes_cloud = h.advertise<sensor_msgs::PointCloud2>("/points_lanes",1);
	_pub_jsk_boundingboxes = h.advertise<jsk_recognition_msgs::BoundingBoxArray>("/bounding_boxes",1);
	_pub_jsk_hulls = h.advertise<jsk_recognition_msgs::PolygonArray>("/cluster_hulls",1);
	_pub_clusters_message = h.advertise<lidar_tracker::CloudClusterArray>("/cloud_clusters",1);
	_pub_text_pictogram = h.advertise<jsk_rviz_plugins::PictogramArray>("cluster_ids", 10); ROS_INFO("output pictograms topic: %s", "cluster_id");
	_pub_ray = h.advertise<visualization_msgs::MarkerArray>("debug_marker", 1, true);
	_pub_text_markers_array = h.advertise<visualization_msgs::MarkerArray>("debug_marker_text", 1, true);
    pose_arraybox_pub=h.advertise<geometry_msgs::PoseArray>("double_decker_parameters",1000);

	std::string points_topic;

	_using_sensor_cloud = false;

	if (private_nh.getParam("points_node", points_topic))
	{
		ROS_INFO("euclidean_cluster > Setting points node to %s", points_topic.c_str());
	}
	else
	{
		ROS_INFO("euclidean_cluster > No points node received, defaulting to points_raw, you can use _points_node:=YOUR_TOPIC");
		points_topic = "/points_raw";
	}

	_use_diffnormals = false;
	if (private_nh.getParam("use_diffnormals", _use_diffnormals))
	{
		if (_use_diffnormals)
			ROS_INFO("Euclidean Clustering: Applying difference of normals on clustering pipeline");
		else
			ROS_INFO("Euclidean Clustering: Difference of Normals will not be used.");
	}

	/* Initialize tuning parameter */
	private_nh.param("downsample_cloud", _downsample_cloud, false);	ROS_INFO("downsample_cloud: %d", _downsample_cloud);
	private_nh.param("remove_ground", _remove_ground, true);		ROS_INFO("remove_ground: %d", _remove_ground);
	private_nh.param("leaf_size", _leaf_size, 0.1);					ROS_INFO("leaf_size: %f", _leaf_size);
	private_nh.param("cluster_size_min", _cluster_size_min, 20);	ROS_INFO("cluster_size_min %d", _cluster_size_min);
	private_nh.param("cluster_size_max", _cluster_size_max, 100000);ROS_INFO("cluster_size_max: %d", _cluster_size_max);
	private_nh.param("pose_estimation", _pose_estimation, false);	ROS_INFO("pose_estimation: %d", _pose_estimation);
	private_nh.param("clip_min_height", _clip_min_height, -1.3);	ROS_INFO("clip_min_height: %f", _clip_min_height);
	private_nh.param("clip_max_height", _clip_max_height, 0.5);		ROS_INFO("clip_max_height: %f", _clip_max_height);
	private_nh.param("keep_lanes", _keep_lanes, false);				ROS_INFO("keep_lanes: %d", _keep_lanes);
	private_nh.param("keep_lane_left_distance", _keep_lane_left_distance, 5.0);		ROS_INFO("keep_lane_left_distance: %f", _keep_lane_left_distance);
	private_nh.param("keep_lane_right_distance", _keep_lane_right_distance, 5.0);	ROS_INFO("keep_lane_right_distance: %f", _keep_lane_right_distance);
	private_nh.param("clustering_thresholds", _clustering_thresholds);
	private_nh.param("clustering_distances", _clustering_distances);
	private_nh.param("max_boundingbox_side", _max_boundingbox_side, 10.0);				ROS_INFO("max_boundingbox_side: %f", _max_boundingbox_side);
	private_nh.param("cluster_merge_threshold", _cluster_merge_threshold, 1.5);			ROS_INFO("cluster_merge_threshold: %f", _cluster_merge_threshold);
	private_nh.param<std::string>("output_frame", _output_frame, "velodyne");			ROS_INFO("output_frame: %s", _output_frame.c_str());

	private_nh.param("use_vector_map", _use_vector_map, false);							ROS_INFO("use_vector_map: %d", _use_vector_map);
	private_nh.param<std::string>("vectormap_frame", _vectormap_frame, "map");			ROS_INFO("vectormap_frame: %s", _output_frame.c_str());

	private_nh.param("remove_points_upto", _remove_points_upto, 0.0);		ROS_INFO("remove_points_upto: %f", _remove_points_upto);


	_velodyne_transform_available = false;

	if (_clustering_distances.size()!=4)
	{
		_clustering_distances = {15, 30, 45, 60};//maximum distance from sensor origin to separate segments
	}
	if (_clustering_thresholds.size()!=5)
	{
		_clustering_thresholds = {0.5, 1.1, 1.6, 2.1, 2.6};//Nearest neighbor distance threshold for each segment
	}

	std::cout << "_clustering_thresholds: "; for (auto i = _clustering_thresholds.begin(); i != _clustering_thresholds.end(); ++i)  std::cout << *i << ' '; std::cout << std::endl;
	std::cout << "_clustering_distances: ";for (auto i = _clustering_distances.begin(); i != _clustering_distances.end(); ++i)  std::cout << *i << ' '; std::cout <<std::endl;

	// Create a ROS subscriber for the input point cloud
	ros::Subscriber sub = h.subscribe (points_topic, 1, velodyne_callback);
	//ros::Subscriber sub_vectormap = h.subscribe ("vector_map", 1, vectormap_callback);
	_vectormap_server = h.serviceClient<vector_map_server::PositionState>("vector_map_server/is_way_area");

	_visualization_marker.header.frame_id = "velodyne";
	_visualization_marker.header.stamp = ros::Time();
	_visualization_marker.ns = "my_namespace";
	_visualization_marker.id = 0;
	_visualization_marker.type = visualization_msgs::Marker::SPHERE_LIST;
	_visualization_marker.action = visualization_msgs::Marker::ADD;
	_visualization_marker.scale.x = 1.0;
	_visualization_marker.scale.y = 1.0;
	_visualization_marker.scale.z = 1.0;
	_visualization_marker.color.a = 1.0;
	_visualization_marker.color.r = 0.0;
	_visualization_marker.color.g = 0.0;
	_visualization_marker.color.b = 1.0;
	// marker.lifetime = ros::Duration(0.1);
	_visualization_marker.frame_locked = true;

	// Spin
	ros::spin ();
}
