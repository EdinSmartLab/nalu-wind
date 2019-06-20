/*------------------------------------------------------------------------*/
/*  Copyright 2014 Sandia Corporation.                                    */
/*  This software is released under the license detailed                  */
/*  in the file, LICENSE, which is located in the top-level Nalu          */
/*  directory structure                                                   */
/*------------------------------------------------------------------------*/


#ifndef TpetraLinearSystem_h
#define TpetraLinearSystem_h

#include <LinearSystem.h>

#include <KokkosInterface.h>

#include <Kokkos_DefaultNode.hpp>
#include <Tpetra_MultiVector.hpp>
#include <Tpetra_CrsMatrix.hpp>

#include <stk_mesh/base/Types.hpp>
#include <stk_mesh/base/Entity.hpp>
#include <stk_mesh/base/FieldBase.hpp>

#include <stk_ngp/Ngp.hpp>

#include <vector>
#include <string>
#include <unordered_map>

namespace stk {
class CommNeighbors;
}

namespace sierra {
namespace nalu {

class Realm;
class EquationSystem;
class LinearSolver;
class LocalGraphArrays;

typedef std::unordered_map<stk::mesh::EntityId, size_t>  MyLIDMapType;

typedef std::pair<stk::mesh::Entity, stk::mesh::Entity> Connection;


class TpetraLinearSystem : public LinearSystem
{
public:
  typedef LinSys::GlobalOrdinal GlobalOrdinal;
  typedef LinSys::LocalOrdinal  LocalOrdinal;

  TpetraLinearSystem(
    Realm &realm,
    const unsigned numDof,
    EquationSystem *eqSys,
    LinearSolver * linearSolver);
  ~TpetraLinearSystem();

 // Utility functions
  GlobalOrdinal get_entity_tpet_id(const stk::mesh::Entity& node);

   // Graph/Matrix Construction
  void buildNodeGraph(const stk::mesh::PartVector & parts); // for nodal assembly (e.g., lumped mass and source)
  void buildFaceToNodeGraph(const stk::mesh::PartVector & parts); // face->node assembly
  void buildEdgeToNodeGraph(const stk::mesh::PartVector & parts); // edge->node assembly
  void buildElemToNodeGraph(const stk::mesh::PartVector & parts); // elem->node assembly
  void buildReducedElemToNodeGraph(const stk::mesh::PartVector & parts); // elem (nearest nodes only)->node assembly
  void buildFaceElemToNodeGraph(const stk::mesh::PartVector & parts); // elem:face->node assembly
  void buildNonConformalNodeGraph(const stk::mesh::PartVector & parts); // nonConformal->node assembly
  void buildOversetNodeGraph(const stk::mesh::PartVector & parts); // overset->elem_node assembly
  void storeOwnersForShared();
  void finalizeLinearSystem();

  sierra::nalu::CoeffApplier* get_coeff_applier();

  // Matrix Assembly
  void zeroSystem();

  void sumInto(
      unsigned numEntities,
      const stk::mesh::Entity* entities,
      const SharedMemView<const double*> & rhs,
      const SharedMemView<const double**> & lhs,
      const SharedMemView<int*> & localIds,
      const SharedMemView<int*> & sortPermutation,
      const char * trace_tag);

  void sumInto(
    unsigned numEntities,
    const ngp::Mesh::ConnectedNodes& entities,
    const SharedMemView<const double*,DeviceShmem> & rhs,
    const SharedMemView<const double**,DeviceShmem> & lhs,
    const SharedMemView<int*,DeviceShmem> & localIds,
    const SharedMemView<int*,DeviceShmem> & sortPermutation,
    const char * trace_tag);

  void sumInto(
    const std::vector<stk::mesh::Entity> & entities,
    std::vector<int> &scratchIds,
    std::vector<double> &scratchVals,
    const std::vector<double> & rhs,
    const std::vector<double> & lhs,
    const char *trace_tag=0
    );

  void applyDirichletBCs(
    stk::mesh::FieldBase * solutionField,
    stk::mesh::FieldBase * bcValuesField,
    const stk::mesh::PartVector & parts,
    const unsigned beginPos,
    const unsigned endPos);

  void prepareConstraints(
    const unsigned beginPos,
    const unsigned endPos);

  /** Reset LHS and RHS for the given set of nodes to 0
   *
   *  @param nodeList A list of STK node entities whose rows are zeroed out
   *  @param beginPos Starting index (usually 0)
   *  @param endPos Terminating index (1 for scalar quantities; nDim for vectors)
   */
  virtual void resetRows(
    const std::vector<stk::mesh::Entity>& nodeList,
    const unsigned beginPos,
    const unsigned endPos,
    const double diag_value = 0.0,
    const double rhs_residual = 0.0);

