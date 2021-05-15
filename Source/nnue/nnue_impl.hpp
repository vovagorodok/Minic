#pragma once

#include "definition.hpp"

#ifdef WITH_NNUE

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>

#ifdef USE_AVX_INTRIN
#include "dot.hpp"
#endif

//#include "bfloat16.hpp"

#ifdef WITH_BLAS
#include <cblas.h>
#endif

// Initially taken from Seer implementation.
// see https://github.com/connormcmonigle/seer-nnue

#define NNUEALIGNMENT 64

#ifdef __clang__
#define CONSTEXPR 
#else
#define CONSTEXPR constexpr
#endif

#define INCBIN_PREFIX
#define INCBIN_STYLE INCBIN_STYLE_CAMEL
#include "incbin.h"

namespace nnue{

#ifdef EMBEDEDNNUEPATH
namespace embeded {
  INCBIN_EXTERN(weightsFile);
}
#endif

// quantization constantes

template<bool Q>
struct Quantization{
   typedef float WT;
   typedef float WIT;
   typedef float BT;
   typedef float BIT;   
   static constexpr float weightMax  {100}; // dummy value
   static constexpr int weightScale  {1};
   static constexpr int weightFactor {1};
   static constexpr int biasFactor   {1};
   static constexpr float outFactor  {1.f/600.f};
   static float round(const float &x){return x;}
};

template <>
struct Quantization<true>{
   typedef int16_t WIT;
   typedef int8_t WT;
   typedef int32_t BIT;  
   typedef int32_t BT;
   static constexpr float weightMax    {2.0f}; // supposed min/max of weights values
   static constexpr int weightScale    {std::numeric_limits<WT>::max()};
   static constexpr int weightFactor   {(int)(weightScale/weightMax)}; // quantization factor
   static constexpr int biasFactor     {weightScale*weightFactor};
   static constexpr float outFactor    {(weightFactor*weightFactor*weightMax)/600.f};
   static float round(const float & x){return std::round(x);}
};

template<bool Q>
inline void quantizationInfo(){
   if constexpr(Q){
      Logging::LogIt(Logging::logInfo) << "Quantization info :";
      Logging::LogIt(Logging::logInfo) << "weightMax    " <<  Quantization<true>::weightMax;
      Logging::LogIt(Logging::logInfo) << "weightScale  " <<  Quantization<true>::weightScale;
      Logging::LogIt(Logging::logInfo) << "weightFactor " <<  Quantization<true>::weightFactor;
      Logging::LogIt(Logging::logInfo) << "biasFactor   " <<  Quantization<true>::biasFactor;
      Logging::LogIt(Logging::logInfo) << "outFactor    " <<  Quantization<true>::outFactor;
   }
   else{
      Logging::LogIt(Logging::logInfo) << "No quantization, using float net";
   }
}

template<typename NT>
struct weights_streamer{
  std::istream * file = nullptr;
  
  weights_streamer<NT>& read_version(uint32_t & version){
     assert(file);
     file->read((char *) &version, sizeof(uint32_t));
     return *this;
  }

  template<typename T, bool Q>
  weights_streamer<NT>& streamW(T* dst, const size_t request, size_t dim0, size_t dim1){
    assert(file);
    NT minW = std::numeric_limits<NT>::max();
    NT maxW = std::numeric_limits<NT>::min();
    std::array<char, sizeof(NT)> single_element{};
    const NT Wscale = Quantization<Q>::weightFactor;
    Logging::LogIt(Logging::logInfo) << "Reading inner weight";
    for(size_t i(0); i < request; ++i){
      file->read(single_element.data(), single_element.size());
      NT tmp{0};
      std::memcpy(&tmp, single_element.data(), single_element.size());
      minW = std::min(minW,tmp);
      maxW = std::max(maxW,tmp);
      if ( Q && std::abs(tmp*Wscale) > (NT)std::numeric_limits<T>::max()){
        NT tmp2 = tmp;
        tmp = std::clamp(tmp2*Wscale,(NT)std::numeric_limits<T>::min(),(NT)std::numeric_limits<T>::max());
        Logging::LogIt(Logging::logWarn) << "Overflow weight " << tmp2 << " -> " << (int)tmp;
      }
      else{
        tmp = tmp*Wscale;
      }
#if (defined USE_AVX_INTRIN) || (defined WITH_BLAS)
      // transpose data
      const size_t j = (i%dim1)*dim0 + i/dim1;
#else
      const size_t j=i;
#endif
      dst[j] = T(Quantization<Q>::round(tmp));
    }
    Logging::LogIt(Logging::logInfo) << "Weight in [" << minW << ", " << maxW << "]";
    return *this;
  }

