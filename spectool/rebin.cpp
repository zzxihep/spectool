#include "rebin.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <vector>
#include <limits>
#define NDEBUG
#include <assert.h>
#include<gsl/gsl_statistics.h>
#include "pybind11/stl.h"

#include "types.h"

#define DARR std::vector<double>

DARR get_edge(const DARR& wave) {
  DARR interval(wave.size());
  std::adjacent_difference(wave.begin(), wave.end(), interval.begin());
  interval[0] = interval[1];
  for (auto& val : interval)
    val *= 0.5;
  DARR edge_out(wave.size() + 1);
  for (size_t ind = 0; ind < wave.size(); ++ind)
    edge_out[ind] = wave[ind] - interval[ind];
  edge_out.back() = wave.back() + interval.back();
  return edge_out;
}

struct wtf {
 public:
  int type;
  double wave;
  double flux;
  bool operator<(const wtf& b) const { return wave < b.wave; }
};

std::vector<wtf> ssort(const std::vector<wtf>& arr1,
                       const std::vector<wtf>& arr2) {
  std::vector<wtf> result(arr1.size() + arr2.size());
  size_t ind = 0;
  auto itr1 = arr1.begin();
  auto itr2 = arr2.begin();
  while (itr1 != arr1.end() && itr2 != arr2.end())
    if ((*itr1) < (*itr2))
      result[ind++] = *(itr1++);
    else
      result[ind++] = *(itr2++);
  while (itr1 != arr1.end())
    result[ind++] = *(itr1++);
  while (itr2 != arr2.end())
    result[ind++] = *(itr2++);
  return result;
}

std::vector<wtf> merge_edge(const DARR& wave,
                            const DARR& flux,
                            const DARR& new_wave,
                            int endtype = 0) {
  auto edge1 = get_edge(wave);
  auto edge2 = get_edge(new_wave);
  std::vector<wtf> group1(edge1.size());
  std::vector<wtf> group2(edge2.size());
  for (size_t ind = 0; ind < wave.size(); ++ind)
    group1[ind] = {0, edge1[ind], flux[ind]};  // 0 indicates old wave edge
  if (endtype == 0)
    group1.back() = {0, edge1.back(), 0.0};  // the last edge flux should be 0
  else
    group1.back() = {0, edge1.back(), flux.back()};

  if (endtype == 0) {
    for (size_t ind = 0; ind < edge2.size(); ++ind)
      group2[ind] = {1, edge2[ind], 0.0};  // 1 indicates new wave edge
  } else {
    size_t ind = 0;
    while (edge2[ind] < edge1[0] && ind < edge2.size()) {
      group2[ind] = {1, edge2[ind], flux.front()};
      ++ind;
    }
    while (ind < edge2.size()) {
      group2[ind] = {1, edge2[ind], flux.back()};
      ++ind;
    }
  }
  auto sorted_data = ssort(group1, group2);
  for (size_t ind = 1; ind < sorted_data.size(); ++ind)
    if (sorted_data[ind].type == 1)
      sorted_data[ind].flux = sorted_data[ind - 1].flux;
  return sorted_data;
}

