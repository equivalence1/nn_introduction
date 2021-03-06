//=======================================================================
// Copyright (c) 2017 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

/*!
 * \file
 * \brief Contains functions to read the CIFAR-10 dataset
 */

#ifndef CIFAR10_READER_HPP
#define CIFAR10_READER_HPP

#include <experiments/core/tensor_pair_dataset.h>
#include <experiments/core/params.h>
#include <util/json.h>

#include <torch/torch.h>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <utility>
#include <cstdint>
#include <chrono>
#include <stdexcept>

namespace experiments::cifar10 {

#define CIFAR10_FILE_SIZE 10000
#define IMAGE_SIZE (3 * 32 * 32)

 std::unique_ptr<uint8_t[]> read_cifar10_file_buffer(const std::string& path) {
    std::ifstream file;
    file.open(path, std::ios::in | std::ios::binary | std::ios::ate);

    if (!file) {
        throw std::runtime_error("Error opening file: " + path);
    }

    auto file_size = file.tellg();
    std::unique_ptr<uint8_t[]> buffer(new uint8_t[file_size]);

    //Read the entire file at once
    file.seekg(0, std::ios::beg);
    file.read((char*)buffer.get(), file_size);
    file.close();

    return buffer;
}

  void read_cifar10_file_float(float* x, long* y, const std::string& path, int& limit) {
    if (limit == 0) {
        return;
    }

    auto buffer = read_cifar10_file_buffer(path);

    int size = CIFAR10_FILE_SIZE;
    if (limit > 0) {
        size = std::min(size, limit);
    }

    for(std::size_t i = 0; i < size; ++i){
        y[i] = buffer[i * (IMAGE_SIZE + 1)];

        for(std::size_t j = 1; j < (IMAGE_SIZE + 1); ++j){
            x[i * IMAGE_SIZE + (j - 1)] = buffer[i * (IMAGE_SIZE + 1) + j] * 1.0f / 255;
        }
    }

    if (limit > 0) {
        limit -= size;
    }
}

/*!
 * \brief Read all test data.
 *
 * The dataset is assumed to be in a cifar-10 subfolder
 *
 * \param limit The maximum number of elements to read (0: no limit)
 */
 void read_test(const std::string& folder, int limit, float* images, long* labels) {
    read_cifar10_file_float(images, labels, folder + "/test_batch.bin", limit);
}

/*!
 * \brief Read all training data
 *
 * The dataset is assumed to be in a cifar-10 subfolder
 *
 * \param limit The maximum number of elements to read (0: no limit)
 */
 void read_training(const std::string& folder, int limit, float* images, long* labels) {
    for (int i = 1; i <= 5; i++) {
        if (limit == 0) {
            break;
        }

        std::string fname = "/data_batch_" + std::to_string(i) + ".bin";
        read_cifar10_file_float(images, labels, folder + fname, limit);

        images += IMAGE_SIZE * CIFAR10_FILE_SIZE;
        labels += 1 * CIFAR10_FILE_SIZE;
    }
}

TensorPairDataset normalize(TensorPairDataset ds) {
    auto mds = ds
            .map(torch::data::transforms::Normalize(
                    std::vector<double>({0.4914, 0.4822, 0.4465}),
                    std::vector<double>({0.2023, 0.1994, 0.2010})))
            .map(torch::data::transforms::Stack<>());

    std::vector<unsigned long> indices;
    for (int i = 0; i < ds.size().value(); ++i) {
        indices.push_back(i);
    }

    auto examples = mds.get_batch(indices);
    return {examples.data, examples.target};
}

std::pair<TensorPairDataset, TensorPairDataset> read_dataset(int trainLimit, int testLimit) {
    static const std::string folder = "../../../../resources/cifar10/cifar-10-batches-bin";

    torch::Tensor trainX = torch::zeros({50000, 3, 32, 32}, torch::kFloat32);
    torch::Tensor trainY = torch::zeros({50000}, torch::kLong);

    torch::Tensor testX = torch::zeros({10000, 3, 32, 32}, torch::kFloat32);
    torch::Tensor testY = torch::zeros({10000}, torch::kLong);

    auto startTime = std::chrono::high_resolution_clock::now();

    read_training(folder, trainLimit, (float*)trainX.data_ptr(), (long*)trainY.data_ptr());
    read_test(folder, testLimit, (float*)testX.data_ptr(), (long*)testY.data_ptr());

    auto elapsedTime = std::chrono::high_resolution_clock::now() - startTime;
    auto elapsedTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsedTime);

    std::cout << "read cifar10 dataset in " << elapsedTimeMs.count() << "ms" << std::endl;

    auto trainDs = normalize(std::move(TensorPairDataset(trainX, trainY)));
    auto testDs = normalize(std::move(TensorPairDataset(testX, testY)));

    return std::make_pair(trainDs, testDs);
}

} //end of namespace cifar

#endif
