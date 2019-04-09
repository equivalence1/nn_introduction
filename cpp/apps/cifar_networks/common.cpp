#include "common.h"

#include <cifar_nn/transform.h>
#include <cifar_nn/model.h>
#include <cifar_nn/tensor_pair_dataset.h>

#include <torch/torch.h>

#include <vector>
#include <memory>
#include <string>

TransformType getDefaultCifar10TrainTransform() {
    // transforms are similar to https://github.com/kuangliu/pytorch-cifar/blob/master/main.py#L31

    auto normTransformTrain = std::make_shared<torch::data::transforms::Normalize<>>(
            torch::ArrayRef<double>({0.4914, 0.4822, 0.4465}),
            torch::ArrayRef<double>({0.2023, 0.1994, 0.2010}));
    auto cropTransformTrain = std::make_shared<experiments::RandomCrop>(
            std::vector<int>({32, 32}),
            std::vector<int>({4, 4}));
    auto flipTransformTrain = std::make_shared<experiments::RandomHorizontalFlip>(0.5);
    auto stackTransformTrain = std::make_shared<torch::data::transforms::Stack<>>();

    auto transformFunc = [=](std::vector<torch::data::Example<>> batch){
        batch = normTransformTrain->apply_batch(batch);
        batch = flipTransformTrain->apply_batch(batch);
        batch = cropTransformTrain->apply_batch(batch);
        return stackTransformTrain->apply_batch(batch);
    };

    TransformType transform(transformFunc);
    return transform;
}

TransformType getDefaultCifar10TestTransform() {
    // transforms are similar to https://github.com/kuangliu/pytorch-cifar/blob/master/main.py#L38

    torch::data::transforms::Normalize<> normTransformTrain(
            {0.4914, 0.4822, 0.4465},
            {0.2023, 0.1994, 0.2010});
    torch::data::transforms::Stack<> stackTransformTrain;

    auto transformFunc = [&](std::vector<torch::data::Example<>> batch){
        batch = normTransformTrain.apply_batch(batch);
        return stackTransformTrain.apply_batch(batch);
    };

    TransformType transform(transformFunc);
    return transform;
}

OptimizerType<TransformType> getDefaultCifar10Optimizer(int epochs, const experiments::ModelPtr& model,
        torch::DeviceType device) {
    experiments::OptimizerArgs<TransformType> args(getDefaultCifar10TrainTransform(),
            epochs, device);

    args.epochs_ = epochs;

    auto dloaderOptions = torch::data::DataLoaderOptions(128);
    args.dloaderOptions_ = std::move(dloaderOptions);

    torch::optim::SGDOptions opt(0.1);
    opt.momentum_ = 0.9;
    opt.weight_decay_ = 5e-4;
    auto optim = std::make_shared<torch::optim::SGD>(model->parameters(), opt);
    args.torchOptim_ = optim;

    args.lrPtrGetter_ = [&](){return &optim->options.learning_rate_;};

    auto optimizer = std::make_shared<experiments::DefaultOptimizer<TransformType>>(args);
    return optimizer;
}
