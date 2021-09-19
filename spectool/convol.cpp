#include "convol.h"
#include <gsl/gsl_sf_legendre.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <utility>
#include <tuple>
#include <deque>
#include <vector>
#include "pybind11/stl.h"
#include "types.h"
// #include "rebin.h"

// Please ensure -1 < arrx < 1
VEC legendre_poly(CVEC& arrx, CVEC& arrpar) {
  VEC arrresult(arrx.size());
  for (auto& val : arrresult)
    val = 0;
  double* buff = new double[arrpar.size()];
  double order = arrpar.size() - 1;
  for (size_t ind = 0; ind < arrx.size(); ++ind) {
    gsl_sf_legendre_Pl_array(order, arrx[ind], buff);
    double temp = 0;
    for (size_t nn = 0; nn < arrpar.size(); ++nn)
      temp += arrpar[nn] * buff[nn];
    arrresult[ind] = temp;
  }
  delete[] buff;
  return arrresult;
}

VEC poly(CVEC& arrx, CVEC& arrpar) {
  VEC arrret(arrx.size());
  for (auto& val : arrret)
    val = 0;
  for (size_t ind = 0; ind < arrpar.size(); ++ind) {
    double par = arrpar[ind];
    if (par != 0)
      for (size_t j = 0; j < arrret.size(); ++j) {
        double comp = par * pow(arrx[j], ind);
        arrret[j] += comp;
      }
  }
  return arrret;
}

// Dlambda = a0 + a1*lam + a2*lam^2 + a3*lam3 ...
// return lambda + Dlambda
VEC map_wave(CVEC& wave, CVEC& map_par) {
  VEC new_wave = poly(wave, map_par);
  for (size_t ind = 0; ind < new_wave.size(); ++ind)
    new_wave[ind] += wave[ind];
  return new_wave;
}

inline double gaussian(double x, double sigma, double x0) {
  // return 1/sqrt(2*M_PI)/sigma * exp(-(x-x0)*(x-x0)/(2*sigma*sigma));
  return exp(-(x - x0) * (x - x0) / (2 * sigma * sigma));
}

VEC gaussian(CVEC& arrx, double sigma, double x0) {
  VEC arrret(arrx.size());
  for (size_t ind = 0; ind < arrx.size(); ++ind) {
    double x = arrx[ind];
    arrret[ind] = gaussian(x, sigma, x0);
  }
  return arrret;
}

std::pair<int, int> gaussian2(CVEC& arrx,
                              double sigma,
                              double x0,
                              VEC& result,
                              size_t index_ref,
                              double threshold_ratio) {
  // double x_ref = arrx[index_ref];
  double val_ref = gaussian(x0, sigma, x0);
  double valtmp = val_ref;
  double threshold = val_ref * threshold_ratio;
  int indl = index_ref;
  result[indl] = valtmp;
  while (valtmp > threshold) {
    --indl;
    if (indl < 0) {
      indl = 0;
      break;
    }
    valtmp = gaussian(arrx[indl], sigma, x0);
    result[indl] = valtmp;
  };
  int indr = index_ref + 1;
  valtmp = val_ref;
  while (indr < arrx.size() && valtmp > threshold) {
    valtmp = gaussian(arrx[indr], sigma, x0);
    result[indr++] = valtmp;
  }
  return std::make_pair(indl, indr);
}

VEC _get_edge(CVEC& wave) {
  VEC interval(wave.size());
  std::adjacent_difference(wave.begin(), wave.end(), interval.begin());
  interval[0] = interval[1];
  for (auto& val : interval)
    val *= 0.5;
  VEC edge_out(wave.size() + 1);
  for (size_t ind = 0; ind < wave.size(); ++ind)
    edge_out[ind] = wave[ind] - interval[ind];
  edge_out.back() = wave.back() + interval.back();
  return edge_out;
}