  template<typename T, bool Q>
  weights_streamer<NT> & streamWI(T* dst, const size_t request){
    assert(file);
    NT minW = std::numeric_limits<NT>::max();
    NT maxW = std::numeric_limits<NT>::min();
    std::array<char, sizeof(NT)> single_element{};
    const NT Wscale = Quantization<Q>::weightScale;
    Logging::LogIt(Logging::logInfo) << "Reading input weight";
    for(size_t i(0); i < request; ++i){
      file->read(single_element.data(), single_element.size());
      NT tmp{0};
      std::memcpy(&tmp, single_element.data(), single_element.size());
      minW = std::min(minW,tmp);
      maxW = std::max(maxW,tmp);
      if ( Q && std::abs(tmp*Wscale) > (NT)std::numeric_limits<T>::max()){
        NT tmp2 = tmp;
        tmp = std::clamp(tmp2*Wscale,(NT)std::numeric_limits<T>::min(),(NT)std::numeric_limits<T>::max());
        Logging::LogIt(Logging::logWarn) << "Overflow weight " << tmp2 << " -> " << (int)tmp;
      }
      else{
        tmp = tmp*Wscale;
      }
      dst[i] = T(Quantization<Q>::round(tmp));
    }
    Logging::LogIt(Logging::logInfo) << "Weight in [" << minW << ", " << maxW << "]";
    return *this;
  }

  template<typename T, bool Q>
  weights_streamer<NT> & streamB(T* dst, const size_t request){
    assert(file);
    NT minB = std::numeric_limits<NT>::max();
    NT maxB = std::numeric_limits<NT>::min();
    std::array<char, sizeof(NT)> single_element{};
    const NT Bscale = Quantization<Q>::biasFactor;
    Logging::LogIt(Logging::logInfo) << "Reading inner bias";
    for(size_t i(0); i < request; ++i){
      file->read(single_element.data(), single_element.size());
      NT tmp{0};
      std::memcpy(&tmp, single_element.data(), single_element.size());
      minB = std::min(minB,tmp);
      maxB = std::max(maxB,tmp);
      if ( Q && std::abs(tmp*Bscale) > (NT)std::numeric_limits<T>::max()){
         NT tmp2 = tmp;
         tmp = std::clamp(tmp2*Bscale,(NT)std::numeric_limits<T>::min(),(NT)std::numeric_limits<T>::max());
         Logging::LogIt(Logging::logWarn) << "Overflow bias " << tmp2 << " -> " << (int)tmp;
      }
      else{
        tmp = tmp*Bscale;
      }      
      dst[i] = T(Quantization<Q>::round(tmp));
    }
    Logging::LogIt(Logging::logInfo) << "Bias in [" << minB << ", " << maxB << "]";
    return *this;
  }

  template<typename T, bool Q>
  weights_streamer<NT> & streamBI(T* dst, const size_t request){
    assert(file);
    NT minB = std::numeric_limits<NT>::max();
    NT maxB = std::numeric_limits<NT>::min();
    std::array<char, sizeof(NT)> single_element{};
    const NT Bscale = Quantization<Q>::weightScale;
    Logging::LogIt(Logging::logInfo) << "Reading input bias";
    for(size_t i(0); i < request; ++i){
      file->read(single_element.data(), single_element.size());
      NT tmp{0};
      std::memcpy(&tmp, single_element.data(), single_element.size());
      minB = std::min(minB,tmp);
      maxB = std::max(maxB,tmp);
      if ( Q && std::abs(tmp*Bscale) > (NT)std::numeric_limits<T>::max()){
         NT tmp2 = tmp;
         tmp = std::clamp(tmp2*Bscale,(NT)std::numeric_limits<T>::min(),(NT)std::numeric_limits<T>::max());
         Logging::LogIt(Logging::logWarn) << "Overflow bias " << tmp2 << " -> " << (int)tmp;
      }
      else{
        tmp = tmp*Bscale;
      }      
      dst[i] = T(Quantization<Q>::round(tmp));
    }
    Logging::LogIt(Logging::logInfo) << "Bias in [" << minB << ", " << maxB << "]";
    return *this;
  }  