  // Solve
  int solve(stk::mesh::FieldBase * linearSolutionField);
  void loadComplete();
  void writeToFile(const char * filename, bool useOwned=true);
  void printInfo(bool useOwned=true);
  void writeSolutionToFile(const char * filename, bool useOwned=true);
  size_t lookup_myLID(MyLIDMapType& myLIDs, stk::mesh::EntityId entityId, const char* /* msg */ =nullptr, stk::mesh::Entity /* entity */ = stk::mesh::Entity())
  {
    return myLIDs[entityId];
  }


  int getDofStatus(stk::mesh::Entity node) const;

  int getRowLID(stk::mesh::Entity node) { return entityToLID_[node.local_offset()]; }
  int getColLID(stk::mesh::Entity node) { return entityToColLID_[node.local_offset()]; }

  Teuchos::RCP<LinSys::Graph>  getOwnedGraph() { return ownedGraph_; }
  Teuchos::RCP<LinSys::Matrix> getOwnedMatrix() { return ownedMatrix_; }
  Teuchos::RCP<LinSys::MultiVector> getOwnedRhs() { return ownedRhs_; }

  class TpetraLinSysCoeffApplier : public CoeffApplier
  {
  public:
    KOKKOS_FUNCTION
    TpetraLinSysCoeffApplier(LinSys::LocalMatrix ownedLclMatrix,
                             LinSys::LocalMatrix sharedNotOwnedLclMatrix,
                             LinSys::LocalVector ownedLclRhs,
                             LinSys::LocalVector sharedNotOwnedLclRhs,
                             Kokkos::View<LocalOrdinal*,Kokkos::LayoutRight,MemSpace> entityLIDs,
                             Kokkos::View<LocalOrdinal*,Kokkos::LayoutRight,MemSpace> entityColLIDs,
                             int maxOwnedRowId, int maxSharedNotOwnedRowId, unsigned numDof)
    : ownedLocalMatrix_(ownedLclMatrix),
      sharedNotOwnedLocalMatrix_(sharedNotOwnedLclMatrix),
      ownedLocalRhs_(ownedLclRhs),
      sharedNotOwnedLocalRhs_(sharedNotOwnedLclRhs),
      entityToLID_(entityLIDs),
      entityToColLID_(entityColLIDs),
      maxOwnedRowId_(maxOwnedRowId), maxSharedNotOwnedRowId_(maxSharedNotOwnedRowId), numDof_(numDof),
      devicePointer_(nullptr)
    {}

    KOKKOS_FUNCTION
    ~TpetraLinSysCoeffApplier() {}

    KOKKOS_FUNCTION
    virtual void operator()(unsigned numEntities,
                            const ngp::Mesh::ConnectedNodes& entities,
                            const SharedMemView<int*,DeviceShmem> & localIds,
                            const SharedMemView<int*,DeviceShmem> & sortPermutation,
                            const SharedMemView<const double*,DeviceShmem> & rhs,
                            const SharedMemView<const double**,DeviceShmem> & lhs,
                            const char * trace_tag);

    void free_device_pointer();

    sierra::nalu::CoeffApplier* device_pointer();

  private:
    LinSys::LocalMatrix ownedLocalMatrix_, sharedNotOwnedLocalMatrix_;
    LinSys::LocalVector ownedLocalRhs_, sharedNotOwnedLocalRhs_;
    Kokkos::View<LocalOrdinal*,Kokkos::LayoutRight,MemSpace> entityToLID_;
    Kokkos::View<LocalOrdinal*,Kokkos::LayoutRight,MemSpace> entityToColLID_;
    int maxOwnedRowId_, maxSharedNotOwnedRowId_;
    unsigned numDof_;
    TpetraLinSysCoeffApplier* devicePointer_;
  };

private:
  void buildConnectedNodeGraph(stk::mesh::EntityRank rank,
                               const stk::mesh::PartVector& parts);

  void beginLinearSystemConstruction();

  void checkError( const int /* err_code */, const char * /* msg */) {}
  //  bool checkForZeroRow(bool useOwned, bool doThrow, bool doPrint);

  void compute_send_lengths(const std::unique_ptr<stk::mesh::Entity[]> & rowEntities, 
                            uint & rowEntities_csz,
         const std::vector<std::vector<stk::mesh::Entity> >& connections,
                            const std::vector<int>& neighborProcs,
                            stk::CommNeighbors& commNeighbors);

  void compute_graph_row_lengths(const std::unique_ptr<stk::mesh::Entity[]>& rowEntities,
                                 uint & rowEntities_csz,
         const std::vector<std::vector<stk::mesh::Entity> >& connections,
                                 LinSys::RowLengths& sharedNotOwnedRowLengths,
                                 LinSys::RowLengths& locallyOwnedRowLengths,
                                 stk::CommNeighbors& commNeighbors);

