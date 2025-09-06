#include "tensor.hpp"
#include <cmath>


float Tensor::norm() {
    double sum = 0;
    for (float d : data) {
        sum += (double)(d * d);
    }
    return (float)sqrt(sum);
}

float Tensor::mean() {
    double sum = 0;
    for (float d : data) {
        sum += (double)d;
    }
    return (float)(sum / data.size());
}

float Tensor::sum() {
    double sum = 0;
    for (float d : data) {
        sum += (double)d;
    }
    return (float)(sum);
}