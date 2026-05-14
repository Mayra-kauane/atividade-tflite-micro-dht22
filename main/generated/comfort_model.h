#pragma once

struct ComfortCentroid {
    const char *label;
    float temperature_c;
    float humidity_percent;
};

static constexpr ComfortCentroid kComfortCentroids[] = {
    {"agradavel", 24.00f, 54.57f},
    {"frio", 17.00f, 55.71f},
    {"quente", 31.50f, 72.88f},
};

static constexpr int kComfortDatasetRows = 22;
static constexpr int kComfortClassCount = 3;
