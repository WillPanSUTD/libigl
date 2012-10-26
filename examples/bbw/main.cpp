
#include <igl/pathinfo.h>
#include <igl/readOBJ.h>
#include <igl/readOFF.h>
#include <igl/readMESH.h>
#include <igl/sample_edges.h>
#include <igl/cat.h>
#include <igl/faces_first.h>
#include <igl/readTGF.h>
#include <igl/tetgen/tetrahedralize.h>
#include <igl/launch_medit.h>
#include <igl/boundary_conditions.h>
#include <igl/mosek/bbw.h>
#include <igl/writeDMAT.h>
#include <igl/writeMESH.h>

#include <Eigen/Dense>

#include <Eigen/Dense>

#include <iostream>
#include <string>

// Whether medit program is install
const bool WITH_MEDIT = true;

const char * USAGE=
"Usage:\n"
"  ./bbw_demo shape{.obj|.off|.mesh} skeleton{.tgf|.bf}\n"
;

// Read a surface mesh from a {.obj|.off|.mesh} files
// Inputs:
//   mesh_filename  path to {.obj|.off|.mesh} file
// Outputs:
//   V  #V by 3 list of mesh vertex positions
//   F  #F by 3 list of triangle indices
// Returns true only if successfuly able to read file
bool load_mesh_from_file(
  const std::string mesh_filename,
  Eigen::MatrixXd & V,
  Eigen::MatrixXi & F)
{
  using namespace std;
  using namespace igl;
  using namespace Eigen;
  string dirname, basename, extension, filename;
  pathinfo(mesh_filename,dirname,basename,extension,filename);
  transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
  bool success = false;
  if(extension == "obj")
  {
    success = readOBJ(mesh_filename,V,F);
  }else if(extension == "off")
  {
    success = readOFF(mesh_filename,V,F);
  }else if(extension == "mesh")
  {
    // Unused Tets read from .mesh file
    MatrixXi Tets;
    success = readMESH(mesh_filename,V,Tets,F);
    // We're not going to use any input tets. Only the surface
    if(Tets.size() > 0 && F.size() == 0)
    {
      // If Tets read, but no faces then use surface of tet volume
    }else
    {
      // Rearrange vertices so that faces come first
      VectorXi IM;
      faces_first(V,F,IM);
      // Dont' bother reordering Tets, but this is how one would:
      //Tets = 
      //  Tets.unaryExpr(bind1st(mem_fun( static_cast<VectorXi::Scalar&
      //  (VectorXi::*)(VectorXi::Index)>(&VectorXi::operator())),
      //  &IM)).eval();
      // Don't throw away any interior vertices, since user may want weights
      // there
    }
  }else
  {
    cerr<<"Error: Unknown shape file format extension: ."<<extension<<endl;
    return false;
  }
  return success;
}

// Load a skeleton (bones, points and cage edges) from a {.bf|.tgf} file
//
// Inputs:
//   skel_filename  path to skeleton {.bf|.tgf} file
// Outputs:
//  C  # vertices by 3 list of vertex positions
//  P  # point-handles list of point handle indices
//  BE # bone-edges by 2 list of bone-edge indices
//  CE # cage-edges by 2 list of cage-edge indices
bool load_skeleton_from_file(
  const std::string skel_filename,
  Eigen::MatrixXd & C,
  Eigen::VectorXi & P,
  Eigen::MatrixXi & BE,
  Eigen::MatrixXi & CE)
{
  using namespace std;
  using namespace igl;
  using namespace Eigen;
  string dirname, basename, extension, filename;
  pathinfo(skel_filename,dirname,basename,extension,filename);
  transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
  bool success = false;
  if(extension == "tgf")
  {
    // Phony space for unused all edges and pseudo edges
    MatrixXi E;
    MatrixXi PE;
    success = readTGF(skel_filename,C,E,P,BE,CE,PE);
  }else
  {
    cerr<<"Error: Unknown skeleton file format extension: ."<<extension<<endl;
    return false;
  }
  return success;
}

