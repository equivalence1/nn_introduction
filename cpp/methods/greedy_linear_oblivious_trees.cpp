#include "greedy_linear_oblivious_trees.h"

#include <memory>
#include <set>
#include <stdexcept>
#include <chrono>

#include <core/vec_factory.h>
#include <core/matrix.h>

void Histogram::build(const DataSet& ds, const std::set<int>& usedFeatures,
        const std::vector<int32_t>& indices, int64_t biasCol) {
    const auto& bds = cachedBinarize(ds, grid_);

    histLeft_XTX_ = std::vector<Mx>();
    histLeft_XTy_ = std::vector<Mx>();
    histLeft_XTX_trace_ = std::vector<double>();
    histLeft_cnt_ = std::vector<uint32_t>();

    bool needBias = biasCol < 0;

    if ((int)usedFeatures.size() - int(!needBias) > 0) {
        for (auto f : usedFeatures) {
            if (f != biasCol) {
                usedFeature_ = f; // this one is origFeatureId
            }
        }

        auto features = grid_->nzFeatures();
        for (int f = 0; f < (int)features.size(); ++f) {
            if (features.at(f).origFeatureId_ == usedFeature_) {
                usedFeature_ = f; // this one is nzFeatureId
                break;
            }
        }
    }

    for (int i = 0, curF = 0; i < (int)bds.totalBins(); ++i) {
        if (curF < grid_->nzFeaturesCount() - 1
                && bds.binOffsets().at(curF + 1) == i) {
            ++curF;
        }
        int curOrigF = grid_->nzFeatures().at(curF).origFeatureId_;

        int vecSize = usedFeatures.size();
        if (usedFeatures.find(curOrigF) == usedFeatures.end()) {
            ++vecSize;
        }

        // bias term
        vecSize += needBias ? 1 : 0;

        histLeft_XTX_.emplace_back(vecSize, vecSize);
        histLeft_XTy_.emplace_back(vecSize, 1);
        histLeft_XTX_trace_.emplace_back(0);
        histLeft_cnt_.emplace_back(0);
    }

    auto y = ds.target().arrayRef();

    Detail::ArrayRef<const int32_t> indicesVecRef(indices.data(), indices.size());

    std::cout << "Hello?" << std::endl;

    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

    auto features = grid_->nzFeatures();
//    for (int64_t i = 0; i < features.size(); ++i) {
    parallelFor(0, features.size(), [&](int64_t f) {
        auto newUsedFeatures = usedFeatures;
        newUsedFeatures.insert(features[f].origFeatureId_);
        std::vector<int64_t> newUsedFeaturesV(newUsedFeatures.begin(), newUsedFeatures.end());
        auto indexes = torch::tensor(newUsedFeaturesV);
        auto newDsDataTensor = ds.samplesMx().data().view({ds.samplesCount(), ds.featuresCount()}).index_select(1, indexes).contiguous().view({-1});
        auto newDsDataMx = Mx(Vec(newDsDataTensor), ds.samplesCount(), newUsedFeatures.size());
        DataSet samplesDs(newDsDataMx, ds.target());

//        std::cout << "newUsedFeatures.size(): " << newUsedFeatures.size() << std::endl;
        bds.visitFeature(features[f].featureId_, indicesVecRef, [&](int blockId, int idxId, int8_t localBinId) {
            int offset = bds.binOffsets()[f];
            int binPos = offset + localBinId;

            int32_t sampleId = indicesVecRef[idxId];

            auto x = samplesDs.sample(sampleId);
            auto xb = needBias ? x.append(1) : std::move(x);
            auto X = Mx(xb, xb.size(), 1);

//            std::cout << "fId: " << f << ", bin: " << int(localBinId) << ", adding x " << X <<
//                     ", target " << std::setprecision(3) << y[sampleId] << std::endl;

            auto XTX = X.XXT();
            auto XTy = X * y[sampleId];

            histLeft_XTX_[binPos] += XTX;
            histLeft_XTy_[binPos] += XTy;
            histLeft_cnt_[binPos] += 1;
            histLeft_XTX_trace_[binPos] += l2(xb);
        }, false);

        int offset = bds.binOffsets()[f];

        for (int localBinId = 1; localBinId <= (int)grid_->conditionsCount(f); ++localBinId) {
            int binPos = offset + localBinId;
            histLeft_XTX_[binPos] += histLeft_XTX_[binPos - 1];
            histLeft_XTy_[binPos] += histLeft_XTy_[binPos - 1];
            histLeft_cnt_[binPos] += histLeft_cnt_[binPos - 1];
            histLeft_XTX_trace_[binPos] += histLeft_XTX_trace_[binPos - 1];
        }
    });

    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();

    std::cout << "Hist built in " << duration << " [ms]" << std::endl;
}

