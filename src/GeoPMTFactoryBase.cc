#include <ButtonDetectorFactory.hh>
#include <GeoPMTFactoryBase.hh>
#include <RAT/DB.hh>
#include <RAT/Log.hh>

#include <CLHEP/Units/PhysicalConstants.h>
#include <CLHEP/Units/SystemOfUnits.h>
#include <G4PVPlacement.hh>
#include <G4SDManager.hh>

#include <G4LogicalBorderSurface.hh>
#include <G4Tubs.hh>
#include <RAT/DetectorConstruction.hh>
#include <RAT/Factory.hh>
#include <RAT/GLG4PMTSD.hh>
#include <RAT/Materials.hh>
#include <RAT/ToroidalPMTConstruction.hh>
#include <RAT/WaveguideFactory.hh>
#include <algorithm>
#include <vector>

#include "G4FastSimulationManager.hh"
#include "G4LogicalSkinSurface.hh"
#include "G4PhysicsOrderedFreeVector.hh"
#include "G4VFastSimulationModel.hh"
#include "RAT/GLG4PMTOpticalModel.hh"

#include "G4RandomDirection.hh"

#include "iostream"

#include <G4GenericPolycone.hh>
#include <G4Paraboloid.hh>
#include <G4Sphere.hh>
#include <G4SubtractionSolid.hh>
#include <G4UnionSolid.hh>
#include <G4VisAttributes.hh>

using namespace std;