VEC gauss_filter_mutable(CVEC& wave,
                         CVEC& flux,
                         CVEC& arrvelocity) {
  const double c = 299792458.0 / 1000;
  const double sigma_l = arrvelocity.front() / 2.355 * wave.front() / c;
  const double sigma_r = arrvelocity.back() / 2.355 * wave.back() / c;
  double delta_w1 = *(wave.begin() + 1) - *(wave.begin());
  double delta_w2 = *(wave.rbegin()) - *(wave.rbegin() + 1);
  int left_margin = (int)(5 * sigma_l / delta_w1);
  int right_margin = (int)(5 * sigma_r / delta_w2);
  VEC newave, newflux, arrsigma;
  double wtmp = wave.front() - left_margin * delta_w1;
  for (int i = 0; i < left_margin; ++i) {
    newave.push_back(wtmp);
    newflux.push_back(flux.front());
    double sigma = arrvelocity.front() / 2.355 * wtmp / c;
    arrsigma.push_back(sigma);
    wtmp += delta_w1;
  }
  for (int i = 0; i < wave.size(); ++i) {
    newave.push_back(wave[i]);
    newflux.push_back(flux[i]);
    double sigma = arrvelocity[i] / 2.355 * wave[i] / c;
    arrsigma.push_back(sigma);
  }
  wtmp = wave.back();
  for (int i = 0; i < right_margin; ++i) {
    wtmp += delta_w2;
    newave.push_back(wtmp);
    newflux.push_back(flux.back());
    double sigma = arrvelocity.back() / 2.355 * wtmp / c;
    arrsigma.push_back(sigma);
  }
  VEC gauss_profile(newave.size());
  VEC new_flux(newave.size());
  for (auto& val : new_flux)
    val = 0;
  VEC arredge = _get_edge(newave);
  VEC arrwidth;
  for (size_t d = 0; d < arredge.size() - 1; ++d)
    arrwidth.push_back(arredge[d + 1] - arredge[d]);
  for (size_t ind = 0; ind < newave.size(); ++ind) {
    double sigma = arrsigma[ind];
    double w0 = newave[ind];
    double mf = newflux[ind] * arrwidth[ind];
    auto indlr = gaussian2(newave, sigma, w0, gauss_profile, ind, 1.0e-5);
    const int indl = indlr.first;
    const int indr = indlr.second;
    double area = 0;
    for (size_t j = indl; j < indr; ++j)
      area += arrwidth[j] * gauss_profile[j];
    double inv_area = 1.0 / area;
    for (size_t j = indl; j < indr; ++j) {
      new_flux[j] += mf * (gauss_profile[j] * inv_area);
      gauss_profile[j] = 0;
    }
  }
  VEC outflux;
  for (int i = left_margin; i < left_margin + wave.size(); ++i)
    outflux.push_back(new_flux[i]);
  return outflux;
}

VEC _get_width(CVEC& wave){
  VEC out(wave.size());
  for(int ind = 1; ind < wave.size() - 1; ++ind){
    out[ind] = (wave[ind+1] - wave[ind-1]) / 2.0;
  }
  out[0] = wave[1] - wave[0];
  const int last = wave.size() - 1;
  out[last] = wave[last] - wave[last-1];
  return out;
}

void inline to_zero(VEC & arr){
  for(auto & val : arr)
    val = 0;
}

void inline vel_to_lambda(CVEC & velocity, const double w, VEC & lambda_arr){
  for(int ind = 0; ind < velocity.size(); ++ind){
    double myvel = velocity[ind];
    double delta_w = myvel / C_VAC * w;
    lambda_arr[ind] = w + delta_w;
  }
}

template<typename IT, typename IT2>
double inline _sum_flux(IT iflux, IT iflux_end, IT2 iwidth){
  double out = 0;
  while (iflux != iflux_end)
  {
    out += *(iflux++) * (*(iwidth++));
  }
  return out;
}

