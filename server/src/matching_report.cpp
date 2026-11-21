#include "matching_report.h"

#include "atomic_file.h"

#include <cstdio>
#include <sstream>
#include <stdexcept>

using nlohmann::json;

namespace {

const char* const kInliersHeader = "index_i,index_j,x_i,y_i,x_j,y_j,distance,sampson_px";

json matrixJson(const cv::Matx33d& m) {
  return json::array({json::array({m(0, 0), m(0, 1), m(0, 2)}), json::array({m(1, 0), m(1, 1), m(1, 2)}),
                      json::array({m(2, 0), m(2, 1), m(2, 2)})});
}

json optionalMatrixJson(const std::optional<cv::Matx33d>& m) { return m ? matrixJson(*m) : json(nullptr); }

cv::Matx33d matrixFromJson(const json& j) {
  if (!j.is_array() || j.size() != 3) throw std::invalid_argument("matriz 3x3 inválida");
  cv::Matx33d m;
  for (int r = 0; r < 3; ++r) {
    if (!j.at(r).is_array() || j.at(r).size() != 3) throw std::invalid_argument("matriz 3x3 inválida");
    for (int c = 0; c < 3; ++c) m(r, c) = j.at(r).at(c).get<double>();
  }
  return m;
}

json toJson(const ViewReport& v) {
  return json{{"index", v.index},
              {"file", v.file},
              {"features_file", v.features_file},
              {"n_features", v.n_features},
              {"width", v.intrinsics.width},
              {"height", v.intrinsics.height},
              {"work_width", v.work_width},
              {"work_height", v.work_height},
              {"focal_px", v.intrinsics.focal_px},
              {"focal_source", v.intrinsics.used_fallback ? "fallback" : "focal_35mm"},
              {"K", matrixJson(v.intrinsics.matrix())},
              {"load_ms", v.load_ms},
              {"extract_ms", v.extract_ms}};
}

json toJson(const PairReport& p) {
  return json{{"i", p.i},
              {"j", p.j},
              {"angle_deg", p.angle_deg},
              {"n_matches", p.n_matches},
              {"n_inliers", p.n_inliers},
              {"inlier_ratio", p.inlier_ratio},
              {"mean_sampson_px", p.mean_sampson_px},
              {"accepted", p.accepted},
              {"reasons", p.reasons},
              {"rank2_enforced", p.rank2_enforced},
              {"F", optionalMatrixJson(p.F)},
              {"E", optionalMatrixJson(p.E)},
              {"match_ms", p.match_ms},
              {"geometry_ms", p.geometry_ms},
              // Solo los pares aceptados apuntan a sus inliers: es lo que leerá el Sprint 3.
              {"inliers_file", p.accepted && !p.inliers_file.empty() ? json(p.inliers_file) : json(nullptr)}};
}

json toJson(const MatchingSummary& s) {
  return json{{"views_used", s.views_used},         {"views_excluded", s.views_excluded},
              {"pairs_all_vs_all", s.pairs_all_vs_all}, {"pairs_selected", s.pairs_selected},
              {"pairs_accepted", s.pairs_accepted}, {"pairs_rejected", s.pairs_rejected}};
}

ViewReport viewFromJson(const json& j) {
  ViewReport v;
  v.index = j.at("index").get<int>();
  v.file = j.at("file").get<std::string>();
  v.features_file = j.at("features_file").get<std::string>();
  v.n_features = j.at("n_features").get<int>();
  v.work_width = j.at("work_width").get<int>();
  v.work_height = j.at("work_height").get<int>();
  v.intrinsics.width = j.at("width").get<int>();
  v.intrinsics.height = j.at("height").get<int>();
  v.intrinsics.focal_px = j.at("focal_px").get<double>();
  v.intrinsics.used_fallback = j.at("focal_source").get<std::string>() == "fallback";
  const cv::Matx33d K = matrixFromJson(j.at("K"));
  v.intrinsics.cx = K(0, 2);
  v.intrinsics.cy = K(1, 2);
  v.load_ms = j.at("load_ms").get<double>();
  v.extract_ms = j.at("extract_ms").get<double>();
  return v;
}

PairReport pairFromJson(const json& j) {
  PairReport p;
  p.i = j.at("i").get<int>();
  p.j = j.at("j").get<int>();
  p.angle_deg = j.at("angle_deg").get<double>();
  p.n_matches = j.at("n_matches").get<int>();
  p.n_inliers = j.at("n_inliers").get<int>();
  p.inlier_ratio = j.at("inlier_ratio").get<double>();
  p.mean_sampson_px = j.at("mean_sampson_px").get<double>();
  p.accepted = j.at("accepted").get<bool>();
  p.reasons = j.at("reasons").get<std::vector<std::string>>();
  p.rank2_enforced = j.at("rank2_enforced").get<bool>();
  if (!j.at("F").is_null()) p.F = matrixFromJson(j.at("F"));
  if (!j.at("E").is_null()) p.E = matrixFromJson(j.at("E"));
  p.match_ms = j.at("match_ms").get<double>();
  p.geometry_ms = j.at("geometry_ms").get<double>();
  if (!j.at("inliers_file").is_null()) p.inliers_file = j.at("inliers_file").get<std::string>();
  return p;
}

}  // namespace