// Mesh the interior of a given surface with tetrahedra which are graded (tend
// to be small near the surface and large inside) and conform to the given
// handles and samplings thereof.
//
// Inputs:
//  V  #V by 3 list of mesh vertex positions
//  F  #F by 3 list of triangle indices
//  C  #C by 3 list of vertex positions
//  P  #P list of point handle indices
//  BE #BE by 2 list of bone-edge indices
//  CE #CE by 2 list of cage-edge indices
// Outputs:
//  VV  #VV by 3 list of tet-mesh vertex positions
//  TT  #TT by 4 list of tetrahedra indices
//  FF  #FF by 3 list of surface triangle indices
// Returns true only on success
bool mesh_with_skeleton(
  const Eigen::MatrixXd & V,
  const Eigen::MatrixXi & F,
  const Eigen::MatrixXd & C,
  const Eigen::VectorXi & /*P*/,
  const Eigen::MatrixXi & BE,
  const Eigen::MatrixXi & CE,
  Eigen::MatrixXd & VV,
  Eigen::MatrixXi & TT,
  Eigen::MatrixXi & FF)
{
  using namespace Eigen;
  using namespace igl;
  using namespace std;
  // Collect all edges that need samples:
  MatrixXi BECE = cat(1,BE,CE);
  MatrixXd S;
  // Sample each edge with 10 samples. (Choice of 10 doesn't seem to matter so
  // much, but could under some circumstances)
  sample_edges(C,BECE,10,S);
  // Vertices we'll constrain tet mesh to meet
  MatrixXd VS = cat(1,V,S);
  // Boundary faces
  MatrixXi BF;
  // Use tetgen to mesh the interior of surface, this assumes surface:
  //   * has no holes
  //   * has no non-manifold edges or vertices
  //   * has consistent orientation
  //   * has no self-intersections
  //   * has no 0-volume pieces
  // Default settings pq100 tell tetgen to mesh interior of triangle mesh and
  // to produce a graded tet mesh
  cerr<<"tetgen begin()"<<endl;
  int status = tetrahedralize( VS,F,"pq100",VV,TT,FF);
  cerr<<"tetgen end()"<<endl;
  if(FF.rows() != F.rows())
  {
    // Issue a warning if the surface has changed
    cerr<<"mesh_with_skeleton: Warning: boundary faces != input faces"<<endl;
  }
  if(status != 0)
  {
    cerr<<
      "***************************************************************"<<endl<<
      "***************************************************************"<<endl<<
      "***************************************************************"<<endl<<
      "***************************************************************"<<endl<<
      "* mesh_with_skeleton: tetgen failed. Just meshing convex hull *"<<endl<<
      "***************************************************************"<<endl<<
      "***************************************************************"<<endl<<
      "***************************************************************"<<endl<<
      "***************************************************************"<<endl;
    // If meshing convex hull then use more regular mesh
    status = tetrahedralize(VS,F,"q1.414",VV,TT,FF);
    // I suppose this will fail if the skeleton is outside the mesh
    assert(FF.maxCoeff() < VV.rows());
    if(status != 0)
    {
      cerr<<"mesh_with_skeleton: tetgen failed again."<<endl;
      return false;
    }
  }
  // If you have medit installed then it's convenient to visualize the tet mesh
  // at this point
  if(WITH_MEDIT)
  {
    launch_medit(VV,TT,FF,false);
  }
  return true;
}

