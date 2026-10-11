#include <runtime_presenter.hpp>

namespace vkrt::nes::host {
using Microsoft::WRL::ComPtr;

void FlipPresenter::disable(){
    if(latency_){CloseHandle(latency_);latency_=nullptr;}
    swap_.Reset();context_.Reset();device_.Reset();
    backend_="GDI_FALLBACK";
}

bool FlipPresenter::initialize(HWND window, PresenterKind kind){
    disable();
    kind_=kind;

    D3D_FEATURE_LEVEL level{};
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,
        &device_,&level,&context_))) return false;

    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory> factory0;
    if(FAILED(device_.As(&dxgi)) ||
       FAILED(dxgi->GetAdapter(&adapter)) ||
       FAILED(adapter->GetParent(IID_PPV_ARGS(&factory0)))){
        disable();return false;
    }

    HRESULT hr=E_FAIL;

    if(kind_==PresenterKind::COMPOSED_BLT){
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width=512;
        desc.BufferDesc.Height=480;
        desc.BufferDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.BufferDesc.RefreshRate.Numerator=0;
        desc.BufferDesc.RefreshRate.Denominator=1;
        desc.BufferDesc.ScanlineOrdering=DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
        desc.BufferDesc.Scaling=DXGI_MODE_SCALING_UNSPECIFIED;
        desc.SampleDesc.Count=1;
        desc.SampleDesc.Quality=0;
        desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount=1;
        desc.OutputWindow=window;
        desc.Windowed=TRUE;
        desc.SwapEffect=DXGI_SWAP_EFFECT_SEQUENTIAL;
        desc.Flags=0;

        ComPtr<IDXGISwapChain> composed;
        hr=factory0->CreateSwapChain(device_.Get(),&desc,&composed);
        if(SUCCEEDED(hr)){
            swap_=composed;
            backend_="DXGI_COMPOSED_BLT";
        }
    }else{
        ComPtr<IDXGIFactory2> factory2;
        if(SUCCEEDED(factory0.As(&factory2))){
            DXGI_SWAP_CHAIN_DESC1 desc{};
            desc.Width=512;desc.Height=480;
            desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count=1;
            desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount=2;
            desc.Scaling=DXGI_SCALING_STRETCH;
            desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
            desc.Flags=DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

            ComPtr<IDXGISwapChain1> flip;
            hr=factory2->CreateSwapChainForHwnd(device_.Get(),window,&desc,nullptr,nullptr,&flip);
            if(FAILED(hr)){
                desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
                hr=factory2->CreateSwapChainForHwnd(device_.Get(),window,&desc,nullptr,nullptr,&flip);
            }
            if(SUCCEEDED(hr)){
                ComPtr<IDXGISwapChain> base;
                if(SUCCEEDED(flip.As(&base))){
                    swap_=base;
                    backend_="DXGI_FLIP_DIAG";
                    ComPtr<IDXGISwapChain2> latency;
                    if(SUCCEEDED(flip.As(&latency)) &&
                       SUCCEEDED(latency->SetMaximumFrameLatency(1))){
                        latency_=latency->GetFrameLatencyWaitableObject();
                    }
                }else hr=E_NOINTERFACE;
            }
        }
    }

    if(FAILED(hr) || !swap_){
        disable();return false;
    }

    factory0->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER);
    return true;
}

void FlipPresenter::wait_ready(){
    if(latency_) MsgWaitForMultipleObjects(1,&latency_,FALSE,20,QS_ALLINPUT);
}

bool FlipPresenter::present(std::span<const std::uint8_t> bgra){
    if(!swap_ || bgra.size()!=512u*480u*4u) return false;

    ComPtr<ID3D11Texture2D> back;
    if(FAILED(swap_->GetBuffer(0,IID_PPV_ARGS(&back)))){
        disable();return false;
    }

    context_->UpdateSubresource(back.Get(),0,nullptr,bgra.data(),512*4,0);

    const auto hr=swap_->Present(1,0);
    if(hr==DXGI_STATUS_OCCLUDED){
        MsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_ALLINPUT);
        return true;
    }
    if(FAILED(hr)){
        disable();return false;
    }
    return true;
}

}
