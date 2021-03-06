// Copyright 2017 National Technology & Engineering Solutions of Sandia, LLC
// (NTESS), National Renewable Energy Laboratory, University of Texas Austin,
// Northwest Research Associates. Under the terms of Contract DE-NA0003525
// with NTESS, the U.S. Government retains certain rights in this software.
//
// This software is released under the BSD 3-clause license. See LICENSE file
// for more details.
//

/** @file ActuatorSimple.h
 *  @brief A class to handle fixed wing/simple 2D aerodynamic blade simulations
 *
 */

#ifndef ActuatorSimple_h
#define ActuatorSimple_h

#include <stk_util/parallel/ParallelVectorConcat.hpp>
#include "Actuator.h"

namespace sierra {
namespace nalu {

class Realm;

/** Class that holds all of the information relevant to each turbine
 *
 *
 */
//
class ActuatorSimpleInfo : public ActuatorInfo
{
public:
    ActuatorSimpleInfo();
    virtual ~ActuatorSimpleInfo();
    ///< The Gaussian spreading width (chordwise, spanwise, thickness) [m]
    Coordinates epsilon_; 

    ///< epsilon / chord in (chord direction, tangential to chord, and spanwise)
    ///   [m]
    Coordinates epsilon_chord_;

    // The value of epsilon used for the tower [m]
    Coordinates epsilon_tower_;

    ///< The minimum epsilon allowed in the simulation [m]
    /// in the (chordwise, spanwise, thickness) directions
    Coordinates epsilon_min_;

    // The file to write the all the points
    std::string fileToDumpPoints_;
    
    ///< Flag to activate the filtered lifting line correction
    /*!
    Martinez-Tossas, L., & Meneveau, C. (2019)
    Filtered lifting line theory and application to the actuator line model
    Journal of Fluid Mechanics, 863, 269-292
    */
    bool fllt_correction_;


    // Stuff needed for a simple blade
    size_t      num_force_pts_blade_;
    Coordinates p1_;                     // Start of the blade
    Coordinates p2_;                     // End of the blade
    //Coordinates epsilon_chord_;          
    std::vector<double> chord_table_;
    std::vector<double> twist_table_;
    std::vector<double> elem_area_;
    Coordinates p1ZeroAlphaDir_;         // Directon of zero alpha at p1
    Coordinates chordNormalDir_;         // Direction normal to chord
    Coordinates spanDir_;                // Direction in the span
    // For the polars
    std::vector<double> aoa_polartable_;
    std::vector<double> cl_polartable_;
    std::vector<double> cd_polartable_;
    bool isSimpleBlade_;
    int runOnProc_;
    size_t bladeId_;
};

/** Class that holds all of the search action for each actuator point
 *
 *
 */
//
class ActuatorSimplePointInfo : public ActuatorPointInfo
{
public:
    ActuatorSimplePointInfo(
        size_t globTurbId,
	size_t bladeId,
        Point centroidCoords,
        double searchRadius,
        Coordinates epsilon,
        Coordinates epsilon_opt,
        int nType, //fast::ActuatorNodeType nType,
        int forceInd);

    virtual ~ActuatorSimplePointInfo();

    size_t globTurbId_; ///< Global turbine number.

    ///< The Gaussian spreading width in (chordwise, spanwise, thickness)
    ///  directions for this actuator point.
    Coordinates epsilon_; 
    ///< The optimal epsilon for this actuator point [m]
    Coordinates epsilon_opt_;

    ///< HUB, BLADE, or TOWER - Defined by an enum.
    //fast::ActuatorNodeType nodeType_;
    int nodeType_;

    // The index this point resides in the total number of
    //   force points for the tower i.e. i \in
    //   [0,numForcePnts-1]
    int forcePntIndex_; 

    size_t bladeId_;
    double gasDensity_;
    Coordinates windSpeed_;
};

class ActuatorSimple : public Actuator
{
public:
    ActuatorSimple(Realm& realm, const YAML::Node& node);
    virtual ~ActuatorSimple();

    // load all of the options
    void load(const YAML::Node& node) override;

    // load the options for each turbine
    //void readTurbineData(int iTurb, fast::fastInputs& fi, YAML::Node turbNode);

    // setup part creation and nodal field registration (before populate_mesh())
    void setup() override;

    // allocate turbines to processors containing hub location
    void allocateTurbinesToProcs();

    // Allocate turbines to processors, initialize FAST and get location of
    // actuator points
    void initialize() override;