// Writes output files to /path/to/input/mesh-skeleton.dmat,
// mesh-volume.dmat, mesh-volume.mesh if input mesh was
// located at /path/to/input/mesh.obj and input skeleton was at
// /other/path/to/input/skel.tgf
// 
// Writes:
////   mesh.dmat  dense weights matrix corresponding to original input
////     vertices V
//   mesh-volume.dmat  dense weights matrix corresponding to all
//     vertices in tet mesh used for computation VV
//   mesh-volume.mesh  Tet mesh used for computation
//
// Inputs:
//   mesh_filename  path to {.obj|.off|.mesh} file
//   skel_filename  path to skeleton {.bf|.tgf} file
//   V  #V by 3 list of original mesh vertex positions
//   F  #F by 3 list of original triangle indices
//   VV  #VV by 3 list of tet-mesh vertex positions
//   TT  #TT by 4 list of tetrahedra indices
//   FF  #FF by 3 list of surface triangle indices
//   W   #VV by #W weights matrix
// Returns true on success
bool save_output(
  const std::string mesh_filename,
  const std::string /*skel_filename*/,
  const Eigen::MatrixXd & V,
  const Eigen::MatrixXi & /*F*/,
  const Eigen::MatrixXd & VV,
  const Eigen::MatrixXi & TT,
  const Eigen::MatrixXi & FF,
  const Eigen::MatrixXd & W)
{
  using namespace std;
  using namespace igl;
  using namespace Eigen;
  // build filename prefix out of input base names
  string prefix = "";
  {
    string dirname, basename, extension, filename;
    pathinfo(mesh_filename,dirname,basename,extension,filename);
    transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    prefix += dirname + "/" + filename;
  }

  //{
  //  string dirname, basename, extension, filename;
  //  pathinfo(skel_filename,dirname,basename,extension,filename);
  //  transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
  //  prefix += "-" + filename;
  //}

  // Keep track if any fail
  bool success = true;
  //// Weights matrix for just V. Assumes V prefaces VV
  //MatrixXd WV = W.block(0,0,V.rows(),W.cols());
  //// write dmat
  //success &= writeDMAT(prefix + ".dmat",WV);
  // write volume weights dmat
  success &= writeDMAT(prefix + "-volume.dmat",W);
  // write volume mesh
  success &= writeMESH(prefix + "-volume.mesh",VV,TT,FF);
  //// write surface OBJ with pseudocolor
  return success;
}

int main(int argc, char * argv[])
{
  using namespace std;
  using namespace Eigen;
  using namespace igl;
  if(argc<3)
  {
    cerr<<USAGE<<endl;
    return 1;
  }

  // #V by 3 list of mesh vertex positions
  MatrixXd V;
  // #F by 3 list of triangle indices
  MatrixXi F;
  // load mesh from .obj, .off or .mesh
  if(!load_mesh_from_file(argv[1],V,F))
  {
    return 1;
  }
  // "Skeleton" (handles) descriptors:
  // List of control and joint (bone endpoint) positions
  MatrixXd C;
  // List of point handles indexing C
  VectorXi P;
  // List of bone edges indexing C
  MatrixXi BE;
  // List of cage edges indexing *P*
  MatrixXi CE;
  // load skeleton (.tgf or .bf)
  if(!load_skeleton_from_file(argv[2],C,P,BE,CE))
  {
    return 1;
  }

  // Mesh with samples on skeleton
  // New vertices of tet mesh, V prefaces VV
  MatrixXd VV;
  // Tetrahedra
  MatrixXi TT;
  // New surface faces FF
  MatrixXi FF;
  if(!mesh_with_skeleton(V,F,C,P,BE,CE,VV,TT,FF))
  {
    return 1;
  }
  // Compute boundary conditions (aka fixed value constraints)
  // List of boundary indices (aka fixed value indices into VV)
  VectorXi b;
  // List of boundary conditions of each weight function
  MatrixXd bc;
  if(!boundary_conditions(VV,TT,C,P,BE,CE,b,bc))
  {
    return 1;
  }

  cout<<"b=["<<b<<"];"<<endl;
  cout<<"bc=["<<bc<<"];"<<endl;

  // compute BBW 
  // Default bbw data and flags
  BBWData bbw_data;
  // Weights matrix
  MatrixXd W;
  if(!bbw(VV,TT,b,bc,bbw_data,W))
  {
    return 1;
  }
  // Save output
  save_output(argv[1],argv[2],V,F,VV,TT,FF,W);
  return 0;
}