  weights_streamer(std::istream& stream) : file(&stream) {}
};

#ifdef WITH_NNUE_CLIPPED_RELU
template<typename T, bool Q>
inline constexpr T activationInput(const T& x){ return std::min(std::max(T(x), T{0}), T{Quantization<Q>::weightScale}); }

template<typename T, bool Q>
inline constexpr T activation(const T& x){ return std::min(std::max(T(x/Quantization<Q>::weightFactor), T{0}), T{Quantization<Q>::weightScale}); }

template<typename T, bool Q>
inline constexpr T activationQSingleLayer(const T& x){ return std::min(std::max(T(x), T{0}), T{Quantization<Q>::weightFactor}); }

#else // standard relu (not clipped)

template<typename T, bool Q>
inline constexpr typename std::enable_if<!Q,T>::type
activation(const T& x){ return std::max(T(x), T{0});}

#define activationInput activation
#define activationQSingleLayer activation

#endif

template<typename T, size_t dim>
struct stack_vector{

#ifdef DEBUG_NNUE_UPDATE  
  std::array<T,dim> data;
  
  bool operator==(const stack_vector<T,dim> & other){
    static const T eps = std::numeric_limits<T>::epsilon() * 100;
    for (size_t i = 0 ; i < dim ; ++i){
       if ( std::fabs(data[i] - other.data[i]) > eps ){
         std::cout << data[i] << "!=" << other.data[i] << std::endl;
         return false;
       }
    }
    return true;
  }

  bool operator!=(const stack_vector<T,dim> & other){
    static const T eps = std::numeric_limits<T>::epsilon() * 100;
    for (size_t i = 0 ; i < dim ; ++i){
       if ( std::fabs(data[i] - other.data[i]) > eps ){
         std::cout << data[i] << "!=" << other.data[i] << std::endl;
         return true;
       }
    }
    return false;
  }
#else
  alignas(NNUEALIGNMENT) T data[dim];
#endif

/*
  template<typename F>
  constexpr stack_vector<T, dim> apply(F&& f) const {
    return stack_vector<T, dim>{*this}.apply_(std::forward<F>(f));
  }
*/
  
  template<typename F>
  CONSTEXPR stack_vector<T, dim>& apply_(F&& f){
    #pragma omp simd
    for(size_t i = 0; i < dim; ++i){
      data[i] = f(data[i]);
    }
    return *this;
  }

  template <typename T2>
  CONSTEXPR stack_vector<T, dim>& add_(const T2* other){
    #pragma omp simd
    for(size_t i = 0; i < dim; ++i){
      data[i] += other[i];
    }
    return *this;
  }
  
  template <typename T2>
  CONSTEXPR stack_vector<T, dim>& sub_(const T2* other){
    #pragma omp simd
    for(size_t i = 0; i < dim; ++i){
      data[i] -= other[i];
    }
    return *this;
  }
  
  template <typename T2, typename T3>
  CONSTEXPR stack_vector<T, dim>& fma_(const T2 c, const T3* other){
    #pragma omp simd
    for(size_t i = 0; i < dim; ++i){
      data[i] += c * other[i];
    }
    return *this;
  }
  
#ifdef USE_AVX_INTRIN
  inline T dot_(const T* other) const {
    return dotProductFma<T,dim>(data, other); 
  }
#endif

  constexpr T item() const {
    static_assert(dim == 1, "called item() on vector with dim != 1");
    return data[0];
  }
  
  static CONSTEXPR stack_vector<T, dim> zeros(){
    stack_vector<T, dim> result{};
    #pragma omp simd
    for(size_t i = 0; i < dim; ++i){
      result.data[i] = T(0);
    }
    return result; // RVO
  }
  
