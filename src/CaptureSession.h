#pragma once

#include "WindowCapture.h"

#include <memory>
#include <optional>
#include <string>

namespace gc {

class CaptureSession {
public:
    static CaptureSession* Open(HWND hwnd, GC_Flags flags, std::string& error);
    std::optional<CaptureResult> CaptureFrame(const CaptureRegion* region, std::string& error);
    void Close();
    ~CaptureSession();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gc
