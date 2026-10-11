#pragma once
#include <runtime_windows.hpp>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl/client.h>
#include <span>

namespace vkrt::nes::host {

enum class PresenterKind {
    COMPOSED_BLT,
    FLIP_DIAG
};

class FlipPresenter {
public:
    bool initialize(HWND, PresenterKind kind = PresenterKind::COMPOSED_BLT);
    bool present(std::span<const std::uint8_t> bgra);
    bool active() const noexcept { return swap_ != nullptr; }
    const char* backend() const noexcept { return backend_; }
    void wait_ready();
    ~FlipPresenter(){ disable(); }
private:
    void disable();
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swap_;
    HANDLE latency_{};
    PresenterKind kind_{PresenterKind::COMPOSED_BLT};
    const char* backend_{"GDI_FALLBACK"};
};

}