  template <typename T2>
  static CONSTEXPR stack_vector<T, dim> from(const T2* data){
    stack_vector<T, dim> result{};
    #pragma omp simd
    for(size_t i = 0; i < dim; ++i){
      result.data[i] = T(data[i]);
    }
    return result; //RVO
  }  

};

template<typename T, size_t dim>
std::ostream& operator<<(std::ostream& ostr, const stack_vector<T, dim>& vec){
  static_assert(dim != 0, "can't stream empty vector.");
  ostr << "stack_vector<T, " << dim << ">([";
  for(size_t i = 0; i < (dim-1); ++i){
    ostr << vec.data[i] << ", ";
  }
  ostr << vec.data[dim-1] << "])";
  return ostr;
}

template<typename T, size_t dim0, size_t dim1>
CONSTEXPR stack_vector<T, dim0 + dim1> splice(const stack_vector<T, dim0>& a, const stack_vector<T, dim1>& b){
  auto c = stack_vector<T, dim0 + dim1>::zeros();
  #pragma omp simd
  for(size_t i = 0; i < dim0; ++i){
    c.data[i] = a.data[i];
  }
  #pragma omp simd  
  for(size_t i = 0; i < dim1; ++i){
    c.data[dim0 + i] = b.data[i];
  }
  return c; // RVO
}

template<typename NT, size_t dim0, size_t dim1, bool Q>
struct stack_affine{
  static constexpr size_t W_numel = dim0*dim1;
  static constexpr size_t b_numel = dim1;
  
  typedef typename Quantization<Q>::BT BT;
  typedef typename Quantization<Q>::BIT BIT;
  typedef typename Quantization<Q>::WT WT;

  // dirty thing here, stack_affine is always for inner layer
  alignas(NNUEALIGNMENT) WT W[W_numel];
  alignas(NNUEALIGNMENT) BT b[b_numel];
  
  CONSTEXPR stack_vector<BT, dim1> forward(const stack_vector<BT, dim0>& x) const {
    auto result = stack_vector<BT, dim1>::from(b);
#ifndef WITH_BLAS
    #pragma omp simd
#ifdef USE_AVX_INTRIN
    for(size_t i = 0; i < dim1; ++i){
      result.data[i] += x.dot_(W + i * dim0);
    }
#else
    for(size_t i = 0; i < dim0; ++i){
      result.fma_(x.data[i], W + i * dim1);
    }
#endif // USE_AVX_INTRIN
#else
    static_assert(std::is_same<BT, float>::value, "only works with float");
    cblas_sgemv(CblasRowMajor, CblasNoTrans, dim1, dim0, 1, W, dim0, x.data, 1, 1, result.data, 1);
#endif
    return result;
  }
  
  ///@todo forward with return type of next layer
  template<typename T>
  CONSTEXPR stack_vector<BT, dim1> forward(const stack_vector<T, dim0>& x) const {
    auto result = stack_vector<BT, dim1>::from(b);
#ifndef WITH_BLAS
    #pragma omp simd
#ifdef USE_AVX_INTRIN
    for(size_t i = 0; i < dim1; ++i){
      result.data[i] += x.dot_(W + i * dim0);
    }
#else
    for(size_t i = 0; i < dim0; ++i){
      result.fma_(x.data[i], W + i * dim1);
    }
#endif // USE_AVX_INTRIN
#else
    static_assert(std::is_same<BT, float>::value, "only works with float");
    cblas_sgemv(CblasRowMajor, CblasNoTrans, dim1, dim0, 1, W, dim0, x.data, 1, 1, result.data, 1);
#endif
    return result; // RVO
  }

  stack_affine<NT, dim0, dim1, Q>& load_(weights_streamer<NT>& ws){
    ws.template streamW<WT, Q>(W, W_numel,dim0,dim1).template streamB<BT, Q>(b, b_numel);
    return *this;
  }
};

template<typename NT, size_t dim0, size_t dim1, bool Q>
struct big_affine{
  static constexpr size_t W_numel = dim0*dim1;
  static constexpr size_t b_numel = dim1;

  typedef typename Quantization<Q>::BIT BIT;
  typedef typename Quantization<Q>::WIT WIT;

  // dirty thing here, big_affine is always for input layer
  typename Quantization<Q>::WIT* W{nullptr};
  alignas(NNUEALIGNMENT) typename Quantization<Q>::BIT b[b_numel];

  void insert_idx(const size_t idx, stack_vector<BIT, b_numel>& x) const {
    const WIT* mem_region = W + idx * dim1;
    x.add_(mem_region);
  }
  
  void erase_idx(const size_t idx, stack_vector<BIT, b_numel>& x) const {
    const WIT* mem_region = W + idx * dim1;
    x.sub_(mem_region);
  }

  big_affine<NT, dim0, dim1, Q>& load_(weights_streamer<NT>& ws){
    ws.template streamWI<WIT, Q>(W, W_numel).template streamBI<BIT, Q>(b, b_numel);
    return *this;
  }

