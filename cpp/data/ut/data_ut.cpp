#include <data/load_data.h>

#include <gtest/gtest.h>
#include <data/grid_builder.h>
#include <data/binarized_dataset.h>

#define EPS 1e-5
#define PATH_PREFIX "../../../../"

TEST(Data, TestLoad) {
    auto ds = loadFeaturesTxt(PATH_PREFIX "test_data/featuresTxt/train");
    EXPECT_EQ(ds.samplesCount(), 12465);
    EXPECT_EQ(ds.featuresCount(), 50);

}

TEST(Data, TesGrid) {
    auto ds = loadFeaturesTxt(PATH_PREFIX "test_data/featuresTxt/train");
    EXPECT_EQ(ds.samplesCount(), 12465);
    EXPECT_EQ(ds.featuresCount(), 50);

    {
        BinarizationConfig config;
        config.bordersCount_ = 32;
        auto grid = buildGrid(ds, config);

        for (int32_t i = 0; i < grid->nzFeaturesCount(); ++i) {
            EXPECT_LE(grid->conditionsCount(i), 33);
        }
    }

    {
        BinarizationConfig config;
        config.bordersCount_ = 128;
        auto grid = buildGrid(ds, config);

        for (int32_t i = 0; i < grid->nzFeaturesCount(); ++i) {
            EXPECT_LE(grid->conditionsCount(i), 128);
        }
    }

}

TEST(Data, TestBinarize) {
    for (int32_t groupSize : {2, 4, 8, 11, 16, 32}) {
        auto ds = loadFeaturesTxt(PATH_PREFIX "test_data/featuresTxt/train");
        EXPECT_EQ(ds.samplesCount(), 12465);
        EXPECT_EQ(ds.featuresCount(), 50);

        {
            BinarizationConfig config;
            config.bordersCount_ = 32;
            auto grid = buildGrid(ds, config);

            for (int32_t i = 0; i < grid->nzFeaturesCount(); ++i) {
                EXPECT_LE(grid->conditionsCount(i), 33);
            }

            auto bds = binarize(ds, grid, groupSize);

            for (int64_t f = 0; f < grid->nzFeaturesCount(); ++f) {
                int64_t origFeatureIndex = grid->origFeatureIndex(f);
                bds->visitFeature(f, [&](int64_t lineIdx, int64_t bin) {
                    EXPECT_EQ(computeBin(ds.sample(lineIdx).get(origFeatureIndex), grid->borders(f)), bin);
                });
            }
        }
    }
}