auto add_margin(CVEC& wave, CVEC& flux, CVEC& velocity){
  const double delta_w = (wave.back() - wave.front()) / wave.size();
  const double vl = velocity.front();
  const double vr = velocity.back();
  const double delta_wl = 3 * vl / C_VAC * wave.front();
  const double delta_wr = 3 * vr / C_VAC * wave.back();
  double wl = wave.front() + delta_wl;
  double wr = wave.back() + delta_wr;
  if(wl < 0){
    double siz = std::ceil(-wl/delta_w) + 1;
    wl += siz * delta_w;
  }
  std::deque<double> new_wave(wave.begin(), wave.end());
  std::deque<double> new_flux(flux.begin(), flux.end());
  int left_margin = 0;
  while(new_wave.front() > wl){
    left_margin++;
    new_wave.push_front(new_wave.front() - delta_w);
    new_flux.push_front(new_flux.front());
  }
  while(new_wave.back() < wr){
    new_wave.push_back(new_wave.back() + delta_w);
    new_flux.push_back(new_flux.back());
  }
  VEC out_wave(new_wave.begin(), new_wave.end());
  VEC out_flux(new_flux.begin(), new_flux.end());
  return std::make_tuple(out_wave, out_flux, left_margin);
}

auto get_int_profile(CVEC& wave, CVEC& flux){
  VEC out_wave(flux.size()+1);
  for(int ind = 0; ind < wave.size(); ++ind) out_wave[ind+1] = wave[ind];
  out_wave[0] = 2*wave[0] - wave[1];
  VEC out_flux(flux.size()+1);
  out_flux[0] = 0;
  for(int ind = 0; ind < flux.size(); ++ind) out_flux[ind+1] = (out_wave[ind+1] - out_wave[ind]) * flux[ind];
  for(int ind = 1; ind < out_flux.size(); ++ind) out_flux[ind] += out_flux[ind-1];
  return std::make_tuple(out_wave, out_flux);
}

// template<typename IT>
// void print(IT first, IT end){
//   for (auto it = first; it != end; ++it)
//     std::cout << *it << " ";
//   std::cout << std::endl;
// }

template<typename IT1, typename IT2>
void myinterp(IT1 iw1, IT1 if1, int size1, IT1 iw2, IT2 if2, int size2){
  auto iw1_end = iw1 + size1;
  auto if1_end = if1 + size1;
  auto iw2_end = iw2 + size2;
  auto if2_end = if2 + size2;

  // print(iw1, iw1_end);
  // std::cout << "size1 = " << size1 << std::endl;
  // print(if1, if1_end);
  // std::cout << "size2 = " << size2 << std::endl;

  const double w1_first = iw1[0];
  const double w1_last = iw1[size1-1];
  const double f1_first = if1[0];
  const double f1_last = if1[size1-1];

  const double left_width = iw2[1] - iw2[0];
  const double left_edge = iw2[0] - left_width;
  double left_flux;
  if(left_edge < *iw1) left_flux = 0;
  else if (left_edge >= w1_last) left_flux = f1_last;
  else{
    auto myfrom = lower_bound(iw1, iw1_end, left_edge);
    int ss = myfrom - iw1;
    double slop = (if1[ss+1] - if1[ss]) / (iw1[ss+1] - iw1[ss]);
    double length = left_edge - iw1[ss];
    left_flux = if1[ss] + slop * length;
  }

  int ind = 0;
  auto iw = iw1;
  auto iff = if1;
  // std::cout << "flag 3" << std::endl;
  while(iw2[ind] < w1_first && ind < size2) if2[ind++] = 0;
  while(iw2[ind] < w1_last && ind < size2){
    while(*(iw+1) < iw2[ind]) {iw++; iff++;}
    double slope = (*(iff+1) - *iff) / (*(iw+1) - *iw);
    double length = iw2[ind] - *iw;
    double tmpflux = *iff + slope*length;
    if2[ind] = tmpflux;
    ind++;
  }
  while(ind < size2) if2[ind++] = f1_last;

  // std::cout << "flag 4" << std::endl;

  auto new_iw = iw2_end - 1;
  for(auto itr = if2_end - 1; itr != if2; --itr){
    *itr -= *(itr-1);
    double width = *new_iw - *(new_iw - 1);
    // std::cout << "width = " << width << std::endl;
    *itr /= width;
    new_iw--;
  }
  if2[0] -= left_flux;
  if2[0] /= left_width;
  // std::cout << "end flag" << std::endl;
}

// void print(CVEC& arr){
//   for (auto val : arr){
//     std::cout << val << " ";
//   }
//   std::cout << std::endl;
// }