    // setup part creation and nodal field registration (after populate_mesh())
    void update();

    // split search operations for disk and line
    virtual void update_class_specific() = 0;

    // determine processor bounding box in the mesh
    void populate_candidate_procs();

    // fill in the map that will hold point and ghosted elements
    void create_actuator_point_info_map();

    // Update the actuator point info map
    void update_actuator_point_info_map();

    virtual void create_point_info_map_class_specific() = 0;

    // populate nodal field and output norms (if appropriate)
    void execute() override;

    // Create the indexing used toaccess actuator points as 
    //   (turbine number, blade number, actuator point number)
    void index_map();
    
    // This is the filtered lifting line correction
    // Martinez-Tossas and Meneveau. JFM 2019
    void filtered_lifting_line();

    virtual void execute_class_specific(
        const int nDim,
        const stk::mesh::FieldBase* coordinates,
        stk::mesh::FieldBase* actuator_source,
        const stk::mesh::FieldBase* dual_nodal_volume) = 0;

    // The the coordinates of individual points on the blade
    void get_blade_coordinates(
	 const int& nDim, 
	 std::vector<double> &coord, 
	 const Coordinates &p1,  
	 const Coordinates &p2, 
	 const int &nPts, const int &iNode);

    // Make sure vec is of length N
    std::vector<double> extend_double_vector(
	std::vector<double> vec, const unsigned N);

    // The the coordinates of individual points on the blade
    double get_blade_chord(
        std::vector<double> &chord_table, 
	const int& iNode);

    // The the coordinates of individual points on the blade
    std::vector<double> get_blade_area_elems(
        std::vector<double> chord_table, 
	const Coordinates &p1,  
	const Coordinates &p2,
	const int &nPts);

    // centroid of the element
    void compute_elem_centroid(
        const int& nDim, double* elemCentroid, const int& nodesPerElement);

    // compute the body force at an element given a
    // projection weighting.
    void compute_node_force_given_weight(
        const int& nDim,
        const double& g,
        const double* pointForce,
        double* nodeForce);

    // Spread the actuator force to a node vector
    void spread_actuator_force_to_node_vec(
        const int& nDim,
        std::set<stk::mesh::Entity>& nodeVec,
        // The force vector
        const std::vector<double>& actuator_force,
        // The tensor to indicate the orientation of the airfoil sections
        const std::vector<double>& orientation_tensor,
        const double* actuator_node_coordinates,
        const stk::mesh::FieldBase& coordinates,
        stk::mesh::FieldBase& actuator_source,
        const stk::mesh::FieldBase& dual_nodal_volume,
        const Coordinates& epsilon,
        const std::vector<double>& hubPt,
        const std::vector<double>& hubShftDir,
        std::vector<double>& thr,
        std::vector<double>& tor);

    void add_thrust_torque_contrib(
        const int& nDim,
        const double* nodeCoords,
        const double dVol,
        const std::vector<double>& nodeForce,
        const std::vector<double>& hubPt,
        const std::vector<double>& hubShftDir,
        std::vector<double>& thr,
        std::vector<double>& tor);

    std::string
    write_turbine_points_to_string(std::size_t turbNum, int width, int precision);

    void dump_turbine_points_to_file(std::size_t turbNum);

    int tStepRatio_; ///< Ratio of Nalu time step to FAST time step
    ///< (dtNalu/dtFAST) - Should be an integral number

    // bounding box data types for stk_search
    std::vector<boundingSphere>
    boundingHubSphereVec_; ///< bounding box around the hub point of each
    ///< turbine
    std::vector<boundingElementBox>
    boundingProcBoxVec_; ///< bounding box around all the nodes residing locally
    ///< on each processor

    virtual std::string get_class_name() override = 0;

    std::size_t numFastPoints_;

    std::vector<std::vector<double>> thrust;
    std::vector<std::vector<double>> torque;

    // Store the actuator point index
    // This vector is
    std::vector<std::vector<std::vector<int>>> indexMap_;

    // Stuff for the simple blade
    std::size_t n_simpleblades_;

    // Total forces as found by BEM theory
    std::vector<double> BladeTotalLift;
    std::vector<double> BladeTotalDrag;
    std::vector<double> BladeAvgAlpha;
    std::vector<std::vector<double>> BladeAvgWS2D;

    bool debug_output_;
};

} // namespace nalu
} // namespace sierra

#endif
