#pragma once

#include <catboost_wrapper.h>
#include <memory>
#include <unordered_map>
#include <util/array_ref.h>
#include <util/city.h>

struct BinarySplit {
    int Feature = 0;
    float Condition = 0;
    bool operator==(const BinarySplit& rhs) const {
        return std::tie(Feature, Condition) == std::tie(rhs.Feature, rhs.Condition);
    }
    bool operator!=(const BinarySplit& rhs) const {
        return !(rhs == *this);
    }

    bool operator<(const BinarySplit& rhs) const {
        return std::tie(Feature, Condition) < std::tie(rhs.Feature, rhs.Condition);
    }
    bool operator>(const BinarySplit& rhs) const {
        return rhs < *this;
    }
    bool operator<=(const BinarySplit& rhs) const {
        return !(rhs < *this);
    }
    bool operator>=(const BinarySplit& rhs) const {
        return !(*this < rhs);
    }
};


struct PolynomStructure {
    std::vector<BinarySplit> Splits;


    uint32_t GetDepth() const {
        return Splits.size();
    }

    void AddSplit(const BinarySplit& split) {
        Splits.push_back(split);
    }

    bool operator==(const PolynomStructure& rhs) const {
        return std::tie(Splits) == std::tie(rhs.Splits);
    }

    bool operator!=(const PolynomStructure& rhs) const {
        return !(rhs == *this);
    }

    uint64_t GetHash() const {
        return VecCityHash(Splits);
    }

    bool IsSorted() const {
        for (uint32_t i = 1; i < Splits.size(); ++i) {
            if (Splits[i] <= Splits[i - 1]) {
                return false;
            }
        }
        return true;
    }

    bool HasDuplicates() const {
        for (uint32_t i = 1; i < Splits.size(); ++i) {
            if (Splits[i] == Splits[i - 1]) {
                return true;
            }
        }
        return false;
    }

};

template <>
struct std::hash<PolynomStructure> {
    inline size_t operator()(const PolynomStructure& value) const {
        return value.GetHash();
    }
};




struct TStat {
    std::vector<double> Value;
    double Weight = -1;
};


// sum v * Prod [x _i > c_i]
class PolynomBuilder {
public:

    void AddTree(const TSymmetricTree& tree);

    PolynomBuilder& AddEnsemble(const TEnsemble& ensemble) {
        for (const auto& tree : ensemble.Trees) {
            AddTree(tree);
        }
        return *this;
    }

    std::unordered_map<PolynomStructure, TStat> Build() {
        return EnsemblePolynoms;
    }
private:
    std::unordered_map<PolynomStructure, TStat> EnsemblePolynoms;
};

// TBD: Not sure if we should make it unique or shared ptr
class Monom;
using MonomPtr = std::unique_ptr<Monom>;

template<class T, class... Args>
inline static MonomPtr _makeMonomPtr(Args&&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
}


class Monom {
public:
    enum class MonomType {
        SigmoidProbMonom,
        ExpProbMonom,
    };

    friend struct Polynom;

public:
    Monom() = default;

    Monom(PolynomStructure structure, std::vector<double> values)
            : Structure_(std::move(structure))
            , Values_(std::move(values)) {

    }

    int OutDim() const {
        return (int)Values_.size();
    }

    //forward/backward will append to dst
    virtual void Forward(double lambda, ConstVecRef<float> features, VecRef<float> dst) const = 0;
    virtual void Backward(double lambda, ConstVecRef<float> features, ConstVecRef<float> outputsDer, VecRef<float> featuresDer) const = 0;

    virtual MonomType getMonomType() const = 0;

    static MonomType getMonomType(const std::string& strMonomType);

    static MonomPtr createMonom(MonomType monomType);
    static MonomPtr createMonom(MonomType monomType, PolynomStructure structure, std::vector<double> values);

    virtual ~Monom() = default;

public:
    PolynomStructure Structure_;
    std::vector<double> Values_;
};

class SigmoidProbMonom : public Monom {
public:
    SigmoidProbMonom() = default;

    SigmoidProbMonom(PolynomStructure structure, std::vector<double> values)
            : Monom(std::move(structure), std::move(values)) {

    }

    Monom::MonomType getMonomType() const override;

    void Forward(double lambda, ConstVecRef<float> features, VecRef<float> dst) const override;
    void Backward(double lambda, ConstVecRef<float> features, ConstVecRef<float> outputsDer, VecRef<float> featuresDer) const override;
};

class ExpProbMonom : public Monom {
public:
    ExpProbMonom() = default;

    ExpProbMonom(PolynomStructure structure, std::vector<double> values)
            : Monom(std::move(structure), std::move(values)) {

    }

    Monom::MonomType getMonomType() const override;

    void Forward(double lambda, ConstVecRef<float> features, VecRef<float> dst) const override;
    void Backward(double lambda, ConstVecRef<float> features, ConstVecRef<float> outputsDer, VecRef<float> featuresDer) const override;
};

struct Polynom {
    std::vector<MonomPtr> Ensemble_;
    double Lambda_  = 1.0;

    Polynom(Monom::MonomType monomType, const std::unordered_map<PolynomStructure, TStat>& polynom) {
        for (const auto& [structure, stat] : polynom) {
            Ensemble_.emplace_back(Monom::createMonom(monomType, structure, stat.Value));
        }
    }

    Polynom() = default;

    void PrintHistogram();

    Monom::MonomType getMonomType() const;

    //forward/backward will append to dst
    void Forward(ConstVecRef<float> features, VecRef<float> dst) const;
    void Backward(ConstVecRef<float> features, ConstVecRef<float> outputsDer, VecRef<float> featuresDer) const;

    int OutDim() const {
        return Ensemble_.empty() ? 0 : Ensemble_.back()->OutDim();
    }
};

using PolynomPtr = std::shared_ptr<Polynom>;
