#include <math.h>

#include <ButtonDetectorFactory.hh>
#include <RAT/DB.hh>
#include <RAT/Log.hh>
#include <vector>

namespace BUTTON {

void ButtonDetectorFactory::DefineDetector(RAT::DBLinkPtr /*detector*/) {
  RAT::DB *db = RAT::DB::Get();
  RAT::DBLinkPtr params = db->GetLink("BUTTON_PARAMS");
  const double photocathode_coverage = params->GetD("photocathode_coverage");
  const double veto_coverage = params->GetD("veto_coverage");
  const double veto_offset = 700;
  const std::string geo_template = "Button/Button.geo";
  if (db->Load(geo_template) == 0) {
    RAT::Log::Die("ButtonDetectorFactory: could not load template "
                  "Button/Button.geo");
  }

  // calculate the area of the defined inner_pmts
  RAT::DBLinkPtr inner_pmts = db->GetLink("GEO", "inner_pmts");
  std::string pmt_model = inner_pmts->GetS("pmt_model");
  RAT::DBLinkPtr pmt = db->GetLink("PMT", pmt_model);
  std::vector<double> rho_edge = pmt->GetDArray("rho_edge");
  double photocathode_radius = rho_edge[0];
  for (size_t i = 1; i < rho_edge.size(); i++) {
    if (photocathode_radius < rho_edge[i])
      photocathode_radius = rho_edge[i];
  }
  const double photocathode_area =
      M_PI * photocathode_radius * photocathode_radius;
  const double black_sheet_offset = inner_pmts->GetD(
      "black_sheet_offset"); // black tarp offset from table (30cm default)
  const double black_sheet_thickness = inner_pmts->GetD(
      "black_sheet_thickness"); // black tarp thickness from table (1cm default)

  RAT::DBLinkPtr shield = db->GetLink("GEO", "shield");
  const double steel_thickness = shield->GetD("steel_thickness");
  const double veto_thickness_r =
      shield->GetD("veto_thickness_r"); // Distance between TANK and Inner PMT
  const double detector_size_d = shield->GetD("detector_size_d");
  const double veto_thickness_z =
      shield->GetD("veto_thickness_z"); // Distance between TANK and Inner PMT
  const double detector_size_z = shield->GetD("detector_size_z");

  const double cable_radius =
      detector_size_d / 2.0 - veto_thickness_r + 4.0 * steel_thickness;
  const double pmt_radius =
      detector_size_d / 2.0 - veto_thickness_r - 4.0 * steel_thickness;
  const double veto_radius = pmt_radius + veto_offset;

  const double topbot_offset = detector_size_z / 2.0 - veto_thickness_z;
  const double topbot_veto_offset = topbot_offset + veto_offset;

  const double surface_area = 2.0 * M_PI * pmt_radius * pmt_radius +
                              2.0 * topbot_offset * 2.0 * M_PI * pmt_radius;
  const double required_pmts =
      ceil(photocathode_coverage * surface_area / photocathode_area);
  const double veto_surface_area =
      2.0 * M_PI * veto_radius * veto_radius +
      2.0 * topbot_veto_offset * 2.0 * M_PI * veto_radius;
  const double required_vetos =
      ceil(veto_coverage * veto_surface_area / photocathode_area);

  const double pmt_space = sqrt(surface_area / required_pmts);
  const double veto_space = sqrt(veto_surface_area / required_vetos);

  const size_t cols = round(2.0 * M_PI * pmt_radius / pmt_space);
  const size_t rows = round(2.0 * topbot_offset / pmt_space);
  const size_t veto_cols = round(2.0 * M_PI * veto_radius / veto_space);
  const size_t veto_rows = round(2.0 * topbot_veto_offset / veto_space);

  RAT::info << "Generating new PMT positions for:\n";
  RAT::info << "\tdesired photocathode coverage " << photocathode_coverage
            << '\n';
  RAT::info << "\ttotal area " << surface_area << '\n';
  RAT::info << "\tphotocathode radius " << photocathode_radius << '\n';
  RAT::info << "\tphotocathode area " << photocathode_area << '\n';
  RAT::info << "\tdesired PMTs " << required_pmts << '\n';
  RAT::info << "\tPMT spacing " << pmt_space << '\n';

  // make the grid for top and bottom PMTs
  std::vector<std::pair<int, int>> topbot;
  std::vector<std::pair<int, int>> topbot_veto;
  const int rdim = round(pmt_radius / pmt_space);
  for (int i = -rdim; i <= rdim; i++) {
    for (int j = -rdim; j <= rdim; j++) {
      if (pmt_space * sqrt(i * i + j * j) <= pmt_radius - pmt_space / 2.0) {
        topbot.push_back(std::make_pair(i, j));
      }
      if (veto_space * sqrt(i * i + j * j) <=
          pmt_radius - pmt_space / 2.0) { // pmt_* is not a mistake
        topbot_veto.push_back(std::make_pair(i, j));
      }
    }
  }

  size_t num_pmts = cols * rows + 2 * topbot.size();
  size_t num_vetos = veto_cols * veto_rows + 2 * topbot_veto.size();
  size_t total_pmts = num_pmts + num_vetos;

  RAT::info << "Actual calculated values:\n";
  RAT::info << "\tactual photocathode coverage "
            << photocathode_area * num_pmts / surface_area << '\n';
  RAT::info << "\tgenerated PMTs " << num_pmts << '\n';
  RAT::info << "\tcols " << cols << '\n';
  RAT::info << "\trows " << rows << '\n';
  RAT::info << "\tgenerated Vetos " << num_vetos << '\n';
  RAT::info << "\tcols " << veto_cols << '\n';
  RAT::info << "\trows " << veto_rows << '\n';

  std::vector<double> x(total_pmts), y(total_pmts), z(total_pmts),
      dir_x(total_pmts), dir_y(total_pmts), dir_z(total_pmts);
  std::vector<int> type(total_pmts);

  // generate cylinder PMT positions
  for (size_t col = 0; col < cols; col++) {
    for (size_t row = 0; row < rows; row++) {
      const size_t idx = row + col * rows;
      const double phi = 2.0 * M_PI * (col + 0.5) / cols;

      x[idx] = pmt_radius * cos(phi);
      y[idx] = pmt_radius * sin(phi);
      z[idx] =
          row * 2.0 * topbot_offset / rows + pmt_space / 2.0 - topbot_offset;

      dir_x[idx] = -cos(phi);
      dir_y[idx] = -sin(phi);
      dir_z[idx] = 0.0;

      type[idx] = 1;
    }
  }

  // generate topbot PMT positions
  for (size_t i = 0; i < topbot.size(); i++) {
    const size_t idx = rows * cols + i * 2;

    // top = idx
    x[idx] = pmt_space * topbot[i].first;
    y[idx] = pmt_space * topbot[i].second;
    z[idx] = topbot_offset;

    dir_x[idx] = dir_y[idx] = 0.0;
    dir_z[idx] = -1.0;

    type[idx] = 1;

    // bot = idx+1
    x[idx + 1] = pmt_space * topbot[i].first;
    y[idx + 1] = pmt_space * topbot[i].second;
    z[idx + 1] = -topbot_offset;

    dir_x[idx + 1] = dir_y[idx] = 0.0;
    dir_z[idx + 1] = 1.0;

    type[idx + 1] = 1;
  }

  // generate cylinder Veto positions
  for (size_t col = 0; col < veto_cols; col++) {
    for (size_t row = 0; row < veto_rows; row++) {
      const size_t idx = num_pmts + row + col * veto_rows;
      const double phi = 2.0 * M_PI * col / veto_cols;

      x[idx] = veto_radius * cos(phi);
      y[idx] = veto_radius * sin(phi);
      z[idx] = row * 2.0 * topbot_offset / veto_rows + veto_space / 2 -
               topbot_offset;

      dir_x[idx] = cos(phi);
      dir_y[idx] = sin(phi);
      dir_z[idx] = 0.0;

      type[idx] = 2;
    }
  }

  // generate topbot Veto positions
  for (size_t i = 0; i < topbot_veto.size(); i++) {
    const size_t idx = num_pmts + veto_rows * veto_cols + i * 2;

    // top = idx
    x[idx] = veto_space * topbot_veto[i].first;
    y[idx] = veto_space * topbot_veto[i].second;
    z[idx] = topbot_veto_offset;

    dir_x[idx] = dir_y[idx] = 0.0;
    dir_z[idx] = 1.0;

    type[idx] = 2;

    // bot = idx+1
    x[idx + 1] = veto_space * topbot_veto[i].first;
    y[idx + 1] = veto_space * topbot_veto[i].second;
    z[idx + 1] = -topbot_veto_offset;

    dir_x[idx + 1] = dir_y[idx] = 0.0;
    dir_z[idx + 1] = -1.0;

    type[idx + 1] = 2;
  }

  // generate cable positions
  std::vector<double> cable_x(cols), cable_y(cols);
  for (size_t col = 0; col < cols; col++) {
    cable_x[col] = cable_radius * cos(col * 2.0 * M_PI / cols);
    cable_y[col] = cable_radius * sin(col * 2.0 * M_PI / cols);
  }

  RAT::info
      << "Update geometry fields related to the reflective and absorptive "
         "tarps...\n";
  // Side tarps
  db->Set("GEO", "white_sheet_side", "r_max", veto_radius);
  db->Set("GEO", "white_sheet_side", "r_min",
          veto_radius -
              10.0); // Marc Bergevin: Hardcoding in a 1 cm value for thickness
  db->Set("GEO", "white_sheet_side", "size_z", topbot_veto_offset);

  db->Set("GEO", "black_sheet_side", "r_max",
          pmt_radius + black_sheet_offset +
              black_sheet_thickness); // paige kunkle: expanding black tarp
                                      // (+30cm) // Marc Bergevin: Hardcoding in
                                      // a 1 cm value for thickness
  db->Set("GEO", "black_sheet_side", "r_min",
          pmt_radius +
              black_sheet_offset); // paige kunkle: expanding black tarp (+30cm)
  db->Set("GEO", "black_sheet_side", "size_z",
          topbot_offset +
              black_sheet_offset); // paige kunkle: expanding black tarp (+30cm)

  db->Set("GEO", "Rod_assemblies", "r_max",
          (pmt_radius + 300.)); // Based on Geofile thickness values of 10 cm
  db->Set("GEO", "Rod_assemblies", "r_min", (pmt_radius + 200.));
  db->Set("GEO", "Rod_assemblies", "size_z", topbot_offset);

  db->Set("GEO", "white_sheet_tank_side", "r_max",
          detector_size_d / 2.0 - 10.0);
  db->Set("GEO", "white_sheet_tank_side", "r_min",
          detector_size_d / 2.0 - 35.0);
  db->Set("GEO", "white_sheet_tank_side", "size_z",
          detector_size_z / 2.0 - 35.0);

  // Top tarps
  std::vector<double> move_white_top;
  move_white_top.push_back(0.0);
  move_white_top.push_back(0.0);
  move_white_top.push_back(topbot_veto_offset);
  std::vector<double> move_black_top;
  move_black_top.push_back(0.0);
  move_black_top.push_back(0.0);
  move_black_top.push_back(
      topbot_offset +
      black_sheet_offset); // paige kunkle: expanding black tarp (+30cm)
  std::vector<double> move_topcap;
  move_topcap.push_back(0.0);
  move_topcap.push_back(0.0);
  move_topcap.push_back(topbot_offset + 200.);
  std::vector<double> move_toptruss;
  move_toptruss.push_back(0.0);
  move_toptruss.push_back(0.0);
  move_toptruss.push_back(topbot_offset + 200. +
                          2.5); // Bergevin: Values based on geofile

  std::vector<double> move_toptanktarp;
  move_toptanktarp.push_back(0.0);
  move_toptanktarp.push_back(0.0);
  move_toptanktarp.push_back(detector_size_z / 2.0 -
                             30.0); // Bergevin: Values based on geofile

  db->Set("GEO", "white_sheet_top", "r_max", veto_radius);
  db->Set("GEO", "white_sheet_top", "position", move_white_top);
  db->Set("GEO", "black_sheet_top", "r_max",
          pmt_radius +
              black_sheet_offset); // paige kunkle: expanding black tarp (+30cm)
  db->Set("GEO", "black_sheet_top", "position", move_black_top);
  db->Set("GEO", "Top_cap_framework", "r_max", pmt_radius);
  db->Set("GEO", "Top_cap_framework", "position", move_topcap);
  db->Set("GEO", "Wall_support_truss_top", "r_min",
          pmt_radius + 5.0); // Bergevin: Values based
  db->Set("GEO", "Wall_support_truss_top", "r_max",
          pmt_radius + 200.0); // on geofile
  db->Set("GEO", "Wall_support_truss_top", "position", move_toptruss);

  db->Set("GEO", "white_sheet_tank_top", "r_max", detector_size_d / 2.0 - 35.0);
  db->Set("GEO", "white_sheet_tank_top", "position", move_toptanktarp);

  // Bottom tarps
  std::vector<double> move_white_bottom;
  move_white_bottom.push_back(0.0);
  move_white_bottom.push_back(0.0);
  move_white_bottom.push_back(-topbot_veto_offset);
  std::vector<double> move_black_bottom;
  move_black_bottom.push_back(0.0);
  move_black_bottom.push_back(0.0);
  move_black_bottom.push_back(
      -topbot_offset -
      black_sheet_offset); // paige kunkle: expanding black tarp (+30cm)
  std::vector<double> move_bottomcap;
  move_bottomcap.push_back(0.0);
  move_bottomcap.push_back(0.0);
  move_bottomcap.push_back(-topbot_offset - 200.);
  std::vector<double> move_bottomtruss;
  move_bottomtruss.push_back(0.0);
  move_bottomtruss.push_back(0.0);
  move_bottomtruss.push_back(-topbot_offset - 200. -
                             2.5); // Bergevin: Values based on geofile

  std::vector<double> move_bottomtanktarp;
  move_bottomtanktarp.push_back(0.0);
  move_bottomtanktarp.push_back(0.0);
  move_bottomtanktarp.push_back(-detector_size_z / 2.0 +
                                30.0); // Bergevin: Values based on geofile

  db->Set("GEO", "white_sheet_bottom", "r_max", veto_radius);
  db->Set("GEO", "white_sheet_bottom", "position", move_white_bottom);
  db->Set("GEO", "black_sheet_bottom", "r_max",
          pmt_radius +
              black_sheet_offset); // paige kunkle: expanding black tarp (+30cm)
  db->Set("GEO", "black_sheet_bottom", "position", move_black_bottom);
  db->Set("GEO", "Bottom_cap_framework", "r_max", pmt_radius);
  db->Set("GEO", "Bottom_cap_framework", "position", move_bottomcap);
  db->Set("GEO", "Wall_support_truss_bottom", "r_min",
          pmt_radius + 5.0); // Bergevin: Values based
  db->Set("GEO", "Wall_support_truss_bottom", "r_max",
          pmt_radius + 200.0); // on geofile
  db->Set("GEO", "Wall_support_truss_bottom", "position", move_bottomtruss);

  db->Set("GEO", "white_sheet_tank_bottom", "r_max",
          detector_size_d / 2.0 - 35.0);
  db->Set("GEO", "white_sheet_tank_bottom", "position", move_bottomtanktarp);

  RAT::info << "Adjusting the Bottom cap standoff frames ...\n";

  RAT::DBLinkPtr frame_0 = db->GetLink("GEO", "Bottom_cap_standoff_frame_0");
  std::vector<double> standoff_frame_0_size = frame_0->GetDArray("size");
  std::vector<double> standoff_frame_0_pos = frame_0->GetDArray("position");
  RAT::DBLinkPtr frame_1 = db->GetLink("GEO", "Bottom_cap_standoff_frame_1");
  std::vector<double> standoff_frame_1_size = frame_1->GetDArray("size");
  std::vector<double> standoff_frame_1_pos = frame_1->GetDArray("position");
  RAT::DBLinkPtr frame_2 = db->GetLink("GEO", "Bottom_cap_standoff_frame_2");
  std::vector<double> standoff_frame_2_size = frame_2->GetDArray("size");
  std::vector<double> standoff_frame_2_pos = frame_2->GetDArray("position");
  RAT::DBLinkPtr frame_3 = db->GetLink("GEO", "Bottom_cap_standoff_frame_3");
  std::vector<double> standoff_frame_3_size = frame_3->GetDArray("size");
  std::vector<double> standoff_frame_3_pos = frame_3->GetDArray("position");
  RAT::DBLinkPtr frame_4 = db->GetLink("GEO", "Bottom_cap_standoff_frame_4");
  std::vector<double> standoff_frame_4_size = frame_4->GetDArray("size");
  std::vector<double> standoff_frame_4_pos = frame_4->GetDArray("position");

  RAT::info << "Size loaded in frame 0" << standoff_frame_0_size[0] << " "
            << standoff_frame_0_size[1] << " " << standoff_frame_0_size[2]
            << "...\n";
  if (standoff_frame_0_size[2] !=
      (detector_size_z / 2.0 - (topbot_offset + 200. + 2.5))) {
    standoff_frame_0_size[2] =
        (detector_size_z / 2.0 - (topbot_offset + 200. + 2.5)) / 2.0;
    standoff_frame_0_pos[2] =
        -(detector_size_z / 2.0 + (topbot_offset + 200. + 2.5)) / 2.0;
    RAT::info << "New size " << standoff_frame_0_size[0] << " "
              << standoff_frame_0_size[1] << " " << standoff_frame_0_size[2]
              << "...\n";
  }
  RAT::info << "Size loaded in frame 1" << standoff_frame_1_size[0] << " "
            << standoff_frame_1_size[1] << " " << standoff_frame_1_size[2]
            << "...\n";
  if (standoff_frame_1_size[2] !=
      (detector_size_z / 2.0 - (topbot_offset + 200. + 2.5))) {
    standoff_frame_1_size[2] =
        (detector_size_z / 2.0 - (topbot_offset + 200. + 2.5)) / 2.0;
    standoff_frame_1_pos[2] =
        -(detector_size_z / 2.0 + (topbot_offset + 200. + 2.5)) / 2.0;
    RAT::info << "New size " << standoff_frame_1_size[0] << " "
              << standoff_frame_1_size[1] << " " << standoff_frame_1_size[2]
              << "...\n";
  }
  RAT::info << "Size loaded in frame 2" << standoff_frame_2_size[0] << " "
            << standoff_frame_2_size[1] << " " << standoff_frame_2_size[2]
            << "...\n";
  if (standoff_frame_2_size[2] !=
      (detector_size_z / 2.0 - (topbot_offset + 200. + 2.5))) {
    standoff_frame_2_size[2] =
        (detector_size_z / 2.0 - (topbot_offset + 200. + 2.5)) / 2.0;
    standoff_frame_2_pos[2] =
        -(detector_size_z / 2.0 + (topbot_offset + 200. + 2.5)) / 2.0;
    RAT::info << "New size " << standoff_frame_2_size[0] << " "
              << standoff_frame_2_size[1] << " " << standoff_frame_2_size[2]
              << "...\n";
  }
  RAT::info << "Size loaded in frame 3" << standoff_frame_3_size[0] << " "
            << standoff_frame_3_size[1] << " " << standoff_frame_3_size[2]
            << "...\n";
  if (standoff_frame_3_size[2] !=
      (detector_size_z / 2.0 - (topbot_offset + 200. + 2.5))) {
    standoff_frame_3_size[2] =
        (detector_size_z / 2.0 - (topbot_offset + 200. + 2.5)) / 2.0;
    standoff_frame_3_pos[2] =
        -(detector_size_z / 2.0 + (topbot_offset + 200. + 2.5)) / 2.0;
    RAT::info << "New size " << standoff_frame_3_size[0] << " "
              << standoff_frame_3_size[1] << " " << standoff_frame_3_size[2]
              << "...\n";
  }
  RAT::info << "Size loaded in frame 4" << standoff_frame_4_size[0] << " "
            << standoff_frame_4_size[1] << " " << standoff_frame_4_size[2]
            << "...\n";
  if (standoff_frame_4_size[2] !=
      (detector_size_z / 2.0 - (topbot_offset + 200. + 2.5))) {
    standoff_frame_4_size[2] =
        (detector_size_z / 2.0 - (topbot_offset + 200. + 2.5)) / 2.0;
    standoff_frame_4_pos[2] =
        -(detector_size_z / 2.0 + (topbot_offset + 200. + 2.5)) / 2.0;
    RAT::info << "New size " << standoff_frame_4_size[0] << " "
              << standoff_frame_4_size[1] << " " << standoff_frame_4_size[2]
              << "...\n";
  }

  db->Set("GEO", "Bottom_cap_standoff_frame_0", "size", standoff_frame_0_size);
  db->Set("GEO", "Bottom_cap_standoff_frame_0", "position",
          standoff_frame_0_pos);
  db->Set("GEO", "Bottom_cap_standoff_frame_1", "size", standoff_frame_1_size);
  db->Set("GEO", "Bottom_cap_standoff_frame_1", "position",
          standoff_frame_1_pos);
  db->Set("GEO", "Bottom_cap_standoff_frame_2", "size", standoff_frame_2_size);
  db->Set("GEO", "Bottom_cap_standoff_frame_2", "position",
          standoff_frame_2_pos);
  db->Set("GEO", "Bottom_cap_standoff_frame_3", "size", standoff_frame_3_size);
  db->Set("GEO", "Bottom_cap_standoff_frame_3", "position",
          standoff_frame_3_pos);
  db->Set("GEO", "Bottom_cap_standoff_frame_4", "size", standoff_frame_4_size);
  db->Set("GEO", "Bottom_cap_standoff_frame_4", "position",
          standoff_frame_4_pos);

  RAT::info << "Override default PMTINFO RAT::information...\n";
  db->Set("PMTINFO", "x", x);
  db->Set("PMTINFO", "y", y);
  db->Set("PMTINFO", "z", z);
  db->Set("PMTINFO", "dir_x", dir_x);
  db->Set("PMTINFO", "dir_y", dir_y);
  db->Set("PMTINFO", "dir_z", dir_z);
  db->Set("PMTINFO", "type", type);

  RAT::info
      << "Update geometry fields related to the reflective and absorptive "
         "tarps...\n";

  RAT::info << "Update geometry fields related to veto PMTs...\n";
  db->Set("GEO", "shield", "veto_start", num_pmts);
  db->Set("GEO", "shield", "veto_len", num_vetos);
  db->Set("GEO", "veto_pmts", "start_idx", num_pmts);
  db->Set("GEO", "veto_pmts", "end_idx", total_pmts - 1);

  RAT::info << "Update geometry fields related to normal PMTs...\n";
  db->Set("GEO", "shield", "cols", cols);
  db->Set("GEO", "shield", "rows", rows);
  db->Set("GEO", "shield", "inner_start", 0);
  db->Set("GEO", "shield", "inner_len", num_pmts);
  db->Set("GEO", "inner_pmts", "start_idx", 0);
  db->Set("GEO", "inner_pmts", "end_idx", num_pmts - 1);

  RAT::info << "Update cable positions to match shield...\n";
  db->Set("cable_pos", "x", cable_x);
  db->Set("cable_pos", "y", cable_y);
  db->Set("cable_pos", "z", std::vector<double>(cols, 0.0));
  db->Set("cable_pos", "dir_x", std::vector<double>(cols, 0.0));
  db->Set("cable_pos", "dir_y", std::vector<double>(cols, 0.0));
  db->Set("cable_pos", "dir_z", std::vector<double>(cols, 1.0));

  //   const std::vector<double> &size = table->GetDArray("boxsize");
  RAT::DBLinkPtr cavern = db->GetLink("GEO", "cavern");
  // const std::vector<double>  &cavSize = cavern->GetDArray("size_z"); //Should
  // be a cube float _shift = cavSize[0]-detector_size_z/2.0;
  const double cavSize = cavern->GetD("size_z"); // Should be a cube
  float _shift = cavSize - detector_size_z / 2.0;

  if (_shift < 0.0) {
    RAT::info << "size of detector greater than cavern. (" << detector_size_z
              << " mm," << cavSize * 2 << "\n";
  }
  std::vector<double> shift, minshift;
  shift.push_back(0.0);
  shift.push_back(0.0);
  shift.push_back(_shift);
  minshift.push_back(0.0);
  minshift.push_back(0.0);
  minshift.push_back(-_shift);
  RAT::info << "Update height of rock and cavern air... (" << _shift
            << " mm shift)\n";

  db->Set("GEO", "rock_1", "position", shift);

  RAT::info << "Adjust size and position of tank...\n";
  db->Set("GEO", "tank", "r_max", detector_size_d / 2.0);
  db->Set("GEO", "tank", "size_z", detector_size_z / 2.0);
  db->Set("GEO", "tank", "position", minshift);
}

} // namespace BUTTON
