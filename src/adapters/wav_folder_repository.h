#pragma once
#include "ports/sample_repository.h"

namespace sampler {

class WavFolderRepository : public SampleRepository {
public:
    LoadResult load(const std::string& folder, uint32_t sampleRate) override;
};

}  // namespace sampler