  big_affine<NT, dim0, dim1, Q>& operator=(const big_affine<NT, dim0, dim1, Q>& other){
    #pragma omp simd
    for(size_t i = 0; i < W_numel; ++i){ W[i] = other.W[i]; }
    #pragma omp simd
    for(size_t i = 0; i < b_numel; ++i){ b[i] = other.b[i]; }
    return *this;
  }

  big_affine<NT, dim0, dim1, Q>& operator=(big_affine<NT, dim0, dim1, Q>&& other){
    std::swap(W, other.W);
    std::swap(b, other.b);
    return *this;
  }

  big_affine(const big_affine<NT, dim0, dim1, Q>& other){
    W = new WIT[W_numel];
    #pragma omp simd
    for(size_t i = 0; i < W_numel; ++i){ W[i] = other.W[i]; }
    #pragma omp simd
    for(size_t i = 0; i < b_numel; ++i){ b[i] = other.b[i]; }
  }

  big_affine(big_affine<NT, dim0, dim1, Q>&& other){
    std::swap(W, other.W);
    std::swap(b, other.b);
  }
  
  big_affine(){ W = new WIT[W_numel]; }

  ~big_affine(){ if(W != nullptr){ delete[] W; } }
};

constexpr size_t half_ka_numel = 12*64*64;
constexpr size_t base_dim = 128;

template<typename NT, bool Q>
struct half_kp_weights{
  big_affine  <NT, half_ka_numel, base_dim, Q> w{};
  big_affine  <NT, half_ka_numel, base_dim, Q> b{};
  stack_affine<NT, 2*base_dim   , 32      , Q> fc0{};
  stack_affine<NT, 32           , 32      , Q> fc1{};
  stack_affine<NT, 64           , 32      , Q> fc2{};
  stack_affine<NT, 96           ,  1      , Q> fc3{};
  uint32_t version = 0;

  half_kp_weights<NT,Q>& load(weights_streamer<NT>& ws, bool readVersion){
    quantizationInfo<Q>();
    if ( readVersion ) ws.read_version(version);
    w.load_(ws);
    b.load_(ws);
    fc0.load_(ws);
    fc1.load_(ws);
    fc2.load_(ws);
    fc3.load_(ws);
    return *this;
  }
  
  static bool load(const std::string& path, half_kp_weights<NT,Q>& loadedWeights){
    static const uint32_t expectedVersion = 0xc0ffee00;
    static const int expectedSize = 50378504; // 50378500 + 4 for version
    static const bool withVersion = true;

    if ( path != "embeded"){ // read from disk
#ifndef __ANDROID__
#ifndef WITHOUT_FILESYSTEM 
      std::error_code ec;
      auto fsize = std::filesystem::file_size(path,ec);
      if ( ec ){
        Logging::LogIt(Logging::logError) << "File " << path << " is not accessible";
        return false;
      }
      if ( fsize != expectedSize ){ // with or without version
        Logging::LogIt(Logging::logError) << "File " << path << " does not look like a compatible net";
        return false;
      }
#endif
#endif
      std::fstream stream(path, std::ios_base::in | std::ios_base::binary);
      auto ws = weights_streamer<NT>(stream);
      loadedWeights.load(ws,withVersion);
    }
#ifdef EMBEDEDNNUEPATH
    else{ // read from embeded data
      if ( embeded::weightsFileSize != expectedSize ){ // with or without version
        Logging::LogIt(Logging::logError) << "File " << path << " does not look like a compatible net";
        return false;
      }
      std::istringstream stream(std::string((const char*)embeded::weightsFileData,embeded::weightsFileSize),std::stringstream::binary);
      auto ws = weights_streamer<NT>(stream);
      loadedWeights.load(ws,withVersion);
    }
#else
    else{
        Logging::LogIt(Logging::logError) << "Minic was not compiled with an embeded net";
        return false;
    }
#endif

    if ( withVersion ){
      Logging::LogIt(Logging::logInfo) << "Expected net version : " << toHexString(expectedVersion);
      Logging::LogIt(Logging::logInfo) << "Read net version     : " << toHexString(loadedWeights.version);
      if ( loadedWeights.version != expectedVersion ){
        Logging::LogIt(Logging::logError) << "File " << path << " is not a compatible version of the net";
        return false;
      }
    }
    return true;
  }
};

template<typename NT, bool Q>
struct feature_transformer{
  const big_affine<NT, half_ka_numel, base_dim, Q>* weights_;