auto rebin_special(CVEC& wave, CVEC& intflux, CVEC& new_wave, VEC& new_flux){
  auto iw1 = wave.begin();
  auto if1 = intflux.begin();
  int size1 = wave.size();
  auto iw2 = lower_bound(new_wave.begin(), new_wave.end(), wave.front());
  auto iw2_end = upper_bound(new_wave.begin(), new_wave.end(), wave.back());
  int size2 = iw2_end - iw2;
  int first_num = iw2 - new_wave.begin();
  auto if2 = new_flux.begin() + first_num;
  // std::cout << "first_num = " << first_num << " first_num+size2 = " << first_num+size2 <<std::endl;
  myinterp(iw1, if1, size1, iw2, if2, size2);
  return std::make_tuple(first_num, first_num+size2);
}

VEC filter_use_given_profile(CVEC& wave, CVEC& flux, CVEC& velocity, CVEC& profile){
  auto [new_wave, new_flux, left_margin] = add_margin(wave, flux, velocity);
  // print(new_wave);
  // print(new_flux);
  auto [velocity_wle, intflux] = get_int_profile(velocity, profile);
  // new_wave = wave;
  // new_flux = flux;
  // left_margin = 0;
  VEC out(new_flux.size());
  VEC outprofile(new_flux.size());
  VEC lambda_arr(velocity_wle.size());
  CVEC width_VEC = _get_width(new_wave);
  to_zero(out);

  for(int ind=0; ind< new_wave.size(); ++ind){
    double w = new_wave[ind];
    vel_to_lambda(velocity_wle, w, lambda_arr);
    // VEC outprofile = rebin(lambda_arr, profile, new_wave);
    // std::cout << "flag 1" << std::endl;
    auto [ind_begin, ind_end] = rebin_special(lambda_arr, intflux, new_wave, outprofile);
    // std::cout << "flag 2" << std::endl;
    double sumflux = _sum_flux(outprofile.begin()+ind_begin, outprofile.begin()+ind_end, width_VEC.begin()+ind_begin);
    if (sumflux != 0){
      double intflux = width_VEC[ind] * new_flux[ind];
      for(int ind_tmp = ind_begin; ind_tmp < ind_end; ++ind_tmp) outprofile[ind_tmp] *= intflux/sumflux;
      for(int ind_tmp = ind_begin; ind_tmp < ind_end; ++ind_tmp) out[ind_tmp] += outprofile[ind_tmp];
    }
  }
  auto from = out.begin() + left_margin;
  auto end = from + wave.size();
  VEC new_out(from, end);
  return new_out;
}

// VEC filter_use_given_profile(CVEC& wave, CVEC& flux, CVEC& velocity, CVEC& profile){
//   auto [new_wave, new_flux, left_margin] = add_margin(wave, flux, velocity);
//   // new_wave = wave;
//   // new_flux = flux;
//   // left_margin = 0;
//   VEC out(new_flux.size());
//   VEC lambda_arr(velocity.size());
//   CVEC width_VEC = _get_width(new_wave);
//   to_zero(out);
//   for(int ind=0; ind< new_wave.size(); ++ind){
//     double w = new_wave[ind];
//     vel_to_lambda(velocity, w, lambda_arr);
//     VEC outprofile = rebin(lambda_arr, profile, new_wave);
//     double sumflux = _sum_flux(outprofile.begin(), outprofile.end(), width_VEC.begin());
//     if (sumflux == 0){
//       out[ind] = new_flux[ind];
//     } else {
//       double intflux = width_VEC[ind] * new_flux[ind];
//       for(auto & val : outprofile) val *= intflux/sumflux;
//       for(int j = 0; j < out.size(); ++j) out[j] += outprofile[j];
//     }
//   }
//   auto from = out.begin() + left_margin;
//   auto end = from + wave.size();
//   VEC new_out(from, end);
//   return new_out;
// }


