#ifndef CAFFE_SQUEEZE_LAYER_HPP_
#define CAFFE_SQUEEZE_LAYER_HPP_

#include <vector>

#include "caffe/blob.hpp"
#include "caffe/layer.hpp"
#include "caffe/proto/caffe.pb.h"

namespace caffe {

/*
 * @brief Remove the dimension of 1 (at the given dimension axis) of input's shape.
 *
 * The dimension index axis starts at zero;
 * if you specify a negative number for axis it is counted backward from the end.
 * If no axis is given, then all the shape=1 dimension will be removed.
 */

template <typename Dtype>
class SqueezeLayer : public Layer<Dtype> {
 public:
  explicit SqueezeLayer(const LayerParameter& param)
      : Layer<Dtype>(param) {}
  virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);

  virtual inline const char* type() const { return "Squeeze"; }
  virtual inline int ExactNumBottomBlobs() const { return 1; }
  virtual inline int ExactNumTopBlobs() const { return 1; }

 protected:

  virtual void Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top);

  virtual void Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom);
};

}  // namespace caffe

#endif  // CAFFE_SQUEEZE_LAYER_HPP_
