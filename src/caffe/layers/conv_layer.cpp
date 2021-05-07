#include <vector>

#include "caffe/layers/conv_layer.hpp"
#include "caffe/util/math_functions.hpp"
#define W this->blobs_[0]
#define B this->blobs_[1]

namespace caffe {

template <typename Dtype>
void ConvolutionLayer<Dtype>::compute_output_shape() {
  const int* kernel_shape_data = this->kernel_shape_.cpu_data();
  const int* stride_data = this->stride_.cpu_data();
  const int* pad_data = this->pad_.cpu_data();
  const int pad_type = this->pad_type_; //CUSTOMIZATION
  const int pad_l = this->pad_l_; //CUSTOMIZATION
  const int pad_r = this->pad_r_; //CUSTOMIZATION
  const int pad_t = this->pad_t_; //CUSTOMIZATION
  const int pad_b = this->pad_b_; //CUSTOMIZATION
  const int* dilation_data = this->dilation_.cpu_data();
  this->output_shape_.clear();
  for (int i = 0; i < this->num_spatial_axes_; ++i) {
    // i + 1 to skip channel axis
    const int input_dim = this->input_shape(i + 1);
    const int kernel_extent = dilation_data[i] * (kernel_shape_data[i] - 1) + 1;
    int output_dim;
    //<--CUSTOMIZATION
    if (pad_l!=0 || pad_r!=0 || pad_t!=0 || pad_b!=0){ //only support 2D
      if (i==0) {
        output_dim = (input_dim + pad_t + pad_b - kernel_extent) / stride_data[i] + 1;
      }
      if (i==1) {
        output_dim = (input_dim + pad_l + pad_r - kernel_extent) / stride_data[i] + 1;
      }
    }
    else{
      switch (pad_type) {
        case 0:
          output_dim = (input_dim + 2 * pad_data[i] - kernel_extent) / stride_data[i] + 1;
          break;
        case 1:
          output_dim = ceil(float(input_dim) / float(stride_data[i]));
          break;
        default:
          LOG(FATAL)<< "Unknown padding type.";
          break;
      }
    //CUSTOMIZATION-->
    }
    this->output_shape_.push_back(output_dim);
  }
}

template <typename Dtype>
void ConvolutionLayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  const double input_scale = this->input_scale_;
  const double output_scale = this->output_scale_;
  const double weight_scale = this->weight_scale_;
  //const double bias_scale = this->bias_scale_; // bias_scale = input_scale * weight_scale
  const int input_zero_point = this->input_zero_point_;
  const int output_zero_point = this->output_zero_point_;
  const int weight_zero_point = this->weight_zero_point_;
  const int bias_zero_point = this->bias_zero_point_;
  const Dtype saturate = this->saturate_;
  // weight/bias scale will retrieve the default values if per-channel is set
  const bool per_channel_scale_weight = this->per_channel_scale_weight_;
  const bool per_channel_scale_bias = this->per_channel_scale_bias_;
  /*** Quantization Computation
    (1) shift input/weight/bias w.r.t corresponding zero_point
    (2) compute Convolution+Bias on the integer value range
    (3) scale the output by input_scale*weight_scale/output_scale, and
    (4) shift the output by output_zero_point
  *Assumption is that bias_scale = input_scale*weight_scale
  For a floating-value model, only (2) is computed with floating values
  ***/
  const bool shift_input = (input_zero_point != 0);
  const bool shift_weight = (weight_zero_point != 0);
  const bool shift_bias = (bias_zero_point != 0); // the tensor has exactly 1 uniform zero_point
  const bool scale_output = (input_scale != Dtype(1.0) || weight_scale != Dtype(1.0) ||
                             output_scale != Dtype(1.0)) ||
                             per_channel_scale_weight || per_channel_scale_bias;
  const bool shift_output = (output_zero_point != 0);

  const int quant_num_ch = (per_channel_scale_weight && per_channel_scale_bias) ? this->num_output_ : 1;
  const Dtype* weight_scale_data = per_channel_scale_weight ? this->blobs_[2]->cpu_data() : NULL;
  const Dtype* weight_zero_point_data = per_channel_scale_weight ? this->blobs_[3]->cpu_data() : NULL;
  const Dtype* bias_scale_data = per_channel_scale_bias ? this->blobs_[4]->cpu_data() : NULL;
  const Dtype* bias_zero_point_data = per_channel_scale_bias ? this->blobs_[5]->cpu_data() : NULL;
  if (shift_weight) { // shift the quantized weight
    caffe_add_scalar<Dtype>(W->count(), Dtype(-weight_zero_point), W->mutable_cpu_data());
  } else if(per_channel_scale_weight) {
    const int slice = W->count() / quant_num_ch;
    Dtype* weight_mutable = W->mutable_cpu_data();
    for (int i = 0; i < quant_num_ch; ++i) {
      caffe_add_scalar<Dtype>(slice, -weight_zero_point_data[i], weight_mutable);
      weight_mutable += slice;
    }
  }
  if (shift_bias) {
    caffe_add_scalar<Dtype>(B->count(), Dtype(-bias_zero_point), B->mutable_cpu_data());
  } else if(per_channel_scale_bias) {
    // this branch might be dummy, because bias_zero_point is 0 in common
    // slice is 1, since quant_num_ch = num_output = len(bias)
    Dtype* bias_mutable = B->mutable_cpu_data();
    for (int i = 0; i < quant_num_ch; ++i) {
      bias_mutable[i] -= bias_zero_point_data[i];
    }
  }