  typedef typename Quantization<Q>::BIT BIT;
  typedef typename Quantization<Q>::WIT WIT;

  // dirty thing here, active_ is always for input layer
  stack_vector<BIT, base_dim> active_;

  constexpr stack_vector<BIT, base_dim> active() const { return active_; }

  void clear(){ 
    assert(weights_);
    active_ = stack_vector<BIT, base_dim>::from(weights_ -> b); 
  }

  void insert(const size_t idx){ 
    assert(weights_);
    weights_ -> insert_idx(idx, active_); 
  }

  void erase(const size_t idx){ 
    assert(weights_);
    weights_ -> erase_idx(idx, active_); 
  }

  feature_transformer(const big_affine<NT, half_ka_numel, base_dim, Q>* src) : weights_{src} {
    clear();
  }

  feature_transformer():weights_(nullptr){}

  ~feature_transformer(){}

#ifdef DEBUG_NNUE_UPDATE
  bool operator==(const feature_transformer<T,Q> & other){
    return active_ == other.active_;
  }

  bool operator!=(const feature_transformer<T,Q> & other){
    return active_ != other.active_;
  }
#endif

};

template<Color> struct them_{};

template<>
struct them_<Co_White>{
  static constexpr Color value = Co_Black;
};

template<>
struct them_<Co_Black>{
  static constexpr Color value = Co_White;
};

template<typename T, typename U>
struct sided{
  using return_type = U;
  
  T& cast(){ return static_cast<T&>(*this); }
  const T& cast() const { return static_cast<const T&>(*this); }
  
  template<Color c>
  return_type& us(){
    if constexpr(c == Co_White){ return cast().white; }
    else{ return cast().black; }
  }
  
  template<Color c>
  const return_type& us() const {
    if constexpr(c == Co_White){ return cast().white; }
    else{ return cast().black; }
  }
  
  template<Color c>
  return_type& them(){ return us<them_<c>::value>(); }

  template<Color c>
  const return_type& them() const { return us<them_<c>::value>(); }
  
 private:
  sided(){};
  friend T;
};

template<typename NT, bool Q>
struct half_kp_eval : sided<half_kp_eval<NT,Q>, feature_transformer<NT,Q>>{
  // common data (weights and bias)
  static half_kp_weights<NT,Q> weights; 
  // instance data (active index)
  feature_transformer<NT,Q> white;
  feature_transformer<NT,Q> black;

  void clear(){
    white.clear();
    black.clear();
  }

  typedef typename Quantization<Q>::BT BT;

  constexpr float propagate(Color c) const {
    const auto w_x = white.active();
    const auto b_x = black.active();
    const auto x0 = c == Co_White ? splice(w_x, b_x).apply_(activationInput<BT,Q>) : splice(b_x, w_x).apply_(activationInput<BT,Q>);
    //std::cout << "x0 " << x0 << std::endl;
    //const stack_vector<BT, 32> x1 = stack_vector<BT, 32>::from((weights.fc0).forward(x0).apply_(activationQSingleLayer<BT,true>).data,1.f/Quantization<Q>::weightFactor);
    const auto x1 = (weights.fc0).forward(x0).apply_(activation<BT,Q>);
    //std::cout << "x1 " << x1 << std::endl;
    const auto x2 = splice(x1, (weights.fc1).forward(x1).apply_(activation<BT,Q>));
    //std::cout << "x2 " << x2 << std::endl;
    const auto x3 = splice(x2, (weights.fc2).forward(x2).apply_(activation<BT,Q>));
    //std::cout << "x3 " << x3 << std::endl;
    const float val = (weights.fc3).forward(x3).item();    
    //std::cout << "val " << val / Quantization<Q>::outFactor << std::endl;
    return val / Quantization<Q>::outFactor;
  }

#ifdef DEBUG_NNUE_UPDATE
  bool operator==(const half_kp_eval<T> & other){
    if (white != other.white || black != other.black) 
       return false;
    return true;
  }

  bool operator!=(const half_kp_eval<T> & other){
       if (white != other.white || black != other.black) 
          return true;
    return false;
  }
#endif

  // default CTOR always use loaded weights
  half_kp_eval(){
    white = & weights.w;
    black = & weights.b;
  }
};

template<typename NT, bool Q>
half_kp_weights<NT,Q> half_kp_eval<NT,Q>::weights;

} // nnue

#endif // WITH_NNUE