auto add_cap(CVEC& wave, CVEC& flux, double sigma){
  std::deque<double> nwave(wave.begin(), wave.end());
  std::deque<double> nflux(flux.begin(), flux.end());
  const double wleft = wave.front() - 2 * sigma;
  const double wright = wave.back() + 2 * sigma;
  double dw = (wave.back() - wave.front()) / wave.size();
  double winputleft = wave.front() - dw;
  double finputleft = flux.front();
  int indfrom = 0;
  while(winputleft > wleft){
    nwave.push_front(winputleft);
    nflux.push_front(finputleft);
    winputleft -= dw;
    indfrom++;
  }
  double winputright = wave.back() + dw;
  double finputright = flux.back();
  while(winputright < wright){
    nwave.push_back(winputright);
    nflux.push_back(finputright);
    winputright += dw;
  }
  VEC wout(nwave.begin(), nwave.end());
  VEC fout(nflux.begin(), nflux.end());
  return std::make_tuple(wout, fout, indfrom);
}

VEC _gauss_filter_wavespace(CVEC& wave, CVEC& flux, double sigma);

VEC gauss_filter_wavespace(CVEC& wave, CVEC& flux, double sigma){
  auto [nwave, nflux, indfrom] = add_cap(wave, flux, sigma);
  auto out = _gauss_filter_wavespace(nwave, nflux, sigma);
  VEC nout(out.begin() + indfrom, out.begin() + indfrom + wave.size());
  return nout;
}

VEC _gauss_filter_wavespace(CVEC& wave, CVEC& flux, double sigma){
  VEC out(wave.size());
  for(int ind = 0; ind < out.size(); ++ind) out[ind] = 0;
  const double accflux = accumulate(flux.begin(), flux.end(), 0);
  const double unit = accflux / wave.size();
  VEC nflux(flux);
  // normalization to improve the result accuracy
  for(int ind = 0; ind < wave.size(); ++ind) nflux[ind] /= unit;
  VEC widthlst(wave.size());
  std::adjacent_difference(wave.begin(), wave.end(), widthlst.begin());
  widthlst[0] = widthlst[1];
  VEC profilelst(wave.size());
  int ind1, ind2;
  const double cutoff_v = 1.0e-5;
  for(int ind = 0; ind < wave.size(); ++ind){
    double w = wave[ind];
    for(int j = ind; j >= 0; --j){
      double wj = wave[j];
      double length = w - wj;
      double length2 = length * length;
      double valj = exp(-length2 / (2*sigma*sigma));
      profilelst[j] = valj;
      ind1 = j;
      if (valj < cutoff_v) break;
      if(j == 0) break;
    }
    for(int j = ind + 1; j <= wave.size(); ++j){
      ind2 = j;
      if(j == wave.size()) break;
      double wj = wave[j];
      double length = wj - w;
      double length2 = length * length;
      double valj = exp(-length2 / (2*sigma*sigma));
      profilelst[j] = valj;
      if(valj < cutoff_v) {j++; break;}
    }
    double sumflux = 0;
    for(int j = ind1; j < ind2; ++j) sumflux += profilelst[j] * widthlst[j];
    double ratio = 1 / sumflux;
    for(int j = ind1; j < ind2; ++j) profilelst[j] *= ratio;
    for(int j = ind1; j < ind2; ++j) out[j] += profilelst[j] * widthlst[j] * nflux[ind];
  }
  for(int ind = 0; ind < wave.size(); ++ind) out[ind] *= unit;
  return out;
}

