#include "features.h"

#include "atomic_file.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numeric>

namespace {

// Formato .feat (docs/formatos.md), little-endian:
//   char[4] "E3DF" | uint32 versión | int32 ancho_orig, alto_orig, ancho_trab, alto_trab
//   | float64 escala | uint32 n | uint32 dim (128)
//   | n x {float32 x, y, size, angle, response; int32 octave}
//   | n x dim float32 (descriptores por filas)
constexpr char kMagic[4] = {'E', '3', 'D', 'F'};
constexpr std::uint32_t kFormatVersion = 1;

template <class T>
void put(std::string& out, const T& v) {
  out.append(reinterpret_cast<const char*>(&v), sizeof(T));
}

class ByteReader {
 public:
  explicit ByteReader(const std::string& bytes) : bytes_(bytes) {}
  template <class T>
  T get() {
    T v;
    read(&v, sizeof(T));
    return v;
  }
  void read(void* dst, std::size_t n) {
    if (n > bytes_.size() - pos_) throw FeatureFileError("archivo de características truncado");
    std::memcpy(dst, bytes_.data() + pos_, n);
    pos_ += n;
  }
  bool atEnd() const { return pos_ == bytes_.size(); }

 private:
  const std::string& bytes_;
  std::size_t pos_ = 0;
};

cv::Mat toGray(const cv::Mat& image) {
  cv::Mat gray;
  if (image.channels() == 3)
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
  else if (image.channels() == 4)
    cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
  else
    gray = image;
  if (gray.depth() != CV_8U) gray.convertTo(gray, CV_8U);
  return gray;
}

// Centro de píxel de la imagen de trabajo -> imagen original (resize alinea las
// esquinas de la imagen, no los centros de los píxeles: x_orig = (x + 0,5) / s - 0,5).
float toOriginal(float work_coord, double scale) {
  return static_cast<float>((work_coord + 0.5) / scale - 0.5);
}

}  // namespace

cv::Mat prepareWorkImage(const cv::Mat& image, const MatchingParams& params, double& scale) {
  if (image.empty()) throw std::invalid_argument("imagen vacía");
  cv::Mat gray = toGray(image);
  scale = 1.0;
  const int long_side = std::max(gray.cols, gray.rows);
  if (long_side > params.work_long_side) {
    scale = static_cast<double>(params.work_long_side) / long_side;
    const cv::Size work(static_cast<int>(std::lround(gray.cols * scale)),
                        static_cast<int>(std::lround(gray.rows * scale)));
    cv::Mat reduced;
    cv::resize(gray, reduced, work, 0, 0, cv::INTER_AREA);
    gray = reduced;
  }
  if (params.clahe) {
    // HU-08: ecualización adaptativa por bloques, sobre la imagen ya reducida.
    cv::Ptr<cv::CLAHE> clahe =
        cv::createCLAHE(params.clahe_clip_limit, cv::Size(params.clahe_tile, params.clahe_tile));
    cv::Mat equalized;
    clahe->apply(gray, equalized);
    gray = equalized;
  }
  return gray;
}

ImageFeatures extractFeatures(const cv::Mat& image, const MatchingParams& params) {
  ImageFeatures f;
  const cv::Mat work = prepareWorkImage(image, params, f.scale);
  f.original_width = image.cols;
  f.original_height = image.rows;
  f.work_width = work.cols;
  f.work_height = work.rows;

  cv::Ptr<cv::SIFT> sift = cv::SIFT::create(params.max_features);
  std::vector<cv::KeyPoint> kps;
  cv::Mat desc;
  sift->detectAndCompute(work, cv::noArray(), kps, desc);

  // SIFT puede devolver algunos puntos de más (empates de respuesta): se recorta al tope.
  std::vector<int> order(kps.size());
  std::iota(order.begin(), order.end(), 0);
  if (static_cast<int>(kps.size()) > params.max_features) {
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return kps[a].response > kps[b].response; });
    order.resize(static_cast<std::size_t>(params.max_features));
  }

  f.keypoints.reserve(order.size());
  f.descriptors.create(static_cast<int>(order.size()), kSiftDescriptorSize, CV_32F);
  for (std::size_t r = 0; r < order.size(); ++r) {
    cv::KeyPoint kp = kps[order[r]];
    kp.pt.x = toOriginal(kp.pt.x, f.scale);
    kp.pt.y = toOriginal(kp.pt.y, f.scale);
    kp.size = static_cast<float>(kp.size / f.scale);
    f.keypoints.push_back(kp);
    desc.row(order[r]).copyTo(f.descriptors.row(static_cast<int>(r)));
  }
  return f;
}

