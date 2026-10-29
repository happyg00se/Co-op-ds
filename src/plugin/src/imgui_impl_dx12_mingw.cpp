// MinGW's d3d12.h lacks the PFN_D3D12_SERIALIZE_ROOT_SIGNATURE typedef that the ImGui DX12 backend
// expects from the Windows SDK. Provide it, then compile the stock backend unchanged.
#if defined(__MINGW32__)
#include <d3d12.h>
#ifndef __PFN_D3D12_SERIALIZE_ROOT_SIGNATURE_DEFINED
typedef HRESULT(WINAPI* PFN_D3D12_SERIALIZE_ROOT_SIGNATURE)(const D3D12_ROOT_SIGNATURE_DESC* pRootSignature, D3D_ROOT_SIGNATURE_VERSION Version,
                                                            ID3DBlob** ppBlob, ID3DBlob** ppErrorBlob);
#endif
#endif
#include "imgui_impl_dx12.cpp"