void Histogram::printCnt() {
    if (usedFeature_ == -1) {
        throw std::runtime_error("No features are used, can not print eig");
    }

    uint32_t offset = grid_->binOffsets().at(usedFeature_);
    uint32_t lastPos = offset + grid_->conditionsCount(usedFeature_);

    std::cout << "cnt: " << histLeft_cnt_[lastPos] << std::endl;
}

void Histogram::printEig(Mx& M) {
    auto eigs = torch::eig(M.data().view({M.ydim(), M.xdim()}), false);
    auto vals = std::get<0>(eigs);
    for (int i = 0; i < (int)vals.size(0); ++i) {
        std::cout << vals[i].data()[0] << ", ";
    }
}

void Histogram::printEig(double l2reg) {
    if (usedFeature_ == -1) {
        throw std::runtime_error("No features are used, can not print eig");
    }

    uint32_t offset = grid_->binOffsets().at(usedFeature_);
    uint32_t lastPos = offset + grid_->conditionsCount(usedFeature_);

    Mx& XTX = histLeft_XTX_[lastPos];

    std::cout << "XTX eig: ";
    printEig(XTX);
    std::cout << std::endl;

    Mx XTX_Reg = XTX + Diag(XTX.xdim(), l2reg);

    std::cout << "(XTX + Diag(" << l2reg << ")) eig: ";
    printEig(XTX_Reg);
    std::cout << std::endl;

    std::cout << "XTX trace: " << histLeft_XTX_trace_[lastPos] << std::endl;
}

std::shared_ptr<Mx> Histogram::getW(double l2reg) {
    if (usedFeature_ == -1) {
        throw std::runtime_error("No features are used");
    }

    uint32_t offset = grid_->binOffsets().at(usedFeature_);
    uint32_t lastPos = offset + grid_->conditionsCount(usedFeature_);

    int xtx_dim = histLeft_XTX_[lastPos].ydim();

    Mx XTX = histLeft_XTX_[lastPos] + Diag(xtx_dim, l2reg);
    Mx& XTy = histLeft_XTy_[lastPos];

    try {
        return std::make_shared<Mx>(XTX.inverse() * XTy);
    } catch (...) {
        std::cout << "No inverse" << std::endl;
        return std::make_shared<Mx>((int64_t)XTX.ydim(), (int64_t)XTX.xdim());
    }
}

double Histogram::computeScore(Mx& XTX, Mx& XTy, double XTX_trace, uint32_t cnt, double l2reg,
        double traceReg) {
    if (cnt == 0) {
        return 0;
    }

    // Dealing with matrix singularity
    // Not sure what we should do in this case...
    // For now just return 0.
    try {
        Mx w = XTX.inverse() * XTy;
//    std::cout << "w is " << w << std::endl;

        Mx c1(XTy.T() * w);
        c1 *= -2;
        assert(c1.xdim() == 1 && c1.ydim() == 1);

        Mx c2(w.T() * XTX * w);
        assert(c2.xdim() == 1 && c2.ydim() == 1);

        Mx reg = w.T() * w * l2reg;
        assert(reg.xdim() == 1 && reg.ydim() == 1);

        Mx res = c1 + c2 + reg;

        return res.get(0, 0) + traceReg * XTX_trace / XTX.ydim();
    } catch (...) {
        std::cout << "No inverse" << std::endl;
        return 0;
    }
}

std::pair<double, double> Histogram::splitScore(int fId, int condId, double l2reg,
        double traceReg) {
    uint32_t offset = grid_->binOffsets()[fId];
    uint32_t binPos = offset + condId;
    uint32_t lastPos = offset + grid_->conditionsCount(fId);

    int xtx_dim = histLeft_XTX_[binPos].ydim();

    Mx left_XTX = histLeft_XTX_[binPos] + Diag(xtx_dim, l2reg);
    Mx right_XTX = histLeft_XTX_[lastPos] - histLeft_XTX_[binPos] + Diag(xtx_dim, l2reg);

    Mx left_XTy(histLeft_XTy_[binPos]);
    Mx right_XTy = histLeft_XTy_[lastPos] - histLeft_XTy_[binPos];

    uint32_t left_cnt = histLeft_cnt_[binPos];
    uint32_t right_cnt = histLeft_cnt_[lastPos] - histLeft_cnt_[binPos];

    double left_XTX_trace = histLeft_XTX_trace_[binPos];
    double right_XTX_trace = histLeft_XTX_trace_[lastPos] - histLeft_XTX_trace_[binPos];

//    std::cout << "split fId: " << fId << ", cond: " << condId << " ";

    auto resLeft = computeScore(left_XTX, left_XTy, left_XTX_trace, left_cnt, l2reg, traceReg);
    auto resRight = computeScore(right_XTX, right_XTy, right_XTX_trace, right_cnt, l2reg, traceReg);

    return std::make_pair(resLeft, resRight);
}