  const Dtype* weight = this->blobs_[0]->cpu_data();
  for (int i = 0; i < bottom.size(); ++i) {
    if (shift_input) {
      caffe_add_scalar<Dtype>(bottom[i]->count(),
        Dtype(-input_zero_point), bottom[i]->mutable_cpu_data());
    }

    const Dtype* bottom_data = bottom[i]->cpu_data();
    Dtype* top_data = top[i]->mutable_cpu_data();

    for (int n = 0; n < this->num_; ++n) {
      this->forward_cpu_gemm(bottom_data + n * this->bottom_dim_, weight,
          top_data + n * this->top_dim_);
      if (this->bias_term_) {
        const Dtype* bias = this->blobs_[1]->cpu_data();
        this->forward_cpu_bias(top_data + n * this->top_dim_, bias);
      }
    }

    const int count_t = top[i]->count();
    if (scale_output) {
      if (per_channel_scale_weight && per_channel_scale_bias) {
        const int slice = count_t / quant_num_ch;
        Dtype* top_mutable = top[i]->mutable_cpu_data();
        for (int i = 0; i < quant_num_ch; ++i) {
          double out_scal = (double)input_scale * weight_scale_data[i];
          out_scal /= output_scale;
          caffe_cpu_scale_double_round(slice, out_scal, top_mutable);
          top_mutable += slice;
        }
      } else {
        // refer out_multiplier to https://github.com/tensorflow/tensorflow/blob/r1.11/tensorflow/contrib/lite/kernels/kernel_util.cc#L41
        double out_scal = (double)input_scale * weight_scale;
        out_scal /= output_scale;
        caffe_cpu_scale_double_round(count_t, out_scal, top_data);
      }
    }
    if (shift_output) {
      caffe_add_scalar<Dtype>(count_t, Dtype(output_zero_point), top_data);
    }
    if (saturate == ConvolutionParameter_SaturateMethod_Signed)
      caffe_cpu_signed_saturate(count_t, top_data);
    if (saturate == ConvolutionParameter_SaturateMethod_Unsigned)
      caffe_cpu_unsigned_saturate(count_t, top_data);
    if (saturate == ConvolutionParameter_SaturateMethod_Signed_8bit)
      caffe_cpu_signed_8bit_saturate(count_t, top_data);
    if (saturate == ConvolutionParameter_SaturateMethod_Unsigned_8bit)
      caffe_cpu_unsigned_8bit_saturate(count_t, top_data);

    if (shift_input) { // shift the quantized input blob back to correct range
      caffe_add_scalar<Dtype>(bottom[i]->count(),
        Dtype(input_zero_point), bottom[i]->mutable_cpu_data());
    }
  }
  // shift quantized weight/bias back to correct range
  if (shift_weight) {
    caffe_add_scalar<Dtype>(W->count(), Dtype(weight_zero_point), W->mutable_cpu_data());
  } else if(per_channel_scale_weight) {
    const int slice = W->count() / quant_num_ch;
    Dtype* weight_mutable = W->mutable_cpu_data();
    for (int i = 0; i < quant_num_ch; ++i) {
      caffe_add_scalar<Dtype>(slice, weight_zero_point_data[i], weight_mutable);
      weight_mutable += slice;
    }
  }
  if (shift_bias) {
    caffe_add_scalar<Dtype>(B->count(), Dtype(bias_zero_point), B->mutable_cpu_data());
  } else if(per_channel_scale_bias) {
    Dtype* bias_mutable = B->mutable_cpu_data();
    for (int i = 0; i < quant_num_ch; ++i) {
      bias_mutable[i] += bias_zero_point_data[i];
    }
  }
}

template <typename Dtype>
void ConvolutionLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom) {
  const Dtype* weight = this->blobs_[0]->cpu_data();
  //default update_weight =true
  bool update_weight = !this->layer_param_.convolution_param().weight_fixed();
  if (this->layer_param_.convolution_param().gen_mode() && this->gan_mode_ != 2) {
	update_weight = false;
  }
  if (this->layer_param_.convolution_param().dis_mode() && this->gan_mode_ == 2) {
	update_weight = false;
  }
  Dtype* weight_diff = this->blobs_[0]->mutable_cpu_diff();
  for (int i = 0; i < top.size(); ++i) {
    const Dtype* top_diff = top[i]->cpu_diff();
    const Dtype* bottom_data = bottom[i]->cpu_data();
    Dtype* bottom_diff = bottom[i]->mutable_cpu_diff();
    // Bias gradient, if necessary.
    if (this->bias_term_ && this->param_propagate_down_[1] && update_weight) {
      Dtype* bias_diff = this->blobs_[1]->mutable_cpu_diff();
      for (int n = 0; n < this->num_; ++n) {
        this->backward_cpu_bias(bias_diff, top_diff + n * this->top_dim_);
      }
    }
    if (this->param_propagate_down_[0] || propagate_down[i]) {
      for (int n = 0; n < this->num_; ++n) {
        // gradient w.r.t. weight. Note that we will accumulate diffs.
        if (this->param_propagate_down_[0] && update_weight) {
          this->weight_cpu_gemm(bottom_data + n * this->bottom_dim_,
              top_diff + n * this->top_dim_, weight_diff);
        }
        // gradient w.r.t. bottom data, if necessary.
        if (propagate_down[i]) {
          this->backward_cpu_gemm(top_diff + n * this->top_dim_, weight,
              bottom_diff + n * this->bottom_dim_);
        }
      }
    }
  }
  // update gan_mode_
  this->gan_mode_ = this->gan_mode_ == 2 ? 1 : this->gan_mode_ + 1;
}

#ifdef CPU_ONLY
STUB_GPU(ConvolutionLayer);
#endif

INSTANTIATE_CLASS(ConvolutionLayer);

}  // namespace caffe