namespace BUTTON {

RAT::DS::PMTInfo GeoPMTFactoryBase::pmtinfo;

G4VPhysicalVolume *GeoPMTFactoryBase::ConstructPMTs(RAT::DBLinkPtr table,
                                                    std::vector<double> pmt_x,
                                                    std::vector<double> pmt_y,
                                                    std::vector<double> pmt_z) {
  // ------------------ Extra PMT settings --------------------
  string volume_name = table->GetIndex();
  string sensitive_detector_name = table->GetS("sensitive_detector");

  int start_idx, end_idx;
  try {
    start_idx = table->GetI(
        "start_idx"); // position in this array to start building pmts
  } catch (DBNotFoundError &e) {
    start_idx = 0; // defaults to beginning
  }
  try {
    end_idx =
        table->GetI("end_idx"); // id of the last pmt to build in this array
  } catch (DBNotFoundError &e) {
    end_idx = pmt_x.size() - 1; // defaults to whole array
  }

  string pos_table_name = table->GetS("pos_table");
  RAT::DBLinkPtr lpos_table = DB::Get()->GetLink(pos_table_name);

  vector<int> pmt_type;
  try {
    pmt_type =
        lpos_table->GetIArray("type"); // functional type (e.g. inner, veto,
                                       // etc. - arbitrary integers)
  } catch (DBNotFoundError &e) {
    pmt_type.resize(pmt_x.size());
    fill(pmt_type.begin(), pmt_type.end(),
         -1); // defaults to type -1 if unspecified
  }

  // read pmt_detector_type
  string pmt_detector_type = table->GetS("pmt_detector_type");

  // flip pmts to face outwards e.g. for use in a veto
  int flip = 0;
  try {
    flip = table->GetI("flip");
  } catch (DBNotFoundError &e) {
  }

  // Find mother
  string mother_name = table->GetS("mother");
  G4LogicalVolume *mother = FindMother(mother_name);
  if (mother == 0)
    Log::Die("Unable to find mother volume " + mother_name + " for " +
             volume_name);

  string pmt_model = table->GetS(
      "pmt_model"); // the form factor of the PMT (physical properties)
  RAT::DBLinkPtr lpmt = DB::Get()->GetLink("PMT", pmt_model);

  // add mu metal shields
  int mu_metal = 0; // default to no shields
  try {
    mu_metal = table->GetI("mu_metal");
  } catch (DBNotFoundError &e) {
  }
  // Material Properties
  G4Material *mu_metal_material = G4Material::GetMaterial("aluminum");
  try {
    mu_metal_material =
        G4Material::GetMaterial(table->GetS("mu_metal_material"));
  } catch (DBNotFoundError &e) {
  }
  // Surface Properties
  G4SurfaceProperty *mu_metal_surface = Materials::optical_surface["aluminum"];
  try {
    mu_metal_surface =
        Materials::optical_surface[table->GetS("mu_metal_surface")];
  } catch (DBNotFoundError &e) {
  }
  G4cout << "Mu metal shield is added!! \n ";

  G4Tubs *mumetal_solid = new G4Tubs("mumetal_solid",
                                     13.0 * CLHEP::cm, // rmin
                                     13.2 * CLHEP::cm, // rmax
                                     10.0 * CLHEP::cm, // z
                                     0., CLHEP::twopi);
  G4LogicalVolume *mumetal_log =
      new G4LogicalVolume(mumetal_solid,     // G4VSolid
                          mu_metal_material, // G4Material
                          "mumetal_log");
  G4LogicalSkinSurface *mumetal_skin =
      new G4LogicalSkinSurface("mumetal_surface",
                               mumetal_log,       // Logical Volume
                               mu_metal_surface); // Surface Property

  // add PMT encapsulation : diameter=40cm
  int encapsulation = 1; // default to encapsulation
  try {
    encapsulation = table->GetI("encapsulation");
  } catch (DBNotFoundError &e) {
  }
  if (encapsulation == 1) {
    G4cout << "Your PMTs are inside an encapsulation! \n ";
  }
  // Material Properties
  G4Material *front_encapsulation_material =
      G4Material::GetMaterial("nakano_acrylic"); // water");//nakano_acrylic");
  try {
    front_encapsulation_material =
        G4Material::GetMaterial(table->GetS("front_encapsulation_material"));
  } catch (DBNotFoundError &e) {
  }
  G4Material *rear_encapsulation_material = G4Material::GetMaterial(
      "acrylic_black"); // polypropylene");//acrylic_black");
  try {
    rear_encapsulation_material =
        G4Material::GetMaterial(table->GetS("rear_encapsulation_material"));
  } catch (DBNotFoundError &e) {
  }
  G4Material *metal_flange_material =
      G4Material::GetMaterial("stainless_steel");
  try {
    metal_flange_material =
        G4Material::GetMaterial(table->GetS("metal_flange_material"));
  } catch (DBNotFoundError &e) {
  }
  G4Material *acrylic_flange_material =
      G4Material::GetMaterial("nakano_acrylic"); // water");//"nakano_acrylic");
  try {
    acrylic_flange_material =
        G4Material::GetMaterial(table->GetS("acrylic_flange_material"));
  } catch (DBNotFoundError &e) {
  }
  // Surface Properties
  G4SurfaceProperty *front_encapsulation_surface = Materials::optical_surface
      ["nakano_acrylic"]; // water"];//"nakano_acrylic"];
  try {
    front_encapsulation_surface =
        Materials::optical_surface[table->GetS("encapsulation_surface")];
  } catch (DBNotFoundError &e) {
  }
  G4SurfaceProperty *rear_encapsulation_surface = Materials::optical_surface
      ["acrylic_black"]; // polypropylene"];//acrylic_black"];
  try {
    rear_encapsulation_surface =
        Materials::optical_surface[table->GetS("encapsulation_surface")];
  } catch (DBNotFoundError &e) {
  }
  G4SurfaceProperty *metal_flange_surface =
      Materials::optical_surface["stainless_steel"];
  try {
    metal_flange_surface =
        Materials::optical_surface[table->GetS("metal_flange_surface")];
  } catch (DBNotFoundError &e) {
  }
  G4SurfaceProperty *acrylic_flange_surface = Materials::optical_surface
      ["nakano_acrylic"]; // water"];//nakano_acrylic"];
  try {
    acrylic_flange_surface =
        Materials::optical_surface[table->GetS("acrylic_flange_surface")];
  } catch (DBNotFoundError &e) {
  }
  G4cout << "PMT encapsulation is added!! \n ";
  double enc_radius = 20.0; // default radius
  try {
    enc_radius = table->GetD("enc_radius");
  } catch (DBNotFoundError &e) {
  }
  double enc_thickness = 0.8; // 8mm encapsulation thickness
  try {
    enc_thickness = table->GetD("enc_thickness");
  } catch (DBNotFoundError &e) {
  }

  // The default inner encapsulation diameter is: 40cm
  // front and back perpendicular to the PMT direction
  G4Sphere *front_encapsulation_solid =
      new G4Sphere("front_encapsulation_solid",
                   (enc_radius)*CLHEP::cm,                   // rmin 20 cm
                   (enc_radius + enc_thickness) * CLHEP::cm, // rmax: 20.8 cm
                   0.5 * CLHEP::pi, CLHEP::twopi,            // phi
                   0., 0.5 * CLHEP::pi);                     // theta
  G4LogicalVolume *front_encapsulation_log =
      new G4LogicalVolume(front_encapsulation_solid,    // G4VSolid
                          front_encapsulation_material, // G4Material
                          "front_encapsulation_log");

  G4Sphere *rear_encapsulation_solid =
      new G4Sphere("rear_encapsulation_solid",
                   (enc_radius)*CLHEP::cm,                   // rmin 20 cm
                   (enc_radius + enc_thickness) * CLHEP::cm, // rmax: 20.8 cm
                   0.5 * CLHEP::pi, CLHEP::twopi,            // phi
                   0.5 * CLHEP::pi, 0.5 * CLHEP::pi);        // theta
  G4LogicalVolume *rear_encapsulation_log =
      new G4LogicalVolume(rear_encapsulation_solid,    // G4VSolid
                          rear_encapsulation_material, // G4Material
                          "rear_encapsulation_log");

  G4Tubs *front_metal_flange_solid = new G4Tubs("front_metal_flange_solid",
                                                21.0 * CLHEP::cm, // rmin
                                                25.3 * CLHEP::cm, // rmax
                                                0.4 * CLHEP::cm,  // size z
                                                0, CLHEP::twopi); // phi
  G4LogicalVolume *front_metal_flange_log =
      new G4LogicalVolume(front_metal_flange_solid, // G4VSolid
                          metal_flange_material,    // G4Material
                          "front_metal_flange_log");

  G4Tubs *rear_metal_flange_solid = new G4Tubs("rear_metal_flange_solid",
                                               21.0 * CLHEP::cm, // rmin
                                               25.3 * CLHEP::cm, // rmax
                                               0.4 * CLHEP::cm,  // size z
                                               0, CLHEP::twopi); // phi
  G4LogicalVolume *rear_metal_flange_log =
      new G4LogicalVolume(rear_metal_flange_solid, // G4VSolid
                          metal_flange_material,   // G4Material
                          "rear_metal_flange_log");

  G4Tubs *acrylic_flange_solid = new G4Tubs("acrylic_flange_solid",
                                            20.8 * CLHEP::cm, // rmin
                                            25.3 * CLHEP::cm, // rmax
                                            0.8 * CLHEP::cm,  // size z
                                            0, CLHEP::twopi); // phi
  G4LogicalVolume *acrylic_flange_log =
      new G4LogicalVolume(acrylic_flange_solid,    // G4VSolid
                          acrylic_flange_material, // G4Material
                          "acrylic_flange_log");

  //----- add gel and air inside the encapsulation:
  // Note: [0.,CLHEP::pi/2.] is the upper hemisphere & [CLHEP::pi/2., CLHEP::pi]
  // is the lower hemisphere of the sphere
  //____ optical grease ______ //V-788 Optical Grease from Rhodia Silicones,
  // upper (front) hemisphere with a layer of optical grease

  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

  /// add temp pmt geomentry outside
  double offset1 = 0; //+50.55; 44.44 40
  G4double zF[] = {136.32 + offset1, 134.42 + offset1, 128.74 + offset1,
                   120.77 + offset1, 110.53 + offset1, 97.26 + offset1,
                   83.60 + offset1,  71.47 + offset1,  58.57 + offset1,
                   42.64 + offset1,  42.64 + offset1}; // 11
  G4double rF[] = {0.00,   20.39,  41.16,  59.66,  76.28, 91.38,
                   103.84, 115.55, 123.48, 125.74, 0};

  G4double zB[] = {
      42.64 + offset1,   42.64 + offset1,   23.30 + offset1,
      9.65 + offset1,    2.06 + offset1,    -26.76 + offset1,
      -35.10 + offset1,  -48.00 + offset1,  -96.16 + offset1,
      -105.65 + offset1, -108.30 + offset1, -108.30 + offset1}; // 12
  G4double rB[] = {0,     125.74, 121.59, 112.15, 101.96, 53.24,
                   44.18, 41.91,  41.91,  38.14,  29.45,  0.00};

  G4VSolid *pmt_front_surface_temp =
      new G4GenericPolycone("PMT assembly", 0, 360. * CLHEP::deg, 11, rF, zF);
  // auto frontFull_LV = new G4LogicalVolume(frontFull_SV,air,"PMTLogical");
  G4VSolid *pmt_back_surface_temp =
      new G4GenericPolycone("PMT assembly", 0, 360. * CLHEP::deg, 12, rB, zB);

  G4ThreeVector zTrans(0, 0, 0 * CLHEP::cm);

  G4VSolid *pmt_surface_temp = new G4UnionSolid(
      "pmt_surface_temp", pmt_front_surface_temp, pmt_back_surface_temp);

  /// remove next 3 lines and .... to remove temp surface
  // G4SurfaceProperty* pmt_surface_surface = Materials::optical_surface["air"];
  //  pmt_surface_surface = Materials::optical_surface[
  //  table->GetS("pmt_surface1")];

  G4Material *encapsulation_innermaterial1 =
      G4Material::GetMaterial("optical_grease");
  try {
    encapsulation_innermaterial1 =
        G4Material::GetMaterial(table->GetS("encapsulation_innermaterial1"));
  } catch (DBNotFoundError &e) {
  }
  G4SurfaceProperty *encapsulation_surface1 =
      Materials::optical_surface["optical_grease"];
  try {
    encapsulation_surface1 =
        Materials::optical_surface[table->GetS("encapsulation_surface1")];
  } catch (DBNotFoundError &e) {
  }

  /*G4LogicalVolume* pmt_surface_temp_log=new G4LogicalVolume(
                                                                pmt_surface_temp,
// G4VSolid encapsulation_innermaterial1,                 // G4Material
                "pmt_surface_temp_log");
//G4LogicalVolume*  pmt_surface_temp_log = new G4LogicalVolume(pmt_surface_temp,
"aluminum", "pmt_temp_surface");*/

  //....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo......

  /// add subtracted grease layer geomentry

  /*  G4Material* encapsulation_innermaterial1 =
    G4Material::GetMaterial("optical_grease"); try {
    encapsulation_innermaterial1 = G4Material::GetMaterial(
    table->GetS("encapsulation_innermaterial1") ); } catch (DBNotFoundError &e)
    { } G4SurfaceProperty* encapsulation_surface1 =
    Materials::optical_surface["optical_grease"]; try { encapsulation_surface1 =
    Materials::optical_surface[ table->GetS("encapsulation_surface1") ]; } catch
    (DBNotFoundError &e) { }
*/
  G4VSolid *encapsulation_innersolid1 = new G4Sphere(
      "encapsulation_innersolid1",
      //  G4VSolid* encapsulation_grease = new G4Sphere("encapsulation_grease",
      (enc_radius - enc_thickness) * CLHEP::cm, // rmin????   changed   -2.3 -10
      (enc_radius - 0.1) * CLHEP::cm,           // rmax???
      0.5 * CLHEP::pi, CLHEP::twopi,            // phi
      0., 0.5 * CLHEP::pi);                     // theta

  // G4VSolid* encapsulation_innersolid1 = new
  // G4Sphere("encapsulation_innersolid1",
  //            						       (enc_radius-enc_thickness)*CLHEP::cm,
  //            // rmin????   changed   -2.3
  //          						       (enc_radius-0.1)*CLHEP::cm,
  //          // rmax???
  //        						       0.5*CLHEP::pi,
  //        CLHEP::twopi,
  //        //phi
  //      						       0.,
  //      0.5*CLHEP::pi);
  //      //theta

  // G4VSolid* encapsulation_innersolid1 = new
  // G4SubtractionSolid("encapsulation_innersolid1", encapsulation_grease,
  // pmt_surface_temp);

  //  G4VSolid* encapsulation_innersolid1 = new
  //  G4UnionSolid("encapsulation_innersolid1",pmt_front_surface_temp,
  //  encapsulation_grease);

  G4LogicalVolume *encapsulation_innerlog1 =
      new G4LogicalVolume(encapsulation_innersolid1,    // G4VSolid
                          encapsulation_innermaterial1, // G4Material
                          "encapsulation_innerlog1");

  //_____ air _____
  // lower (rear) hemisphere filled with air
  G4Material *encapsulation_innermaterial2 =
      G4Material::GetMaterial("optical_grease"); //("air");
  try {
    encapsulation_innermaterial2 =
        G4Material::GetMaterial(table->GetS("encapsulation_innermaterial2"));
  } catch (DBNotFoundError &e) {
  }
  G4SurfaceProperty *encapsulation_surface2 =
      Materials::optical_surface["optical_grease"]; //("air");
  try {
    encapsulation_surface2 =
        Materials::optical_surface[table->GetS("encapsulation_surface2")];
  } catch (DBNotFoundError &e) {
  }

  G4VSolid *encapsulation_innersolid2 =
      new G4Sphere("encapsulation_innersolid2",
                   0 * CLHEP::cm,                            // rmin
                   (enc_radius - enc_thickness) * CLHEP::cm, // rmax
                   0.5 * CLHEP::pi, CLHEP::twopi,            // phi
                   0.5 * CLHEP::pi, 0.5 * CLHEP::pi);        // theta

  G4LogicalVolume *encapsulation_innerlog2 =
      new G4LogicalVolume(encapsulation_innersolid2,    // G4VSolid
                          encapsulation_innermaterial2, // G4Material
                          "encapsulation_innerlog2");

  // Add the encapsulation surfaces
  G4LogicalSkinSurface *front_encapsulation_skin =
      new G4LogicalSkinSurface("front_encapsulation_skin",
                               front_encapsulation_log,      // Logical Volume
                               front_encapsulation_surface); // Surface Property
  G4LogicalSkinSurface *rear_encapsulation_skin =
      new G4LogicalSkinSurface("rear_encapsulation_skin",
                               rear_encapsulation_log,      // Logical Volume
                               rear_encapsulation_surface); // Surface Property
  G4LogicalSkinSurface *front_metal_flange_skin =
      new G4LogicalSkinSurface("front_metal_flange_skin",
                               front_metal_flange_log, // Logical Volume
                               metal_flange_surface);  // Surface Property
  G4LogicalSkinSurface *rear_metal_flange_skin =
      new G4LogicalSkinSurface("rear_metal_flange_skin",
                               rear_metal_flange_log, // Logical Volume
                               metal_flange_surface); // Surface Property
  G4LogicalSkinSurface *acrylic_flange_skin =
      new G4LogicalSkinSurface("acrylic_flange_skin",
                               acrylic_flange_log,      // Logical Volume
                               acrylic_flange_surface); // Surface Property
  G4LogicalSkinSurface *encapsulation_skin1 =
      new G4LogicalSkinSurface("encapsulation_skin1",
                               encapsulation_innerlog1, // Logical Volume
                               encapsulation_surface1); // Surface Property
  /*  G4LogicalSkinSurface* encapsulation_pmt_surface = new
     G4LogicalSkinSurface( "encapsulation_pmt_surface", pmt_surface_temp_log,
     //Logical Volume encapsulation_surface1); //Surface Property*/
  G4LogicalSkinSurface *encapsulation_skin2 =
      new G4LogicalSkinSurface("encapsulation_skin2",
                               encapsulation_innerlog2, // Logical Volume
                               encapsulation_surface2); // Surface Property

  //----------------------------------------------------

  // add light cone
  int light_cone = table->GetI("light_cone");
  bool lightcones = false;
  if (light_cone == 1) {
    lightcones = true;
    G4cout << "Light cones are added!! \n ";
  }
  // material properties
  G4Material *light_cone_material = G4Material::GetMaterial("aluminum");
  try {
    light_cone_material =
        G4Material::GetMaterial(table->GetS("light_cone_material"));
  } catch (DBNotFoundError &e) {
  }
  // surface properties
  G4SurfaceProperty *light_cone_surface =
      Materials::optical_surface["aluminum"];
  try {
    light_cone_surface =
        Materials::optical_surface[table->GetS("light_cone_surface")];
  } catch (DBNotFoundError &e) {
  }
  G4cout << "Light cone is added!! \n ";
  // light cone parameter: dz
  double light_cone_length = 17.5;
  try {
    light_cone_length = table->GetD("light_cone_length");
  } catch (DBNotFoundError &e) {
  }
  // light cone parameter: inner radius
  double light_cone_innerradius = 12.65;
  try {
    light_cone_innerradius = table->GetD("light_cone_innerradius");
  } catch (DBNotFoundError &e) {
  }
  // light cone parameter: outer radius
  double light_cone_outerradius = 21;
  try {
    light_cone_outerradius = table->GetD("light_cone_outerradius");
  } catch (DBNotFoundError &e) {
  }
  // light cone parameter: thickness
  double light_cone_thickness = 0.2;
  try {
    light_cone_thickness = table->GetD("light_cone_thickness");
  } catch (DBNotFoundError &e) {
  }

  // Add Light cone geometry from Sheffield
  G4Paraboloid *lightcone_outer = new G4Paraboloid(
      "lightcone_outer", light_cone_length * CLHEP::cm,
      light_cone_innerradius * CLHEP::cm, light_cone_outerradius * CLHEP::cm);
  G4Paraboloid *lightcone_inner = new G4Paraboloid(
      "lightcone_inner", (light_cone_length + 0.2) * CLHEP::cm,
      (light_cone_innerradius - light_cone_thickness) * CLHEP::cm,
      (light_cone_outerradius - light_cone_thickness) * CLHEP::cm);
  G4SubtractionSolid *lightcone_solid = new G4SubtractionSolid(
      "lightcone_solid", lightcone_outer, lightcone_inner);

  G4LogicalVolume *lightcone_log = new G4LogicalVolume(
      lightcone_solid, light_cone_material, "lightcone_log");
  G4LogicalSkinSurface *lightcone_skin = new G4LogicalSkinSurface(
      "lightcone_surface", lightcone_log, light_cone_surface);

  ToroidalPMTConstructionParams pmtParam;
  pmtParam.faceGap = 0.1 * CLHEP::mm;
  pmtParam.zEdge = lpmt->GetDArray("z_edge");
  pmtParam.rhoEdge = lpmt->GetDArray("rho_edge");
  pmtParam.zOrigin = lpmt->GetDArray("z_origin");
  pmtParam.dynodeRadius = lpmt->GetD("dynode_radius");
  pmtParam.dynodeTop = lpmt->GetD("dynode_top");
  pmtParam.wallThickness = lpmt->GetD("wall_thickness");
  pmtParam.photocathode_MINrho = lpmt->GetD("photocathode_MINrho");
  pmtParam.photocathode_MAXrho = lpmt->GetD("photocathode_MAXrho");
  try {
    pmtParam.prepulseProb = lpmt->GetD("prepulse_prob");
  } catch (DBNotFoundError &e) {
  }

  // Materials
  pmtParam.exterior = mother->GetMaterial();
  pmtParam.glass = G4Material::GetMaterial(lpmt->GetS("glass_material"));
  pmtParam.dynode = G4Material::GetMaterial(lpmt->GetS("dynode_material"));
  pmtParam.vacuum = G4Material::GetMaterial(lpmt->GetS("pmt_vacuum_material"));
  string pc_surface_name = lpmt->GetS("photocathode_surface");
  pmtParam.photocathode = Materials::optical_surface[pc_surface_name];
  string mirror_surface_name = lpmt->GetS("mirror_surface");
  pmtParam.mirror = Materials::optical_surface[mirror_surface_name];
  pmtParam.dynode_surface =
      Materials::optical_surface[lpmt->GetS("dynode_surface")];

  if (pmtParam.photocathode == 0)
    Log::Die("GeoPMTFactoryBase error: Photocathode surface \"" +
             pc_surface_name + "\" not found");

  // Simplified PMT drawing for faster visualization
  bool vis_simple = false;
  try {
    vis_simple = table->GetI("vis_simple") != 0;
  } catch (DBNotFoundError &e) {
  }

  // Orientation of PMTs
  bool orient_manual = false;
  try {
    string orient_str = table->GetS("orientation");
    if (orient_str == "manual")
      orient_manual = true;
    else if (orient_str == "point")
      orient_manual = false;
    else
      Log::Die("GeoPMTFactoryBase error: Unknown PMT orientation " +
               orient_str);
  } catch (DBNotFoundError &e) {
  }

  vector<double> dir_x, dir_y, dir_z;
  vector<double> orient_point_array;
  G4ThreeVector orient_point;
  bool rescale_radius = false;
  double new_radius = 1.0;
  if (orient_manual) {
    dir_x = lpos_table->GetDArray("dir_x");
    dir_y = lpos_table->GetDArray("dir_y");
    dir_z = lpos_table->GetDArray("dir_z");
  } else {
    // fill with dummy values needed for pmtinfo.AddPMT. They will be redirected
    // towards the proper point afterwards
    dir_x.push_back(9999.);
    dir_y.push_back(9999.);
    dir_z.push_back(9999.);
    orient_point_array = table->GetDArray("orient_point");
    if (orient_point_array.size() != 3)
      Log::Die("GeoPMTFactoryBase error: orient_point must have 3 values");
    orient_point.set(orient_point_array[0], orient_point_array[1],
                     orient_point_array[2]);
  }

  // Optionally can rescale PMT radius from mother volume center for
  // case where PMTs have spherical layout symmetry
  try {
    new_radius = table->GetD("rescale_radius");
    rescale_radius = true;
  } catch (DBNotFoundError &e) {
  }

  // get pointer to physical mother volume
  G4VPhysicalVolume *phys_mother = FindPhysMother(mother_name);
  if (phys_mother == 0)
    Log::Die("GeoPMTFactoryBase error: PMT mother physical volume " +
             mother_name + " not found");

  // --------------- Start building PMT geometry ------------------

  // PMT sensitive detector
  G4SDManager *fSDman = G4SDManager::GetSDMpointer();
  GLG4PMTSD *pmtSDInner =
      new GLG4PMTSD(sensitive_detector_name, end_idx - start_idx + 1,
                    pmtinfo.GetPMTCount(), -1 /* evidently unused? */);
  fSDman->AddNewDetector(pmtSDInner);
  pmtParam.detector = pmtSDInner;

  // Setup for waveguide
  WaveguideFactory *waveguide_factory = 0;
  try {
    string waveguide = table->GetS("waveguide");
    string waveguide_desc = table->GetS("waveguide_desc");
    string waveguide_table, waveguide_index;
    if (!DB::ParseTableName(waveguide_desc, waveguide_table, waveguide_index))
      Log::Die("GeoPMTFactoryBase: Waveguide descriptor name is not a valid "
               "RATDB table: " +
               waveguide_desc);

    waveguide_factory = GlobalFactory<WaveguideFactory>::New(waveguide);
    waveguide_factory->SetTable(waveguide_table, waveguide_index);
    pmtParam.faceGap = waveguide_factory->GetZTop();
    pmtParam.minEnvelopeRadius = waveguide_factory->GetRadius();
  } catch (DBNotFoundError &e) {
  }

  // Set new efficiency correction if requested
  try {
    float efficiency_correction = table->GetD("efficiency_correction");
    pmtParam.efficiencyCorrection = efficiency_correction;
  } catch (DBNotFoundError &e) {
  }

  // Build PMT
  pmtParam.useEnvelope = true; // enable the use of envelope volume for now (not
                               // used in standard rat-pac)
  ToroidalPMTConstruction pmtConstruct(pmtParam);

  G4LogicalVolume *logiPMT = pmtConstruct.NewPMT(volume_name, vis_simple);
  G4LogicalVolume *logiWg = 0;
  G4ThreeVector offsetWg;

  // G4VPhysicalVolume *mid_water_phys = FindPhysMother("mid_water");
  // new G4PVPlacement
  //( 0,
  //   G4ThreeVector(0.0, 0.0, /*-10.0*/64.0*CLHEP::cm),
  //   "mumetal_phys",
  //   mumetal_log,
  //   mid_water_phys,
  //   false,
  //   0 );

  // Add waveguide if needed
  if (waveguide_factory) {
    waveguide_factory->SetPMTBodySolid(
        pmtConstruct.NewBodySolid(volume_name + "_waveguide_sub"));
    logiWg = waveguide_factory->Construct(volume_name + "_waveguide_log",
                                          logiPMT, vis_simple);
    offsetWg = waveguide_factory->GetPlacementOffset();
    if (pmtParam.useEnvelope) {
      new G4PVPlacement(
          0, // no rotation
          offsetWg,
          logiWg,                          // the logical volume
          volume_name + "_waveguide_phys", // a name for this physical volume
          logiPMT,                         // the mother volume
          false,                           // no boolean ops
          0);                              // copy number
    }
  }

  // preparing to calculate magnetic efficiency corrections for all PMTs, if
  // requested
  int BFieldOn = 0;
  try {
    BFieldOn = DB::Get()->GetLink("BField")->GetI("b_field_on");
  } catch (DBNotFoundError &e) {
  }
  string BFieldTableName = "";
  string BEffiTableName = "";
  string dynorfilename = "";
  string BEffiModel = "multiplicative";
  vector<pair<int, double>> BEfficiencyCorrection;
  RAT::DBLinkPtr BEffiTable;
  //  G4PhysicsOrderedFreeVector Bepsix,Bepsiy;
  vector<G4PhysicsOrderedFreeVector> Bepsix, Bepsiy;
  vector<G4ThreeVector> Bpos, Bf;
  vector<G4ThreeVector> Dpos, Dorie; // dynode position&orient

  // Force B efficiency<= 1 by default, whatever the input from BEffiTable.
  // If we start believing that B may actually help the PMT response, change
  // default to false
  bool CorrBEpsiInput = true;
  int nocorr = 0;
  try {
    nocorr =
        DB::Get()->GetLink("BField")->GetI("no_b_efficiency_table_correction");
  } catch (DBNotFoundError &e) {
  }
  if (nocorr)
    CorrBEpsiInput = false;
  else
    cout << "Forcing B efficiency<= 1\n";
  bool HaveDynoData = false;

  if (BFieldOn) {
    try {
      BFieldTableName = DB::Get()->GetLink("BField")->GetS("b_field_file");
    } catch (DBNotFoundError &e) {
    }
    try {
      BEffiTableName = DB::Get()->GetLink("BField")->GetS("b_efficiency_table");
    } catch (DBNotFoundError &e) {
    }
    try {
      dynorfilename = DB::Get()->GetLink("dynorfile")->GetS("dynorfilename");
    } catch (DBNotFoundError &e) {
    }
    // check if we can calculate B field effect
    if (BFieldTableName == "" || BEffiTableName == "") {
      G4cout << "B field is on, but either B data or B PMT efficiency "
                "correction missing.\n"
             << "Turning B field off.\n";
      BFieldOn = 0;
      BEffiTable = NULL;
    } else {
      string ExpSubdir = DB::Get()->GetLink("DETECTOR")->GetS("experiment");
      string BFieldTableName1 = string(getenv("GLG4DATA")) + "/" + ExpSubdir +
                                "/" +
                                BFieldTableName; // add the experiment subdir
      ifstream Bdata(BFieldTableName1.data());
      if (!Bdata.is_open()) {
        BFieldTableName = string(getenv("GLG4DATA")) + "/" + BFieldTableName;
        cout << "file " << BFieldTableName1 << " not found, trying "
             << BFieldTableName << "\n";
        Bdata.close();
        Bdata.open(BFieldTableName.data());
        if (!Bdata.is_open()) {
          BFieldOn = false;
          BEffiTable = NULL;
          cout << "also file " << BFieldTableName
               << " not found, magnetic efficiency correction turned off\n";
        }
        if (!Bdata.good())
          Bdata.clear(); // for backwards compatibility: g++ 3.4 requires to
                         // manually reset the error state flags on opening a
                         // new file with the same stream
      }
      string header;
      getline(Bdata, header);
      double xr, yr, zr, bxr, byr, bzr;
      cout << "about to load B field from file " << BFieldTableName << "\n";
      G4ThreeVector posi, field;
      while (!Bdata.rdstate()) {
        Bdata >> xr >> yr >> zr >> bxr >> byr >> bzr;
        posi.set(xr, yr, zr);
        field.set(bxr, byr, bzr);
        Bpos.push_back(posi);
        Bf.push_back(field);
      }
      BEffiTable = DB::Get()->GetLink(BEffiTableName);
      vector<double> bpmt;
      vector<double> epsix, epsiy;
      bpmt = BEffiTable->GetDArray("b");
      epsix = BEffiTable->GetDArray("deltax");
      epsiy = BEffiTable->GetDArray("deltay");
      G4PhysicsOrderedFreeVector *QBepsix;
      G4PhysicsOrderedFreeVector *QBepsiy;
      QBepsix = new G4PhysicsOrderedFreeVector();
      QBepsiy = new G4PhysicsOrderedFreeVector();
      for (int i = 0; i < int(bpmt.size()); i++) {
        QBepsix->InsertValues(bpmt[i], epsix[i]);
        QBepsiy->InsertValues(bpmt[i], epsiy[i]);
      }
      Bepsix.push_back(*QBepsix);
      Bepsiy.push_back(*QBepsiy);
      // no. of datasheets transcribed in the db table of the same PMT model
      int nsheets = 0;
      try {
        nsheets = BEffiTable->GetI("nsheets");
      } catch (DBNotFoundError &e) {
      }
      if (nsheets > 1) {
        char c[12];
        string name;
        for (int is = 1; is < nsheets; is++) {
          sprintf(c, "%i", is);
          name = string("deltax") + c;
          epsix = BEffiTable->GetDArray(name);
          name = string("deltay") + c;
          epsiy = BEffiTable->GetDArray(name);
          delete QBepsix;
          delete QBepsiy;
          QBepsix = new G4PhysicsOrderedFreeVector();
          QBepsiy = new G4PhysicsOrderedFreeVector();
          for (int i = 0; i < int(bpmt.size()); i++) {
            QBepsix->InsertValues(bpmt[i], epsix[i]);
            QBepsiy->InsertValues(bpmt[i], epsiy[i]);
          }
          Bepsix.push_back(*QBepsix);
          Bepsiy.push_back(*QBepsiy);
        }
      }

      // try to load PMT orientation table from file
      dynorfilename =
          string(getenv("GLG4DATA")) + "/" + ExpSubdir + "/" + dynorfilename;
      ifstream dynorfile(dynorfilename.data());
      if (!dynorfile.is_open()) {
        cout << "Failed to open " << dynorfilename.data()
             << ", will assume random dynode orientations\n";
        dynorfile.close();
      } else {
        getline(dynorfile, header);
        double xd, yd, zd, dynox, dynoy, dynoz;
        G4ThreeVector dor;
        while (!dynorfile.rdstate()) {
          dynorfile >> xd >> yd >> zd >> dynox >> dynoy >> dynoz;
          posi.set(xd, yd, zd);
          dor.set(dynox, dynoy, dynoz);
          Dpos.push_back(posi);
          Dorie.push_back(dor);
        }
      }
      Bdata.close();
      dynorfile.close();
      if (!Dorie.empty() && Dorie.size() == Dpos.size())
        HaveDynoData = true;
      else
        cout << "No dynode orientation datafile or error in the data, "
                "randomizing dynode orientations\n";
    }
    try {
      BEffiModel = DB::Get()->GetLink("BField")->GetS("b_efficiency_model");
    } catch (DBNotFoundError &e) {
    }
    cout << "\nSelected " << BEffiModel.data() << " B Efficiency Model\n";
  } else
    BEffiTable = NULL;

  bool parent_coord = false;
  try {
    parent_coord = table->GetI("use_parent_coordinates") != 0;
  } catch (DBNotFoundError &e) {
  }

  // PMTINFO is always in global coordinates - so calculate the local offset
  // first
  G4ThreeVector offset = G4ThreeVector(0.0, 0.0, 0.0);
  for (string parent_name = mother_name; parent_name != "";) {
    G4cout << "parent_name is " << parent_name << "\n";
    G4VPhysicalVolume *parent_phys = FindPhysMother(parent_name);
    offset += parent_phys->GetFrameTranslation();
    RAT::DBLinkPtr parent_table = DB::Get()->GetLink("GEO", parent_name);
    parent_name = parent_table->GetS("mother");
  }

  // Place physical PMTs
  // idx - the element of the particular set of arrays we are reading
  // id - the nth pmt that GeoPMTFactoryBase has built
  for (int idx = start_idx, id = pmtinfo.GetPMTCount(); idx <= end_idx;
       idx++, id++) {

    string pmtname = volume_name + "_pmtenv_" +
                     ::to_string(id); // internally PMTs are represented by the
                                      // nth pmt built, not pmtid

    // position
    G4ThreeVector pmtpos(pmt_x[idx], pmt_y[idx], pmt_z[idx]);
    if (!parent_coord)
      pmtpos += offset;
    if (rescale_radius)
      pmtpos.setMag(new_radius);

    // direction
    G4ThreeVector pmtdir;
    if (orient_manual)
      pmtdir.set(dir_x[idx], dir_y[idx], dir_z[idx]);
    else
      pmtdir = orient_point - pmtpos;
    pmtdir = pmtdir.unit();
    if (flip == 1)
      pmtdir = -pmtdir;

    // Write the real (perhaps calculated) PMT positions and directions.
    // This goes into the DS by way of Gsim
    pmtinfo.AddPMT(TVector3(pmtpos.x(), pmtpos.y(), pmtpos.z()),
                   TVector3(pmtdir.x(), pmtdir.y(), pmtdir.z()), pmt_type[idx],
                   pmt_model);

    // if requested, generates the magnetic efficiency corrections as the PMTs
    // are created
    if (BFieldOn) {
      // finds the point of the B grid closest to the current PMT, and
      // attributes it that Bfield
      double MinDist = DBL_MAX;
      int imin = -1;
      for (int i = 0; i < int(Bpos.size()); i++) {
        if (MinDist > (pmtpos - Bpos[i]).mag()) {
          MinDist = (pmtpos - Bpos[i]).mag();
          imin = i;
        }
      }
      if (imin < 0)
        cout << "can't find a point close to the " << id
             << "-th pmt; MinDist is " << MinDist << "\n";
      else {
        G4ThreeVector bfield = Bf[imin].perpPart(pmtdir);
        G4ThreeVector dynorient;
        if (HaveDynoData) {
          int mini = -1;
          double MinDiff = DBL_MAX;
          for (int i = 0; i < int(Dorie.size()); i++)
            if ((pmtpos.unit() - Dpos[i].unit()).mag() < MinDiff) {
              MinDiff = (pmtpos.unit() - Dpos[i].unit()).mag();
              mini = i;
            }
          if (mini < 0) {
            cout << "can't find the orientation of the " << id
                 << "-th pmt's dynode; MinDiff is " << MinDiff << "\n"
                 << "Throwing a random dynode orientation\n";
            dynorient = G4RandomDirection();
            dynorient = dynorient.perpPart(pmtdir);
          } else
            dynorient = Dorie[mini];
        } else {
          // random dynode orientation
          dynorient = G4RandomDirection();
          // dynode orthogonal to PMT axis
          dynorient = dynorient.perpPart(pmtdir);
        }
        if (dynorient.mag() == 0) {
          for (int di = 0; di < 100 && dynorient.mag() == 0; di++) {
            dynorient = G4RandomDirection();
            dynorient = dynorient.perpPart(pmtdir);
          }
          if (dynorient.mag() == 0)
            cout << "Warning: tried 100 times to generate a random dynode "
                    "orientation for "
                 << id << "-th PMT and failed. dynorient " << dynorient(0)
                 << "," << dynorient(1) << "," << dynorient(2) << "\n";
        }
        dynorient = dynorient.unit();
        // build BEfficiencyCorrection table. PMT x axis is dynorient
        bool isOutRange; // just a flag, not really used in the G4 code
        double BeffiComp;
        int sheetno = 0;
        if (Bepsix.size() > 1) {
          double chooser = G4UniformRand();
          if (chooser < 1. / 3)
            sheetno = 0;
          else if (chooser < 2. / 3)
            sheetno = 1;
          else
            sheetno = 2;
        }
        if (BEffiModel == "multiplicative") {
          BeffiComp =
              Bepsix[sheetno].GetValue(bfield * dynorient, isOutRange) *
              Bepsiy[sheetno].GetValue(
                  bfield * (pmtdir.cross(dynorient)).unit(), isOutRange);
          if (CorrBEpsiInput && BeffiComp > 1)
            BEfficiencyCorrection.push_back(pair<int, double>(id, 1));
          else
            BEfficiencyCorrection.push_back(pair<int, double>(id, BeffiComp));
        } else if (BEffiModel == "additive") {
          BeffiComp =
              Bepsix[sheetno].GetValue(bfield * dynorient, isOutRange) +
              Bepsiy[sheetno].GetValue(
                  bfield * (pmtdir.cross(dynorient)).unit(), isOutRange) -
              1.;
          if (CorrBEpsiInput && BeffiComp > 1)
            BEfficiencyCorrection.push_back(pair<int, double>(id, 1));
          else
            BEfficiencyCorrection.push_back(pair<int, double>(id, BeffiComp));
        } else
          cout << "\nError: undefined B Efficiency Model\n";
      }
    }

    // rotation required to point in direction of pmtdir
    double angle_y = (-1.0) * atan2(pmtdir.x(), pmtdir.z());
    double angle_x = atan2(
        pmtdir.y(), sqrt(pmtdir.x() * pmtdir.x() + pmtdir.z() * pmtdir.z()));

    G4RotationMatrix *pmtrot = new G4RotationMatrix();
    pmtrot->rotateY(angle_y);
    pmtrot->rotateX(angle_x);
    // ****************************************************************
    // * Use the constructor that specifies the PHYSICAL mother, since
    // * each PMT occurs only once in one physical volume.  This saves
    // * the GeometryManager some work. -GHS.
    // ****************************************************************
    G4PVPlacement *thisPhysPMT = new G4PVPlacement(
        pmtrot, pmtpos, pmtname, logiPMT, phys_mother, false, id);
    if (!pmtParam.useEnvelope) {
      // If not using envelope volume, the PMT optical surfaces have NOT been
      // set and we must do so NOW.
      pmtConstruct.SetPMTOpticalSurfaces(thisPhysPMT, pmtname);
    }

    // place the mumetal shields
    G4ThreeVector offsetmumetal =
        G4ThreeVector(0.0, 0.0, /*-10.0*/ 0.0 * CLHEP::cm);
    // G4cout << "pmtpos is " << pmtpos << "\n";
    G4ThreeVector offsetmu_rot = pmtrot->inverse()(offsetmumetal);
    G4ThreeVector mumetalpos = pmtpos + offsetmu_rot;
    if (mu_metal) {
      new G4PVPlacement(pmtrot, mumetalpos, "mumetal_phys", mumetal_log,
                        phys_mother, false, id);
    }

    // place the encapsulation:

    G4ThreeVector offsetacrylicflange =
        G4ThreeVector(0.0, 0.0, -10.2 * CLHEP::cm); //-10.55
    G4ThreeVector offsetacrylicflange_rot =
        pmtrot->inverse()(offsetacrylicflange);
    G4ThreeVector acrylicflangepos = pmtpos + offsetacrylicflange_rot;

    G4ThreeVector offsetfrontencapsulation =
        G4ThreeVector(0.0, 0.0, 0.8 * CLHEP::cm);
    G4ThreeVector offsetfrontencapsulation_rot =
        pmtrot->inverse()(offsetfrontencapsulation);
    G4ThreeVector frontencapsulationpos =
        pmtpos + offsetacrylicflange_rot + offsetfrontencapsulation_rot;

    G4ThreeVector offsetrearencapsulation =
        G4ThreeVector(0.0, 0.0, -0.8 * CLHEP::cm);
    G4ThreeVector offsetrearencapsulation_rot =
        pmtrot->inverse()(offsetrearencapsulation);
    G4ThreeVector rearencapsulationpos =
        pmtpos + offsetacrylicflange_rot + offsetrearencapsulation_rot;

    G4ThreeVector offsetfrontmetalflange =
        G4ThreeVector(0.0, 0.0, 1.2 * CLHEP::cm);
    G4ThreeVector offsetfrontmetalflange_rot =
        pmtrot->inverse()(offsetfrontmetalflange);
    G4ThreeVector frontmetalflangepos =
        pmtpos + offsetacrylicflange_rot + offsetfrontmetalflange_rot;

    G4ThreeVector offsetrearmetalflange =
        G4ThreeVector(0.0, 0.0, -1.2 * CLHEP::cm);
    G4ThreeVector offsetrearmetalflange_rot =
        pmtrot->inverse()(offsetrearmetalflange);
    G4ThreeVector rearmetalflangepos =
        pmtpos + offsetacrylicflange_rot + offsetrearmetalflange_rot;

    if (encapsulation) {
      new G4PVPlacement(pmtrot, frontencapsulationpos, "encapsulation_phys",
                        front_encapsulation_log, phys_mother, false, id);

      new G4PVPlacement(pmtrot, rearencapsulationpos, "encapsulation_phys",
                        rear_encapsulation_log, phys_mother, false, id);

      new G4PVPlacement(pmtrot, frontencapsulationpos, "encapsulation_phys",
                        encapsulation_innerlog1, phys_mother, false, id);

      /*    new G4PVPlacement
        (pmtrot,
          frontencapsulationpos,
          "encapsulation_phys",
          pmt_surface_temp_log,
          phys_mother,
          false,
          id);*/

      new G4PVPlacement(pmtrot, rearencapsulationpos, "encapsulation_phys",
                        encapsulation_innerlog2, phys_mother, false, id);

      new G4PVPlacement(pmtrot, acrylicflangepos, "encapsulation_phys",
                        acrylic_flange_log, phys_mother, false, id);

      new G4PVPlacement(pmtrot, frontmetalflangepos, "encapsulation_phys",
                        front_metal_flange_log, phys_mother, false, id);

      new G4PVPlacement(pmtrot, rearmetalflangepos, "encapsulation_phys",
                        rear_metal_flange_log, phys_mother, false, id);
    }

    // add light cones if required
    G4RotationMatrix *lightconerot = new G4RotationMatrix();
    lightconerot->rotateY(angle_y);
    lightconerot->rotateX(angle_x);
    // place the mumetal shields
    G4ThreeVector offsetlightcone =
        /*G4ThreeVector(0.0, 0.0, 10.0*CLHEP::cm)*/ pmtdir * 9.5 * CLHEP::cm;
    G4ThreeVector lightconepos = pmtpos + offsetlightcone;
    if (lightcones) {
      new G4PVPlacement(lightconerot, lightconepos, "mumetal_phys",
                        lightcone_log, phys_mother, false, id);
    }

    if (!pmtParam.useEnvelope && logiWg) {
      // If not using envelope volume, the waveguide must be placed in a
      // separate operation

      // pmtrot is a passive rotation, but we need an active one to put offsetWg
      // into coordinates of mother
      G4ThreeVector offsetWg_rot = pmtrot->inverse()(offsetWg);
      G4ThreeVector waveguidepos = pmtpos + offsetWg_rot;
      new G4PVPlacement(pmtrot, waveguidepos,
                        pmtname +
                            "_waveguide", // a name for this physical volume
                        logiWg,           // the logical volume
                        phys_mother,      // the mother volume
                        false,            // no boolean ops
                        id);              // copy number
    }

  } // end loop over id

  // finally pass the lookup table to GLG4PMTOpticalModel
  if (BFieldOn) {
    const G4String modname(volume_name + "_optical_model");
    //    G4cout<<"Setting B Efficiency Correction Table for PMT optical model
    //    "<<modname<<"\n";

    if (logiPMT->GetFastSimulationManager()
            ->GetFastSimulationModelList()[0]
            ->GetName() == modname)
      ((GLG4PMTOpticalModel *)logiPMT->GetFastSimulationManager()
           ->GetFastSimulationModelList()[0])
          ->SetBEfficiencyCorrection(BEfficiencyCorrection);
    else {
      for (int i = 0; i < int(logiPMT->GetFastSimulationManager()
                                  ->GetFastSimulationModelList()
                                  .size());
           i++)
        if (logiPMT->GetFastSimulationManager()
                ->GetFastSimulationModelList()[i]
                ->GetName() == modname) {
          ((GLG4PMTOpticalModel *)logiPMT->GetFastSimulationManager()
               ->GetFastSimulationModelList()[i])
              ->SetBEfficiencyCorrection(BEfficiencyCorrection);
          G4cout << "trying to set B efficiency for "
                 << ((GLG4PMTOpticalModel *)logiPMT->GetFastSimulationManager()
                         ->GetFastSimulationModelList()[i])
                        ->GetName()
                 << "\n";
          break;
        }
    }
    /*    for(int
       i=0;i<int(logiPMT->GetFastSimulationManager()->GetFastSimulationModelList().size());i++)
          ((GLG4PMTOpticalModel
       *)logiPMT->GetFastSimulationManager()->GetFastSimulationModelList()[i])->DumpBEfficiencyCorrectionTable();*/
  }

  return 0; // There is no specific physical volume to return
}

} // namespace BUTTON