Histogram operator-(const Histogram& lhs, const Histogram& rhs) {
    Histogram res(lhs.grid_);

    for (int32_t i = 0; i < lhs.grid_->totalBins(); ++i) {
        res.histLeft_XTX_.emplace_back(lhs.histLeft_XTX_[i] - rhs.histLeft_XTX_[i]);
        res.histLeft_XTy_.emplace_back(lhs.histLeft_XTy_[i] - rhs.histLeft_XTy_[i]);
        res.histLeft_cnt_.emplace_back(lhs.histLeft_cnt_[i] - rhs.histLeft_cnt_[i]);
        res.histLeft_XTX_trace_.emplace_back(lhs.histLeft_XTX_trace_[i] - rhs.histLeft_XTX_trace_[i]);
    }

    res.usedFeature_ = rhs.usedFeature_;

    return res;
}


class LinearObliviousTreeLeaf : std::enable_shared_from_this<LinearObliviousTreeLeaf> {
public:
    LinearObliviousTreeLeaf(GridPtr grid, int64_t biasCol, double l2reg, double traceReg)
            : grid_(std::move(grid))
            , biasCol_(biasCol)
            , l2reg_(l2reg)
            , traceReg_(traceReg) {

    }

    void buildHist(const DataSet& ds) {
        if (hist_) {
            return;
        }

//        std::cout << "building hist with indices: ";
//        for (auto xId : xIds_) {
//            std::cout << xId << " ";
//        }
//        std::cout << std::endl;

        hist_ = std::make_unique<Histogram>(grid_);
        hist_->build(ds, usedFeatures_, xIds_, biasCol_);
    }

    double splitScore(const DataSet& ds, int fId, int condId) {
        buildHist(ds);
        auto sScore = hist_->splitScore(fId, condId, l2reg_, traceReg_);
        return sScore.first + sScore.second;
    }

    void fit(const DataSet& ds) {
        if (w_) {
            return;
        }
        if (!hist_) {
            buildHist(ds);
        }
        w_ = hist_->getW(l2reg_);
    }

    double value(const Vec& x) {
        if (!w_) {
            throw std::runtime_error("Not fitted");
        }

        std::vector<double> tmp;
        for (auto f : usedFeatures_) {
            tmp.push_back(x(f));
        }
        auto xVec = biasCol_ < 0 ? VecFactory::fromVector(tmp).append(1) : VecFactory::fromVector(tmp);
        Mx X(xVec, xVec.size(), 1);
        Mx res = X.T() * (*w_);

        return res.get(0, 0);
    }

    bool isInRegion(const Vec& x) {
        for (auto& s : splits_) {
            int32_t fId = std::get<0>(s);
            int32_t condId = std::get<1>(s);
            bool isLeft = std::get<2>(s);

            int32_t origFId = grid_->nzFeatures().at(fId).origFeatureId_;
            float border = grid_->borders(fId).at(condId);

            if ((x(origFId) <= border) ^ isLeft) {
                return false;
            }
        }

        return true;
    }

    std::pair<std::shared_ptr<LinearObliviousTreeLeaf>, std::shared_ptr<LinearObliviousTreeLeaf>> split(const DataSet& ds,
            int32_t fId, int32_t condId) {
        auto left = std::make_shared<LinearObliviousTreeLeaf>(grid_, biasCol_, l2reg_, traceReg_);
        auto right = std::make_shared<LinearObliviousTreeLeaf>(grid_, biasCol_, l2reg_, traceReg_);

        initChildren(ds, left, right, fId, condId);

        return std::make_pair(left, right);
    }

    void printInfo() {
        hist_->printEig(l2reg_);
        hist_->printCnt();
        printSplits();
        std::cout << std::endl;
    }

    void printSplits() {
        for (auto& s : splits_) {
            auto fId = std::get<0>(s);
            auto condId = std::get<1>(s);
            double minCondition = grid_->condition(fId, 0);
            double maxCondition = grid_->condition(fId, grid_->conditionsCount(fId) - 1);
            double condition = grid_->condition(fId, condId);
            std::cout << "split: fId=" << fId << ", condId=" << condId
                    << std::setprecision(5) << ", used cond=" << condition
                    << ", min cond=" << minCondition << ", max cond=" << maxCondition << std::endl;
        }
    }

private:
    void initChildren(const DataSet& ds, std::shared_ptr<LinearObliviousTreeLeaf>& left,
            std::shared_ptr<LinearObliviousTreeLeaf>& right,
            int32_t fId, int32_t condId) {
        int32_t origFeatureId = grid_->nzFeatures().at(fId).origFeatureId_;
        double border = grid_->borders(fId).at(condId);

//        std::cout << "Splitting on feature " << origFeatureId << std::endl;

        for (auto xId : xIds_) {
            if (ds.sample(xId)(origFeatureId) <= border) {
                left->xIds_.push_back(xId);
            } else {
                right->xIds_.push_back(xId);
            }
        }

        left->usedFeatures_ = this->usedFeatures_;
        left->usedFeatures_.insert(origFeatureId);
        right->usedFeatures_ = this->usedFeatures_;
        right->usedFeatures_.insert(origFeatureId);

        left->splits_ = this->splits_;
        left->splits_.emplace_back(std::make_tuple(fId, condId, true));
        right->splits_ = this->splits_;
        right->splits_.emplace_back(std::make_tuple(fId, condId, false));

        if (usedFeatures_.size() == left->usedFeatures_.size()) {
            left->buildHist(ds);
            right->hist_ = std::make_unique<Histogram>((*hist_) - (*left->hist_));
        }
    }

private:
    friend class GreedyLinearObliviousTreeLearner;

