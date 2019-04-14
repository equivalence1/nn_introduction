#include "common.h"
#include <cifar_nn/vgg.h>
#include <cifar_nn/cifar10_reader.hpp>
#include <cifar_nn/optimizer.h>
#include <cifar_nn/cross_entropy_loss.h>

#include <torch/torch.h>

#include <string>
#include <memory>
#include <iostream>

class EpochEndCallback : public experiments::OptimizerEpochListener {
public:
    template <class Callback>
    EpochEndCallback(Callback callback)
    : Callback_(std::move(callback)) {

    }


    void epochReset() override {

    }

    void onEpoch(int epoch, double* lr, experiments::ModelPtr model) override {
        Callback_(epoch, *model);

    }

private:
    std::function<void(int, experiments::Model& model)> Callback_;
};

int main(int argc, char* argv[]) {
    auto device = torch::kCPU;
    if (argc > 1 && std::string(argv[1]) == std::string("CUDA")
            && torch::cuda::is_available()) {
        device = torch::kCUDA;
        std::cout << "Using CUDA device for training" << std::endl;
    } else {
        std::cout << "Using CPU device for training" << std::endl;
    }

    // Init model

    auto vgg = std::make_shared<Vgg>(VggConfiguration::Vgg16);
//    auto vgg = std::make_shared<ResNet>(ResNetConfiguration::ResNet18);
    vgg->to(device);

    // Load data

    const std::string& path = "../../../../resources/cifar10/cifar-10-batches-bin";
    auto dataset = cifar::read_dataset(path);

    // Create optimizer

    auto optimizer = getDefaultCifar10Optimizer(1000, vgg, device);
    auto loss = std::make_shared<CrossEntropyLoss>();

    // AttachListeners

    attachDefaultListeners(optimizer, 50000 / 128 / 10, "vgg_checkpoint.pt");
    auto mds = dataset.second.map(getDefaultCifar10TestTransform());

    experiments::Optimizer::emplaceEpochListener<EpochEndCallback>(optimizer.get(), [&](int epoch, experiments::Model& model) {
        model.eval();

        auto dloader = torch::data::make_data_loader(mds, torch::data::DataLoaderOptions(128));
        int rightAnswersCnt = 0;

        for (auto& batch : *dloader) {
            auto data = batch.data;
            data = data.to(device);
            torch::Tensor target = batch.target;

            torch::Tensor prediction = model.forward(data);
            prediction = torch::argmax(prediction, 1);

            prediction = prediction.to(torch::kCPU);

            auto targetAccessor = target.accessor<int64_t, 1>();
            auto predictionsAccessor = prediction.accessor<int64_t, 1>();
            int size = target.size(0);

            for (int i = 0; i < size; ++i) {
                const int targetClass = targetAccessor[i];
                const int predictionClass = predictionsAccessor[i];
                if (targetClass == predictionClass) {
                    rightAnswersCnt++;
                }
            }
        }

        std::cout << "Test accuracy: " <<  rightAnswersCnt * 100.0f / dataset.second.size().value() << std::endl;
    });

    // Train model

    optimizer->train(dataset.first, loss, vgg);

    // Evaluate on test set
    auto acc = evalModelTestAccEval(dataset.second,
                                    vgg,
                                    device,
                                    getDefaultCifar10TestTransform());

    std::cout << "VGG test accuracy: " << std::setprecision(2)
              << acc << "%" << std::endl;
    return 0;
}