// a gaussian filter for spectrum, the sigma of gaussians can be different in
// different wave, the sigma is defined as sigma = par0 + par1*wave +
// par2*wave^2 ... return the fluxes after smooth
VEC gauss_filter(CVEC& wave, CVEC& flux, CVEC& arrpar) {
  VEC arrsigma = poly(wave, arrpar);
  // adjust for boundry condition
  auto left_sigma = arrsigma.front();
  auto right_sigma = arrsigma.back();
  double delta_w1 = *(wave.begin() + 1) - *(wave.begin());
  double delta_w2 = *(wave.rbegin()) - *(wave.rbegin() + 1);
  int left_margin = (int)(5 * left_sigma / delta_w1);
  int right_margin = (int)(5 * right_sigma / delta_w2);
  VEC newave, newflux;
  double wtmp = wave.front() - left_margin * delta_w1;
  for (int i = 0; i < left_margin; ++i) {
    newave.push_back(wtmp);
    newflux.push_back(flux.front());
    wtmp += delta_w1;
  }
  for (int i = 0; i < wave.size(); ++i) {
    newave.push_back(wave[i]);
    newflux.push_back(flux[i]);
  }
  wtmp = wave.back();
  for (int i = 0; i < right_margin; ++i) {
    wtmp += delta_w2;
    newave.push_back(wtmp);
    newflux.push_back(flux.back());
  }

  arrsigma = poly(newave, arrpar);
  VEC gauss_profile(newave.size());
  VEC new_flux(newave.size());
  for (auto& val : new_flux)
    val = 0;
  VEC arredge = _get_edge(newave);
  VEC arrwidth;
  for (size_t d = 0; d < arredge.size() - 1; ++d)
    arrwidth.push_back(arredge[d + 1] - arredge[d]);
  for (size_t ind = 0; ind < newave.size(); ++ind) {
    double sigma = arrsigma[ind];
    double w0 = newave[ind];
    double mf = newflux[ind] * arrwidth[ind];
    auto indlr = gaussian2(newave, sigma, w0, gauss_profile, ind, 1.0e-5);
    const int indl = indlr.first;
    const int indr = indlr.second;
    double area = 0;
    for (size_t j = indl; j < indr; ++j)
      area += arrwidth[j] * gauss_profile[j];
    double inv_area = 1.0 / area;
    for (size_t j = indl; j < indr; ++j) {
      new_flux[j] += mf * (gauss_profile[j] * inv_area);
      gauss_profile[j] = 0;
    }
  }
  VEC outflux;
  for (int i = left_margin; i < left_margin + wave.size(); ++i)
    outflux.push_back(new_flux[i]);
  return outflux;
}

py::array_t<double> numpy_gauss_filter(CVEC& wave,
                                       CVEC& flux,
                                       CVEC& arrpar) {
  return VEC2numpyarr(gauss_filter(wave, flux, arrpar));
}

py::array_t<double> numpy_gauss_filter_wavespace(CVEC& wave, CVEC& flux, double sigma) {
  return VEC2numpyarr(gauss_filter_wavespace(wave, flux, sigma));
}

py::array_t<double> numpy_gauss_filter_mutable(CVEC& wave,
                                               CVEC& flux,
                                               CVEC& arrvelocity) {
  return VEC2numpyarr(gauss_filter_mutable(wave, flux, arrvelocity));
}

py::array_t<double> numpy_legendre_poly(CVEC& arrx, CVEC& arrpar) {
  return VEC2numpyarr(legendre_poly(arrx, arrpar));
}

py::array_t<double> numpy_filter_use_given_profile(CVEC& wave,
                                                   CVEC& flux,
                                                   CVEC& velocity,
                                                   CVEC& profile){
  return VEC2numpyarr(filter_use_given_profile(wave, flux, velocity, profile));
}

PYBIND11_MODULE(convol, m) {
  // xt::import_numpy();
  m.doc() = " ";

  m.def("gauss_filter", numpy_gauss_filter,
        "Smooth the input spectrum\ninput par: wave, flux, arrpar");
  m.def("gauss_filter_wavespace", numpy_gauss_filter_wavespace,
        "Smooth the input spectrum in wave space\n Input par: wave, flux, sigma(double)");
  m.def("gauss_filter_mutable", numpy_gauss_filter_mutable,
        "Smooth the input spectrum\ninput par: wave, flux, arrvelocity");
  m.def("legendre_poly", numpy_legendre_poly,
        "legendre polynomial function\ninput par: arrx, arrpar\nCaution: "
        "Please ensure -1 < arrx < 1!");
  m.def("filter_use_given_profile", numpy_filter_use_given_profile,
        "Smooth the input spectrum using the given kernel\n"
        "input par\n"
        "wave(numpy.ndarray): the wavelength of the spectrum\n"
        "flux(numpy.ndarray): the flux of spectrum\n"
        "velocity(numpy.ndarray): the velocity of the convolution kernel(in the unit of km/s)\n"
        "profile(numpy.darray): the profile of the convolution kernel"
        );
}