  void insert_graph_connections(const std::unique_ptr<stk::mesh::Entity[]>& rowEntities,
                                uint & rowEntities_csz,
         const std::vector<std::vector<stk::mesh::Entity> >& connections,
                                LocalGraphArrays& locallyOwnedGraph,
                                LocalGraphArrays& sharedNotOwnedGraph);

  void fill_entity_to_row_LID_mapping();
  void fill_entity_to_col_LID_mapping();

  void copy_tpetra_to_stk(
    const Teuchos::RCP<LinSys::MultiVector> tpetraVector,
    stk::mesh::FieldBase * stkField);

  // This method copies a stk::mesh::field to a tpetra multivector. Each dof/node is written into a different
  // vector in the multivector.
  void copy_stk_to_tpetra(stk::mesh::FieldBase * stkField,
    const Teuchos::RCP<LinSys::MultiVector> tpetraVector);

  int insert_connection(const stk::mesh::Entity &a, const stk::mesh::Entity &b);
  void addConnections(const stk::mesh::Entity* entities, const size_t&);
  void expand_unordered_map(unsigned newCapacityNeeded);

  std::unique_ptr<stk::mesh::Entity[]> ownedAndSharedNodes_;
  uint ownedAndSharedNodes_csz_;
  std::vector<std::vector<stk::mesh::Entity> > connections_;
  std::vector<GlobalOrdinal> totalGids_;
  std::set<std::pair<int,GlobalOrdinal> > ownersAndTpetGids_;
  std::unique_ptr<int[]> sharedPids_;

  Teuchos::RCP<LinSys::Node>   node_;

  // all rows, otherwise known as col map
  Teuchos::RCP<LinSys::Map>    totalColsMap_;

  // Map of rows my proc owns (locally owned)
  Teuchos::RCP<LinSys::Map>    ownedRowsMap_;

  // Only nodes that share with other procs that I don't own
  Teuchos::RCP<LinSys::Map>    sharedNotOwnedRowsMap_;

  Teuchos::RCP<LinSys::Graph>  ownedGraph_;
  Teuchos::RCP<LinSys::Graph>  sharedNotOwnedGraph_;

  Teuchos::RCP<LinSys::Matrix> ownedMatrix_;
  Teuchos::RCP<LinSys::MultiVector> ownedRhs_;
  LinSys::LocalMatrix ownedLocalMatrix_;
  LinSys::LocalMatrix sharedNotOwnedLocalMatrix_;
  LinSys::LocalVector ownedLocalRhs_;
  LinSys::LocalVector sharedNotOwnedLocalRhs_;

  Teuchos::RCP<LinSys::Matrix>      sharedNotOwnedMatrix_;
  Teuchos::RCP<LinSys::MultiVector> sharedNotOwnedRhs_;

  Teuchos::RCP<LinSys::MultiVector> sln_;
  Teuchos::RCP<LinSys::MultiVector> globalSln_;
  Teuchos::RCP<LinSys::Export>      exporter_;

  MyLIDMapType myLIDs_;
  Kokkos::View<LocalOrdinal*,Kokkos::LayoutRight,MemSpace> entityToColLID_;
  Kokkos::View<LocalOrdinal*,Kokkos::LayoutRight,MemSpace> entityToLID_;
  LocalOrdinal maxOwnedRowId_; // = num_owned_nodes * numDof_
  LocalOrdinal maxSharedNotOwnedRowId_; // = (num_owned_nodes + num_sharedNotOwned_nodes) * numDof_
  std::vector<int> sortPermutation_;

  GlobalOrdinal iLower_; // lowest row GID for this owned map
  GlobalOrdinal iUpper_; // higest row GID for this owned map
  size_t        numOwnedRows_;
  GlobalOrdinal maxGlobalRowId_;
  int myRank_; // this procs rank

};

template<typename T1, typename T2>
void copy_kokkos_unordered_map(const Kokkos::UnorderedMap<T1,T2>& src,
                               Kokkos::UnorderedMap<T1,T2>& dest)
{
  if (src.capacity() > dest.capacity()) {
    dest = Kokkos::UnorderedMap<T1,T2>(src.capacity());
  }

  unsigned capacity = src.capacity();
  unsigned fail_count = 0;
  for(unsigned i=0; i<capacity; ++i) {
    if (src.valid_at(i)) {
      auto insert_result = dest.insert(src.key_at(i));
      fail_count += insert_result.failed() ? 1 : 0;
    }
  }
  ThrowRequire(fail_count == 0);
}

int getDofStatus_impl(stk::mesh::Entity node, const Realm& realm);

} // namespace nalu
} // namespace Sierra

#endif
