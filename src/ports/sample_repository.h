#pragma once
#include <string>
#include <vector>
#include "domain/sample.h"

namespace sampler {

struct LoadResult {
    SampleLibrary library;
    std::vector<std::string> failed;
};

class SampleRepository {
public:
    virtual ~SampleRepository() = default;
    virtual LoadResult load(const std::string& folder, uint32_t sampleRate) = 0;
};

}  // namespace sampler
