/**\file principal_component_analysis.hpp
 * \brief Description...
 *
 * @version 1.0
 * @author Carlos Miguel Correia da Costa
 */

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   <includes>   <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
#include <dynamic_robot_localization/cloud_matchers/point_matchers/principal_component_analysis.h>
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   </includes>  <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

namespace dynamic_robot_localization {

// =============================================================================  <public-section>  ============================================================================
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   <constructors-destructor>   <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   </constructors-destructor>  <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   <PrincipalComponentAnalysis-functions>   <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
template<typename PointT>
void PrincipalComponentAnalysis<PointT>::setupConfigurationFromParameterServer(ros::NodeHandlePtr& node_handle, ros::NodeHandlePtr& private_node_handle, std::string configuration_namespace) {
	CloudMatcher<PointT>::setupTFConfigurationsFromParameterServer(node_handle, private_node_handle, configuration_namespace);
	CloudMatcher<PointT>::setupAlignedPointCloudPublisher(node_handle, private_node_handle, configuration_namespace);
	private_node_handle->param(configuration_namespace + "flip_pca_z_axis_for_aligning_it_to_the_cluster_centroid_z_normal", flip_pca_z_axis_for_aligning_it_to_the_cluster_centroid_z_normal_, true);
	private_node_handle->param(configuration_namespace + "flip_pca_z_axis_for_aligning_it_to_the_pointcloud_custom_z_flip_axis", flip_pca_z_axis_for_aligning_it_to_the_pointcloud_custom_z_flip_axis_, false);
	private_node_handle->param(configuration_namespace + "flip_pca_x_axis_for_aligning_it_to_the_pointcloud_custom_x_flip_axis", flip_pca_x_axis_for_aligning_it_to_the_pointcloud_custom_x_flip_axis_, true);
	private_node_handle->param(configuration_namespace + "custom_z_flip_axis/x", custom_z_flip_axis_(0), 0.0);
	private_node_handle->param(configuration_namespace + "custom_z_flip_axis/y", custom_z_flip_axis_(1), 0.0);
	private_node_handle->param(configuration_namespace + "custom_z_flip_axis/z", custom_z_flip_axis_(2), 1.0);
	private_node_handle->param(configuration_namespace + "custom_x_flip_axis/x", custom_x_flip_axis_(0), 0.0);
	private_node_handle->param(configuration_namespace + "custom_x_flip_axis/y", custom_x_flip_axis_(1), 0.0);
	private_node_handle->param(configuration_namespace + "custom_x_flip_axis/z", custom_x_flip_axis_(2), 1.0);
	custom_z_flip_axis_.normalize();
	custom_x_flip_axis_.normalize();
}

template<typename PointT>
bool PrincipalComponentAnalysis<PointT>::registerCloud(typename pcl::PointCloud<PointT>::Ptr& ambient_pointcloud,
		typename pcl::search::KdTree<PointT>::Ptr& ambient_pointcloud_search_method,
		typename pcl::PointCloud<PointT>::Ptr& pointcloud_keypoints,
		tf2::Transform& best_pose_correction_out, std::vector< tf2::Transform >& accepted_pose_corrections_out, typename pcl::PointCloud<PointT>::Ptr& pointcloud_registered_out, bool return_aligned_keypoints) {
	accepted_pose_corrections_out.clear();
	CloudMatcher<PointT>::cloud_align_time_ms_ = 0;
	PerformanceTimer performance_timer;
	performance_timer.start();

	PointT centroid_with_normal;
	pcl::computeCentroid(*ambient_pointcloud, centroid_with_normal);
	Eigen::Vector4f centroid(centroid_with_normal.x, centroid_with_normal.y, centroid_with_normal.z, 1.0f);
	Eigen::Vector3f centroid_normal(centroid_with_normal.normal_x, centroid_with_normal.normal_y, centroid_with_normal.normal_z);
	centroid_normal.normalize();

	Eigen::Matrix3f covariance_matrix;
	pcl::computeCovarianceMatrixNormalized(*ambient_pointcloud, centroid, covariance_matrix);
	Eigen::SelfAdjointEigenSolver<Eigen::Matrix3f> solver(covariance_matrix, Eigen::ComputeEigenvectors);
	Eigen::Matrix3f eigen_vectors = solver.eigenvectors(); // eigen vectors are already normalized and sorted from min to max eigen value
	eigen_vectors.col(0) = eigen_vectors.col(2).cross(eigen_vectors.col(1));

	if (flip_pca_z_axis_for_aligning_it_to_the_cluster_centroid_z_normal_) {
		double dot_product_between_centroid_normal_and_z_eigen_vector =
				centroid_normal(0) * eigen_vectors(0,0) +
				centroid_normal(1) * eigen_vectors(1,0) +
				centroid_normal(2) * eigen_vectors(2,0);
		if (dot_product_between_centroid_normal_and_z_eigen_vector < 0.0) {
			// align the z eigen vector with the centroid normal by rotating 180º around X when the diff_angle_vectors > 180º --> cos(diff_angle_vectors) < 0
			// useful for ensuring that the PCA Z axis is always pointing to the objects surface outside
			eigen_vectors.col(0) *= -1.0f;
			eigen_vectors.col(1) *= -1.0f;
			ROS_DEBUG_STREAM("Flipped PCA Z axis for aligning it to the cluster centroid Z normal [" << centroid_normal(0) << ", " << centroid_normal(1) << ", " << centroid_normal(2) << "]");
		}
	}

	if (flip_pca_z_axis_for_aligning_it_to_the_pointcloud_custom_z_flip_axis_) {
		double dot_product_between_custom_z_flip_axis_and_z_eigen_vector =
				custom_z_flip_axis_(0) * eigen_vectors(0,0) +
				custom_z_flip_axis_(1) * eigen_vectors(1,0) +
				custom_z_flip_axis_(2) * eigen_vectors(2,0);
		if (dot_product_between_custom_z_flip_axis_and_z_eigen_vector < 0.0) {
			// align the z eigen vector with a given custom axis by rotating 180º around X when the diff_angle_vectors > 180º --> cos(diff_angle_vectors) < 0
			// useful for ensuring that the PCA Z axis is always pointing along a given direction
			eigen_vectors.col(0) *= -1.0f;
			eigen_vectors.col(1) *= -1.0f;
			ROS_DEBUG_STREAM("Flipped PCA Z axis for aligning it to the custom axis [" << custom_z_flip_axis_(0) << ", " << custom_z_flip_axis_(1) << ", " << custom_z_flip_axis_(2) << "]");
		}
	}

	if (flip_pca_x_axis_for_aligning_it_to_the_pointcloud_custom_x_flip_axis_) {
		double dot_product_between_custom_x_flip_axis_and_x_eigen_vector =
				custom_x_flip_axis_(0) * eigen_vectors(0,2) +
				custom_x_flip_axis_(1) * eigen_vectors(1,2) +
				custom_x_flip_axis_(2) * eigen_vectors(2,2);
		if (dot_product_between_custom_x_flip_axis_and_x_eigen_vector < 0.0) {
			// align the x eigen vector with a given custom axis by rotating 180º around Z when the diff_angle_vectors > 180º --> cos(diff_angle_vectors) < 0
			// useful for ensuring that the PCA X axis is always pointing along a given direction
			eigen_vectors.col(2) *= -1.0f;
			eigen_vectors.col(1) *= -1.0f;
			ROS_DEBUG_STREAM("Flipped PCA X axis for aligning it to the custom axis [" << custom_x_flip_axis_(0) << ", " << custom_x_flip_axis_(1) << ", " << custom_x_flip_axis_(2) << "]");
		}
	}

	Eigen::Matrix4f final_transformation_pca;
	final_transformation_pca << eigen_vectors(0,2), eigen_vectors(0,1), eigen_vectors(0,0), centroid(0),
								eigen_vectors(1,2), eigen_vectors(1,1), eigen_vectors(1,0), centroid(1),
								eigen_vectors(2,2), eigen_vectors(2,1), eigen_vectors(2,0), centroid(2),
								0.0f, 0.0f, 0.0f, 1.0f;
	Eigen::Matrix4f final_transformation = final_transformation_pca.inverse();

	CloudMatcher<PointT>::cloud_align_time_ms_ = performance_timer.getElapsedTimeInMilliSec();
	pcl::transformPointCloudWithNormals(*ambient_pointcloud, *pointcloud_registered_out, final_transformation);
	pointcloud_registered_out->header = ambient_pointcloud->header;
	if (CloudMatcher<PointT>::cloud_publisher_ && pointcloud_registered_out) {
		CloudMatcher<PointT>::cloud_publisher_->publishPointCloud(*pointcloud_registered_out);
	}

	return CloudMatcher<PointT>::postProcessRegistrationMatrix(ambient_pointcloud, final_transformation, best_pose_correction_out);
}
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   </PrincipalComponentAnalysis-functions>  <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
// =============================================================================  </public-section>  ===========================================================================

// =============================================================================   <protected-section>   =======================================================================
// =============================================================================   </protected-section>  =======================================================================

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   <template instantiations>   <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   </template instantiations>  <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

} /* namespace dynamic_robot_localization */