    GridPtr grid_;
    std::vector<int32_t> xIds_;
    std::set<int32_t> usedFeatures_;
    std::shared_ptr<Mx> w_;
    std::vector<std::tuple<int32_t, int32_t, bool>> splits_;
    int64_t biasCol_;

    double l2reg_;
    double traceReg_;

    std::unique_ptr<Histogram> hist_;
};

ThreadPool GreedyLinearObliviousTreeLearner::buildThreadPool_;

ModelPtr GreedyLinearObliviousTreeLearner::fit(const DataSet& ds, const Target& target) {
    auto root = std::make_shared<LinearObliviousTreeLeaf>(this->grid_, biasCol_, l2reg_, traceReg_);
    root->xIds_.resize(ds.samplesCount());
    std::iota(root->xIds_.begin(), root->xIds_.end(), 0);

    if (biasCol_ != -1) {
        root->usedFeatures_.insert(biasCol_);
    }

    std::vector<std::shared_ptr<LinearObliviousTreeLeaf>> leaves;
    leaves.push_back(std::move(root));

    Vec ys = target.targets();
    DataSet newDs(ds.samplesMx(), ys);

    for (int d = 0; d < maxDepth_; ++d) {
        double bestSplitScore = 1e9;
        int32_t splitFId = -1;
        int32_t splitCond = -1;

        parallelForInThreadPool(buildThreadPool_, 0, leaves.size(), [&](int64_t lId) {
            auto& l = leaves[lId];
            l->buildHist(newDs);
        });

        Mx splitScores(grid_->nzFeatures().size(), 32);

        parallelForInThreadPool(buildThreadPool_, 0, grid_->nzFeatures().size(), [&](int64_t fId) {
            parallelFor(0, grid_->conditionsCount(fId), [&](int64_t cond) {
                for (auto& l : leaves) {
                    double oldScore = splitScores.get(cond, fId);
                    splitScores.set(cond, fId, oldScore + l->splitScore(newDs, fId, cond));
                }
            });
        });

        for (int fId = 0; fId < grid_->nzFeatures().size(); ++fId) {
            for (int64_t cond = 0; cond < grid_->conditionsCount(fId); ++cond) {
                double sScore = splitScores.get(cond, fId);
                if (sScore < bestSplitScore) {
                    bestSplitScore = sScore;
                    splitFId = fId;
                    splitCond = cond;
                }
            }
        }

        std::vector<std::shared_ptr<LinearObliviousTreeLeaf>> new_leaves(leaves.size() * 2);

        parallelForInThreadPool(buildThreadPool_, 0, leaves.size(), [&](int64_t lId) {
            auto& l = leaves[lId];
            auto nLeaves = l->split(newDs, splitFId, splitCond);
            new_leaves[lId * 2] = std::move(nLeaves.first);
            new_leaves[lId * 2 + 1] = std::move(nLeaves.second);
        });

        leaves = std::move(new_leaves);
    }

    parallelForInThreadPool(buildThreadPool_, 0, leaves.size(), [&](int64_t lId) {
        auto& l = leaves[lId];
        l->fit(newDs);
    });

    for (auto& l : leaves) {
        l->printInfo();
    }

    return std::make_shared<GreedyLinearObliviousTree>(grid_, std::move(leaves));
}


double GreedyLinearObliviousTree::value(const Vec& x) const {
    for (auto& l : leaves_) {
        if (l->isInRegion(x)) {
            return scale_ * l->value(x);
        }
    }

    throw std::runtime_error("given x does not belong to any region O_o");
}

void GreedyLinearObliviousTree::appendTo(const Vec &x, Vec to) const {
    to += static_cast<const GreedyLinearObliviousTree*>(this)->value(x);
}

double GreedyLinearObliviousTree::value(const Vec &x) {
    return static_cast<const GreedyLinearObliviousTree*>(this)->value(x);
}

void GreedyLinearObliviousTree::grad(const Vec &x, Vec to) {
    throw std::runtime_error("Unimplemented");
}