// don't worry about the efficiency, don't make things complex
DARR rebin_err(const DARR& wave, const DARR& err, const DARR& new_wave) {
  DARR new_err;
  // err.data();
  DARR nerr(err.begin(), err.end());
  double med_value = gsl_stats_median(nerr.data(), 0, err.size());
  if (med_value > 0){
    for(size_t ind = 0; ind < nerr.size(); ++ind) nerr[ind] /= med_value;
  }
  auto sorted_data = merge_edge(wave, nerr, new_wave);
  typedef decltype(sorted_data.begin()) ITR;
  std::vector<std::pair<ITR, ITR>> blocks;
  for (auto itr = sorted_data.begin(); itr != sorted_data.end(); ++itr)
    if (itr->type == 0)
      blocks.push_back({itr, itr});
  assert(wave.size() + 1 == blocks.size());
  for (auto btr = blocks.begin(); btr != blocks.end() - 1; ++btr)
    btr->second = (btr + 1)->first;
  blocks.pop_back();
  for (auto& block : blocks) {
    auto sa = block.second->wave - block.first->wave;
    auto err = block.first->flux;
    for (auto bitr = block.first; bitr != block.second; ++bitr) {
      double si = (bitr + 1)->wave - bitr->wave;
      bitr->flux = si * sa * err * err;
    }
  }
  blocks.clear();
  for (auto itr = sorted_data.begin(); itr != sorted_data.end(); ++itr)
    if (itr->type == 1)
      blocks.push_back({itr, itr});
  for (auto btr = blocks.begin(); btr != blocks.end() - 1; ++btr)
    btr->second = (btr + 1)->first;
  blocks.pop_back();
  for (auto& block : blocks) {
    auto sb = block.second->wave - block.first->wave;
    double err = 0;
    for (auto bitr = block.first; bitr != block.second; ++bitr)
      err += bitr->flux;
    err /= sb * sb;
    err = sqrt(err);
    new_err.push_back(err);
  }
  assert(new_err.size() == new_wave.size());
  for(auto itr = new_err.begin(); itr != new_err.end(); ++itr) *itr *= med_value;
  int ind = 0;
  while(new_wave[ind] < wave.front()){
    new_err[ind++] = std::numeric_limits<double>::infinity();
  }
  ind = new_wave.size() - 1;
  while(new_wave[ind] > wave.back()){
    new_err[ind--] = std::numeric_limits<double>::infinity();
  }
  return new_err;
}

DARR _rebin_proto(const DARR& wave,
                  const DARR& flux,
                  const DARR& new_wave,
                  int end_type) {
  auto sorted_data = merge_edge(wave, flux, new_wave, end_type);
  DARR new_flux;
  double tmpflux = 0, w1 = 0, w2 = 0;
  size_t ind = 0;
  while (sorted_data[ind].type != 1)
    ++ind;
  w1 = sorted_data[ind].wave;
  while (++ind != sorted_data.size()) {
    double length = sorted_data[ind].wave - sorted_data[ind - 1].wave;
    tmpflux += length * sorted_data[ind - 1].flux;
    if (sorted_data[ind].type == 1) {
      w2 = sorted_data[ind].wave;
      double realflux = tmpflux / (w2 - w1);
      new_flux.push_back(realflux);
      w1 = sorted_data[ind].wave;
      tmpflux = 0;
    }
  }
  return new_flux;
}

DARR rebin(const DARR& wave, const DARR& flux, const DARR& new_wave) {
  return _rebin_proto(wave, flux, new_wave, 0);
}

DARR rebin_padvalue(const DARR& wave, const DARR& flux, const DARR& new_wave) {
  return _rebin_proto(wave, flux, new_wave, 1);
}

py::array_t<double> numpy_rebin(const DARR& wave,
                                const DARR& flux,
                                const DARR& new_wave) {
  return VEC2numpyarr(rebin(wave, flux, new_wave));
}

py::array_t<double> numpy_rebin_padvalue(const DARR& wave,
                                         const DARR& flux,
                                         const DARR& new_wave) {
  return VEC2numpyarr(rebin_padvalue(wave, flux, new_wave));
}

py::array_t<double> numpy_rebin_err(const DARR& wave,
                                    const DARR& err,
                                    const DARR& new_wave) {
  return VEC2numpyarr(rebin_err(wave, err, new_wave));
}

PYBIND11_MODULE(rebin, m) {
  // xt::import_numpy();
  m.doc() = "rebin the spectrum and ensure the flux conservation";

  m.def("rebin", numpy_rebin,
        "rebin the input spectrum and ensure the input flux "
        "conservation\ninput par: wave, flux, new_wave\nCaution: \nthe "
        "wavelength out of the spectrum is filled with 0, like this\n ..., 0, "
        "0, 0, f1, ..., fn, 0, 0, 0, ...");
  m.def("rebin_padvalue", numpy_rebin_padvalue,
        "rebin the input spectrum and ensure the input flux "
        "conservation\ninput par: wave, flux, new_wave\nCaution: \nthe "
        "wavelength out of the spectrum is filled with the first and last "
        "value, like this\n ..., f1, f1, f1, f1, ..., fn, fn, fn, fn, ...");
  m.def("rebin_err", numpy_rebin_err,
        "rebin the spectrum err\n input par: wave, err, new_wave");
}