std::string featureFileName(int index) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "img_%03d.feat", index);
  return buffer;
}

void writeFeatures(const std::filesystem::path& file, const ImageFeatures& f) {
  if (f.descriptors.rows != static_cast<int>(f.keypoints.size()) ||
      (!f.keypoints.empty() && (f.descriptors.cols != kSiftDescriptorSize || f.descriptors.type() != CV_32F)))
    throw std::invalid_argument("descriptores inconsistentes con los puntos");

  std::string out;
  out.append(kMagic, sizeof(kMagic));
  put(out, kFormatVersion);
  put(out, static_cast<std::int32_t>(f.original_width));
  put(out, static_cast<std::int32_t>(f.original_height));
  put(out, static_cast<std::int32_t>(f.work_width));
  put(out, static_cast<std::int32_t>(f.work_height));
  put(out, f.scale);
  put(out, static_cast<std::uint32_t>(f.keypoints.size()));
  put(out, static_cast<std::uint32_t>(kSiftDescriptorSize));
  for (const cv::KeyPoint& kp : f.keypoints) {
    put(out, kp.pt.x);
    put(out, kp.pt.y);
    put(out, kp.size);
    put(out, kp.angle);
    put(out, kp.response);
    put(out, static_cast<std::int32_t>(kp.octave));
  }
  for (int r = 0; r < f.descriptors.rows; ++r)
    out.append(reinterpret_cast<const char*>(f.descriptors.ptr<float>(r)), kSiftDescriptorSize * sizeof(float));
  writeFileAtomic(file, out);
}

ImageFeatures readFeatures(const std::filesystem::path& file) {
  std::string bytes;
  try {
    bytes = readFileBytes(file);
  } catch (const std::runtime_error& e) {
    throw FeatureFileError(e.what());
  }
  ByteReader in(bytes);
  char magic[4];
  in.read(magic, sizeof(magic));
  if (std::memcmp(magic, kMagic, sizeof(kMagic)) != 0)
    throw FeatureFileError("no es un archivo de características: " + file.string());
  if (in.get<std::uint32_t>() != kFormatVersion)
    throw FeatureFileError("versión de archivo de características no soportada: " + file.string());

  ImageFeatures f;
  f.original_width = in.get<std::int32_t>();
  f.original_height = in.get<std::int32_t>();
  f.work_width = in.get<std::int32_t>();
  f.work_height = in.get<std::int32_t>();
  f.scale = in.get<double>();
  const std::uint32_t n = in.get<std::uint32_t>();
  if (in.get<std::uint32_t>() != kSiftDescriptorSize)
    throw FeatureFileError("dimensión de descriptor inesperada en " + file.string());
  constexpr std::size_t kHeaderBytes = 4 + 7 * 4 + 8;  // magia, 7 enteros de 32 bits, escala
  const std::size_t expected = kHeaderBytes + std::size_t(n) * (6 * 4 + kSiftDescriptorSize * 4);
  if (bytes.size() != expected) throw FeatureFileError("tamaño inconsistente en " + file.string());

  f.keypoints.resize(n);
  for (cv::KeyPoint& kp : f.keypoints) {
    kp.pt.x = in.get<float>();
    kp.pt.y = in.get<float>();
    kp.size = in.get<float>();
    kp.angle = in.get<float>();
    kp.response = in.get<float>();
    kp.octave = in.get<std::int32_t>();
  }
  f.descriptors.create(static_cast<int>(n), kSiftDescriptorSize, CV_32F);
  for (int r = 0; r < f.descriptors.rows; ++r)
    in.read(f.descriptors.ptr<float>(r), kSiftDescriptorSize * sizeof(float));
  return f;
}
