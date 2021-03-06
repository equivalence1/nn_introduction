#pragma once

#include <set>

#include <torch/torch.h>
#include <core/object.h>
#include <core/matrix.h>
#include <core/cache.h>

class DataSet : public Object, public CacheHolder<DataSet> {
public:
    explicit DataSet(Mx data, Vec target)
    : data_(data)
    , target_(target){
        assert(target.dim() == samplesCount());
    }

    int64_t featuresCount() const {
        return data_.xdim();
    }

    int64_t samplesCount() const {
        return data_.ydim();
    }

    void copyColumn(int fIndex, Vec* col) const {
        assert(col->dim() == samplesCount());
        assert(col->isContiguous());
        VecRef<float> writeDst = col->arrayRef();
        data_.iterateOverColumn(fIndex, [&](int64_t lineIdx, float val) {
            writeDst[lineIdx] = val;
        });
    }

    void addColumn(const Vec& col) {
        data_.addColumn(col, true);
    }

    void addBiasColumn() {
        Vec x(samplesCount(), 1);
        data_.addColumn(x);
    }

    template <class Visitor>
    void visitColumn(int fIndex, Visitor&& visitor) const {
        data_.iterateOverColumn(fIndex, visitor);
    }

    Vec sample(int64_t line) const {
        return data_.row(line);
    }

    Vec sample(int64_t line, const std::set<int>& features) const {
        auto x = sample(line);
        auto x_arr = x.arrayRef();

        Vec res(features.size());
        auto res_arr = res.arrayRef();

        int i = 0;
        for (auto f : features) {
            res_arr[i++] = x_arr[f];
        }

        return res;
    }

    Mx samplesMx() const {
        return data_;
    }

    Mx sampleMx(const std::set<int>& features) const {
        auto indexes = torch::tensor(std::vector<long>(features.begin(), features.end()));
        auto resTensor = data_.data().view({samplesCount(), featuresCount()}).transpose(0, 1).index_select(0, indexes).transpose(0, 1).contiguous().view({-1});
        auto tmpResVec = Vec(resTensor);
        return Mx(tmpResVec, samplesCount(), features.size());
    }

    DataSet subDs(const std::set<int>& features) const {
        Mx data = sampleMx(features);
        return DataSet(data, target_);
    }

    Vec target() const {
        return target_;
    }

    const float* samples() const {
        return data_.arrayRef().data();
    }
    const float* labels() const {
        return target_.arrayRef().data();
    }

    torch::Tensor tensorData() const {
        return data_.data();
    }
private:
    Mx data_;
    Vec target_;
};