MatchingSummary summarize(const MatchingReport& r) {
  MatchingSummary s;
  s.views_used = static_cast<int>(r.views.size());
  s.views_excluded = static_cast<int>(r.excluded_views.size());
  s.pairs_all_vs_all = s.views_used * (s.views_used - 1) / 2;
  s.pairs_selected = static_cast<int>(r.pairs.size());
  for (const PairReport& p : r.pairs) (p.accepted ? s.pairs_accepted : s.pairs_rejected)++;
  return s;
}

json toJson(const MatchingReport& r) {
  json views = json::array();
  for (const ViewReport& v : r.views) views.push_back(toJson(v));
  json excluded = json::array();
  for (const ExcludedView& e : r.excluded_views) excluded.push_back({{"index", e.index}, {"reason", e.reason}});
  json pairs = json::array();
  for (const PairReport& p : r.pairs) pairs.push_back(toJson(p));
  return json{{"schema_version", r.schema_version},
              {"session_id", r.session_id},
              {"generated_at", r.generated_at},
              {"params", toJson(r.params)},
              {"views", std::move(views)},
              {"excluded_views", std::move(excluded)},
              {"pairs", std::move(pairs)},
              {"warnings", r.warnings},
              {"timing", {{"extraction_ms", r.extraction_ms}, {"matching_ms", r.matching_ms}, {"total_ms", r.total_ms}}},
              {"summary", toJson(summarize(r))}};
}

MatchingReport matchingReportFromJson(const json& j) {
  try {
    MatchingReport r;
    r.schema_version = j.at("schema_version").get<int>();
    if (r.schema_version != kMatchingReportSchemaVersion)
      throw std::invalid_argument("versión de matching_report no soportada");
    r.session_id = j.at("session_id").get<std::string>();
    r.generated_at = j.at("generated_at").get<std::string>();
    r.params = applyMatchingJson(MatchingParams{}, j.at("params"));
    for (const json& v : j.at("views")) r.views.push_back(viewFromJson(v));
    for (const json& e : j.at("excluded_views"))
      r.excluded_views.push_back({e.at("index").get<int>(), e.at("reason").get<std::string>()});
    for (const json& p : j.at("pairs")) r.pairs.push_back(pairFromJson(p));
    r.warnings = j.at("warnings").get<std::vector<std::string>>();
    const json& t = j.at("timing");
    r.extraction_ms = t.at("extraction_ms").get<double>();
    r.matching_ms = t.at("matching_ms").get<double>();
    r.total_ms = t.at("total_ms").get<double>();
    return r;
  } catch (const json::exception& e) {
    throw std::invalid_argument(std::string("matching_report inválido: ") + e.what());
  }
}

void writeMatchingReport(const std::filesystem::path& file, const MatchingReport& r) {
  writeFileAtomic(file, toJson(r).dump(2) + '\n');
}

MatchingReport readMatchingReport(const std::filesystem::path& file) {
  return matchingReportFromJson(json::parse(readFileBytes(file)));
}

std::string inliersFileName(int i, int j) {
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), "matches/pair_%03d_%03d.csv", i, j);
  return buffer;
}

void writeInliers(const std::filesystem::path& file, const std::vector<InlierCorrespondence>& inliers) {
  std::string out = std::string(kInliersHeader) + '\n';
  char line[256];
  for (const InlierCorrespondence& c : inliers) {
    // %.9g reproduce exactamente un float; %.17g, un double.
    std::snprintf(line, sizeof(line), "%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.17g\n", c.index_i, c.index_j,
                  double(c.point_i.x), double(c.point_i.y), double(c.point_j.x), double(c.point_j.y),
                  double(c.distance), c.sampson_px);
    out += line;
  }
  writeFileAtomic(file, out);
}

std::vector<InlierCorrespondence> readInliers(const std::filesystem::path& file) {
  std::istringstream in(readFileBytes(file));
  std::string line;
  if (!std::getline(in, line) || line.rfind(kInliersHeader, 0) != 0)
    throw std::runtime_error("cabecera inválida en " + file.string());

  std::vector<InlierCorrespondence> out;
  int line_no = 1;
  while (std::getline(in, line)) {
    ++line_no;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ',')) fields.push_back(field);
    if (fields.size() != 8)
      throw std::runtime_error("línea " + std::to_string(line_no) + " inválida en " + file.string());
    try {
      InlierCorrespondence c;
      std::size_t used = 0;
      auto checked = [&](const std::string& s) {
        if (used != s.size()) throw std::invalid_argument(s);
      };
      c.index_i = std::stoi(fields[0], &used), checked(fields[0]);
      c.index_j = std::stoi(fields[1], &used), checked(fields[1]);
      c.point_i.x = std::stof(fields[2], &used), checked(fields[2]);
      c.point_i.y = std::stof(fields[3], &used), checked(fields[3]);
      c.point_j.x = std::stof(fields[4], &used), checked(fields[4]);
      c.point_j.y = std::stof(fields[5], &used), checked(fields[5]);
      c.distance = std::stof(fields[6], &used), checked(fields[6]);
      c.sampson_px = std::stod(fields[7], &used), checked(fields[7]);
      out.push_back(c);
    } catch (const std::logic_error&) {
      throw std::runtime_error("línea " + std::to_string(line_no) + " inválida en " + file.string());
    }
  }
  return out;
}
